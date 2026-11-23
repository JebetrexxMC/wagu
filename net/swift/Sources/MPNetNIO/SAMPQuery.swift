// MPNetNIO - ask an open.mp / SA-MP server for its info over UDP with SwiftNIO.
// Same query the game's server list and the PC launcher send: "SAMP" + ip + port + 'i'.
import Foundation
import NIOCore
import NIOPosix

public struct SAMPInfo: Sendable, Equatable {
    public var password: Bool
    public var players: Int
    public var maxPlayers: Int
    public var hostname: String
    public var gamemode: String
    public var language: String
    public var pingMs: Double
}

public enum SAMPQueryError: Error { case badAddress, timeout, badReply }

final class SAMPReplyHandler: ChannelInboundHandler {
    typealias InboundIn = AddressedEnvelope<ByteBuffer>
    private let promise: EventLoopPromise<ByteBuffer>
    init(promise: EventLoopPromise<ByteBuffer>) { self.promise = promise }
    func channelRead(context: ChannelHandlerContext, data: NIOAny) {
        let env = unwrapInboundIn(data)
        let b = env.data
        if b.readableBytes >= 11, b.getString(at: b.readerIndex, length: 4) == "SAMP",
           b.getInteger(at: b.readerIndex + 10, as: UInt8.self) == UInt8(ascii: "i") {
            promise.succeed(b)
        }
    }
    func errorCaught(context: ChannelHandlerContext, error: Error) { promise.fail(error) }
}

public enum MPNetNIO {
    /// Builds the 11-byte SA-MP query header + opcode.
    public static func makeQuery(ip: [UInt8], port: Int, op: Character, allocator: ByteBufferAllocator = .init()) -> ByteBuffer {
        var b = allocator.buffer(capacity: 15)
        b.writeString("SAMP")
        b.writeBytes(ip.count == 4 ? ip : [0, 0, 0, 0])
        b.writeInteger(UInt16(truncatingIfNeeded: port), endianness: .little)
        b.writeInteger(op.asciiValue ?? 0)
        return b
    }

    /// Parses an 'i' answer (header included).
    public static func parseInfo(_ input: ByteBuffer, pingMs: Double = 0) -> SAMPInfo? {
        var b = input
        guard b.readableBytes >= 16, b.readString(length: 4) == "SAMP" else { return nil }
        b.moveReaderIndex(forwardBy: 6)
        guard b.readInteger(as: UInt8.self) == UInt8(ascii: "i"),
              let pw = b.readInteger(as: UInt8.self),
              let pl = b.readInteger(endianness: .little, as: UInt16.self),
              let mx = b.readInteger(endianness: .little, as: UInt16.self) else { return nil }
        func str() -> String? {
            guard let n = b.readInteger(endianness: .little, as: UInt32.self), n <= b.readableBytes,
                  let bytes = b.readBytes(length: Int(n)) else { return nil }
            return String(bytes.map { Character(Unicode.Scalar($0)) })   // Latin-1, like the game
        }
        guard let hn = str(), let gm = str() else { return nil }
        let la = str() ?? ""
        return SAMPInfo(password: pw != 0, players: Int(pl), maxPlayers: Int(mx), hostname: hn, gamemode: gm, language: la, pingMs: pingMs)
    }

    /// Queries host:port once ('i'). Uses the shared NIO event loop group.
    public static func query(host: String, port: Int, timeout: TimeAmount = .seconds(2),
                             group: EventLoopGroup = MultiThreadedEventLoopGroup.singleton) async throws -> SAMPInfo {
        let remote = try SocketAddress.makeAddressResolvingHost(host, port: port)
        guard let ipStr = remote.ipAddress else { throw SAMPQueryError.badAddress }
        let ip = ipStr.split(separator: ".").compactMap { UInt8($0) }
        guard ip.count == 4 else { throw SAMPQueryError.badAddress }
        let loop = group.next()
        let promise = loop.makePromise(of: ByteBuffer.self)
        let channel = try await DatagramBootstrap(group: loop)
            .channelInitializer { ch in
                ch.eventLoop.makeCompletedFuture { try ch.pipeline.syncOperations.addHandler(SAMPReplyHandler(promise: promise)) }
            }
            .bind(host: "0.0.0.0", port: 0).get()
        let timer = loop.scheduleTask(in: timeout) { promise.fail(SAMPQueryError.timeout) }
        defer { timer.cancel(); channel.close(promise: nil) }
        let t0 = DispatchTime.now().uptimeNanoseconds
        let q = makeQuery(ip: ip, port: port, op: "i", allocator: channel.allocator)
        try await channel.writeAndFlush(AddressedEnvelope(remoteAddress: remote, data: q)).get()
        let reply = try await promise.futureResult.get()
        let ms = Double(DispatchTime.now().uptimeNanoseconds - t0) / 1_000_000
        guard let info = parseInfo(reply, pingMs: ms) else { throw SAMPQueryError.badReply }
        return info
    }
}
