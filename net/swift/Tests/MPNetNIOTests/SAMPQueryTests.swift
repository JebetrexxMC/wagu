import XCTest
import NIOCore
@testable import MPNetNIO

final class SAMPQueryTests: XCTestCase {
    func testQueryHeader() {
        let q = MPNetNIO.makeQuery(ip: [127, 0, 0, 1], port: 7777, op: "i")
        XCTAssertEqual(Array(q.readableBytesView), Array("SAMP".utf8) + [127, 0, 0, 1, 0x61, 0x1E, UInt8(ascii: "i")])
    }

    func testParseInfo() {
        var b = MPNetNIO.makeQuery(ip: [1, 2, 3, 4], port: 7777, op: "i")
        b.writeInteger(UInt8(1))
        b.writeInteger(UInt16(12), endianness: .little)
        b.writeInteger(UInt16(100), endianness: .little)
        for s in ["MP Crossplay 24/7", "Freeroam", "English"] {
            b.writeInteger(UInt32(s.utf8.count), endianness: .little)
            b.writeString(s)
        }
        let info = MPNetNIO.parseInfo(b, pingMs: 42)
        XCTAssertEqual(info, SAMPInfo(password: true, players: 12, maxPlayers: 100, hostname: "MP Crossplay 24/7",
                                      gamemode: "Freeroam", language: "English", pingMs: 42))
        XCTAssertNil(MPNetNIO.parseInfo(ByteBuffer(string: "SAMPxx")))
    }
}
