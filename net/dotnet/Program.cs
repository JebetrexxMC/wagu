// mpnet-dotnetty: SA-MP / open.mp query ('i') and NetWire ping for .NET servers and tools (bots, Discord
// bots, admin panels), on DotNetty's event loop (github.com/JebetrexxMC/DotNetty).
//   dotnet run -- wap-fresh.tun.ply.gg 56616          -> JSON: hostname, players, max, mode, ms
//   dotnet run -- 127.0.0.1 7790 --ping               -> MPNETPNG ping to an mpnetd relay / reflector
using System.Diagnostics;
using System.Net;
using System.Text;
using DotNetty.Buffers;
using DotNetty.Transport.Bootstrapping;
using DotNetty.Transport.Channels;
using DotNetty.Transport.Channels.Sockets;

sealed class Reply : SimpleChannelInboundHandler<DatagramPacket>
{
    public readonly TaskCompletionSource<byte[]> Got = new(TaskCreationOptions.RunContinuationsAsynchronously);
    protected override void ChannelRead0(IChannelHandlerContext ctx, DatagramPacket p)
    {
        var b = new byte[p.Content.ReadableBytes];
        p.Content.ReadBytes(b);
        Got.TrySetResult(b);
    }
}

static class Program
{
    static string J(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");

    static async Task<int> Main(string[] args)
    {
        if (args.Length < 2) { Console.Error.WriteLine("mpnet-dotnetty <host> <port> [--ping]"); return 2; }
        bool ping = args.Contains("--ping");
        var ip = (await Dns.GetHostAddressesAsync(args[0])).First(a => a.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork);
        int port = int.Parse(args[1]);
        var ep = new IPEndPoint(ip, port);
        var token = BitConverter.GetBytes(Random.Shared.Next());
        byte[] pkt;
        if (ping) pkt = Encoding.ASCII.GetBytes("MPNETPNG").Concat(token).Concat(new byte[4]).ToArray();
        else pkt = Encoding.ASCII.GetBytes("SAMP").Concat(ip.GetAddressBytes()).Concat(new[] { (byte)(port & 0xff), (byte)(port >> 8), (byte)'i' }).ToArray();

        var group = new MultithreadEventLoopGroup(1);
        try
        {
            for (int attempt = 0; attempt < 3; attempt++)
            {
                var h = new Reply();
                var ch = await new Bootstrap().Group(group).Channel<SocketDatagramChannel>().Handler(h).BindAsync(new IPEndPoint(IPAddress.Any, 0));
                var sw = Stopwatch.StartNew();
                await ch.WriteAndFlushAsync(new DatagramPacket(Unpooled.WrappedBuffer(pkt), ep));
                var done = await Task.WhenAny(h.Got.Task, Task.Delay(1500));
                await ch.CloseAsync();
                if (done != h.Got.Task) continue;
                var b = h.Got.Task.Result;
                double ms = sw.Elapsed.TotalMilliseconds;
                if (ping)
                {
                    if (b.Length < 12 || Encoding.ASCII.GetString(b, 0, 8) != "MPNETPNG" || !b.AsSpan(8, 4).SequenceEqual(token)) continue;
                    Console.WriteLine($"{{\"ok\":true,\"ms\":{ms:F2}}}");
                    return 0;
                }
                if (b.Length < 16 || Encoding.ASCII.GetString(b, 0, 4) != "SAMP" || b[10] != 'i') continue;
                int p = 11;
                bool pw = b[p] != 0; int players = BitConverter.ToUInt16(b, p + 1); int max = BitConverter.ToUInt16(b, p + 3); p += 5;
                string Str() { int l = BitConverter.ToInt32(b, p); p += 4; var s = Encoding.Latin1.GetString(b, p, Math.Min(l, b.Length - p)); p += l; return s; }
                string host = Str(), mode = Str(), lang = Str();
                Console.WriteLine($"{{\"ok\":true,\"ms\":{ms:F1},\"password\":{pw.ToString().ToLower()},\"players\":{players},\"max\":{max},\"hostname\":\"{J(host)}\",\"mode\":\"{J(mode)}\",\"language\":\"{J(lang)}\"}}");
                return 0;
            }
            Console.WriteLine("{\"ok\":false,\"error\":\"no answer\"}");
            return 1;
        }
        finally { await group.ShutdownGracefullyAsync(TimeSpan.FromMilliseconds(100), TimeSpan.FromSeconds(1)); }
    }
}
