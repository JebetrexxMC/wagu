// 5.00.04: the host's own address + location for custom servers (included at the end of online.cpp).
//
// Before, every custom server was reached through the lobby broker only: friend -> broker -> host phone
// -> broker -> friend, so a server next door had the ping of the broker's city (Frankfurt for the public
// lobby, the Asia box for everyone else). Now the host side finds:
//   - its own location (city / region / country from its public IP, ipinfo.io with ipwho.is as backup):
//     shown on the host's console page and used as the Server location when none was picked;
//   - a way in: the router's UPnP port mapping (UDP game port, like PC games), the owner's own free
//     DuckDNS name (yourname.duckdns.org -> the host's IP, refreshed every 5 minutes, the duckdns-mirror /
//     ha-duckdns idea) or an address typed by hand (a port-forwarded router, a VPS, a tunnel);
// and puts "^address/port" into its lobby listing. Players then probe that address over UDP like the
// 24/7 servers (3.11): when it answers they connect straight to the host - the ping is the real distance
// between the two phones (NA host + NA friend = low, NA host + Asia friend = higher but no detour) - and
// when it does not (mobile data / CGNAT, no mapping) the relay is used like before.

static std::mutex  s_HaMx;
static Online::HostAddrInfo s_Ha;            // guarded by s_HaMx
static std::string s_HaManual, s_HaDuckName, s_HaDuckTok;   // guarded
static int         s_nHaMode = 1;            // guarded: 0 = off, 1 = auto, 2 = always announce the public IP
static std::atomic<bool>     s_bHaThread{false};
static std::atomic<bool>     s_bHaHosting{false};
static std::atomic<uint16_t> s_nHaPort{0};

static std::string HaJsonStr(const std::string& j, const char* key)
{   // tiny flat-JSON reader: "key": "value" (strings only)
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if(p == std::string::npos) return "";
    p = j.find(':', p + k.size()); if(p == std::string::npos) return "";
    p = j.find_first_not_of(" \t\r\n", p + 1);
    if(p == std::string::npos || j[p] != '"') return "";
    std::string out;
    for(size_t i = p + 1; i < j.size() && j[i] != '"' && out.size() < 64; ++i)
    {
        if(j[i] == '\\' && i + 1 < j.size()) { ++i; if(j[i] == 'u') { i += 4; out += '?'; continue; } }
        out += j[i];
    }
    for(char& c : out) if(c == '|' || c == '^' || c == '\n' || c == '\r') c = ' ';
    return out;
}
static bool HaIsPrivateV4(uint32_t hostOrder)
{
    return (hostOrder >> 24) == 10 || (hostOrder >> 20) == (172u << 4 | 1) || (hostOrder >> 16) == (192u << 8 | 168) ||
           (hostOrder >> 22) == (100u << 2 | 1) /* 100.64/10 carrier NAT */ || (hostOrder >> 24) == 127 || (hostOrder >> 16) == (169u << 8 | 254) || hostOrder == 0;
}
static bool HaParseV4(const std::string& s, uint32_t* hostOrder)
{
    in_addr a; if(s.empty() || inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
    if(hostOrder) *hostOrder = ntohl(a.s_addr);
    return true;
}

// ---- location ----------------------------------------------------------------------------------
static bool HaLocate(Online::HostAddrInfo& o)
{
    std::string body, why;
    if(HttpsGet("https://ipinfo.io/json", body, 8000, why, 8192) && !HaJsonStr(body, "ip").empty())
    {
        MP_CopyStr(o.publicIp, HaJsonStr(body, "ip").c_str(), sizeof(o.publicIp));
        MP_CopyStr(o.city, HaJsonStr(body, "city").c_str(), sizeof(o.city));
        MP_CopyStr(o.region, HaJsonStr(body, "region").c_str(), sizeof(o.region));
        MP_CopyStr(o.cc, HaJsonStr(body, "country").c_str(), sizeof(o.cc));
        MP_CopyStr(o.isp, HaJsonStr(body, "org").c_str(), sizeof(o.isp));
        return true;
    }
    if(HttpsGet("https://ipwho.is/", body, 8000, why, 16384) && !HaJsonStr(body, "ip").empty())
    {
        MP_CopyStr(o.publicIp, HaJsonStr(body, "ip").c_str(), sizeof(o.publicIp));
        MP_CopyStr(o.city, HaJsonStr(body, "city").c_str(), sizeof(o.city));
        MP_CopyStr(o.region, HaJsonStr(body, "region").c_str(), sizeof(o.region));
        MP_CopyStr(o.cc, HaJsonStr(body, "country_code").c_str(), sizeof(o.cc));
        MP_CopyStr(o.isp, HaJsonStr(body, "isp").c_str(), sizeof(o.isp));
        return true;
    }
    return false;
}

// ---- UPnP IGD (router port mapping) -------------------------------------------------------------
struct HaIgd { std::string host; uint16_t port = 80; std::string ctrl, svc, localIp; };
static bool HaHttpPlain(const std::string& host, uint16_t port, const std::string& req, std::string& resp, int timeoutMs)
{
    resp.clear();
    char e[64] = "";
    int fd = TcpConnectTimed(host, port, timeoutMs, nullptr, e, sizeof(e));
    if(fd < 0) return false;
    uint64_t until = MP_NowMs() + (uint64_t)timeoutMs;
    size_t off = 0;
    while(off < req.size() && MP_NowMs() < until)
    {
        ssize_t w = send(fd, req.data() + off, req.size() - off, MSG_NOSIGNAL);
        if(w > 0) { off += (size_t)w; continue; }
        if(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { pollfd p = { fd, POLLOUT, 0 }; poll(&p, 1, 100); continue; }
        break;
    }
    char buf[4096];
    while(MP_NowMs() < until && resp.size() < 64 * 1024)
    {
        pollfd p = { fd, POLLIN, 0 };
        if(poll(&p, 1, 200) <= 0) continue;
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if(r > 0) { resp.append(buf, (size_t)r); continue; }
        if(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break;
    }
    close(fd);
    return !resp.empty();
}
static bool HaSplitUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path)
{
    if(url.compare(0, 7, "http://")) return false;
    std::string r = url.substr(7);
    size_t sl = r.find('/');
    host = r.substr(0, sl); path = sl == std::string::npos ? "/" : r.substr(sl);
    port = 80;
    size_t co = host.find(':'); if(co != std::string::npos) { port = (uint16_t)atoi(host.c_str() + co + 1); host = host.substr(0, co); }
    return !host.empty();
}
static std::string HaTag(const std::string& x, const char* tag, size_t from = 0, size_t* at = nullptr)
{
    std::string o = std::string("<") + tag + ">", c = std::string("</") + tag + ">";
    size_t a = x.find(o, from); if(a == std::string::npos) return "";
    size_t b = x.find(c, a); if(b == std::string::npos) return "";
    if(at) *at = b;
    return x.substr(a + o.size(), b - a - o.size());
}
static bool HaDiscover(HaIgd& g, std::string& why)
{
    int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(fd < 0) { why = "no socket"; return false; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in to; memset(&to, 0, sizeof(to)); to.sin_family = AF_INET; to.sin_port = htons(1900); inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
    const char* sts[] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2" };
    for(const char* st : sts)
    {
        char m[256]; int n = snprintf(m, sizeof(m), "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", st);
        sendto(fd, m, n, 0, (sockaddr*)&to, sizeof(to));
    }
    std::string loc;
    uint64_t t0 = MP_NowMs();
    while(MP_NowMs() - t0 < 2500 && loc.empty())
    {
        pollfd p = { fd, POLLIN, 0 };
        if(poll(&p, 1, 100) <= 0) continue;
        char b[1500]; int r = (int)recv(fd, b, sizeof(b) - 1, 0);
        if(r <= 0) continue;
        b[r] = 0;
        std::string s(b);
        std::string low = s; for(char& c : low) c = (char)tolower((unsigned char)c);
        size_t l = low.find("\nlocation:");
        if(l == std::string::npos) continue;
        size_t v = s.find_first_not_of(" \t", l + 10), e = s.find_first_of("\r\n", v);
        if(v != std::string::npos) loc = s.substr(v, e == std::string::npos ? std::string::npos : e - v);
    }
    close(fd);
    if(loc.empty()) { why = "no UPnP router answered (mobile data, or UPnP is off on the router)"; return false; }
    std::string path;
    if(!HaSplitUrl(loc, g.host, g.port, path)) { why = "bad router address"; return false; }
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + g.host + ":" + std::to_string(g.port) + "\r\nConnection: close\r\n\r\n", resp;
    if(!HaHttpPlain(g.host, g.port, req, resp, 4000)) { why = "router description not readable"; return false; }
    size_t pos = 0;
    while(true)
    {
        size_t end = 0;
        std::string svc = HaTag(resp, "service", pos, &end);
        if(svc.empty()) break;
        pos = end;
        std::string type = HaTag(svc, "serviceType");
        if(type.find("WANIPConnection") == std::string::npos && type.find("WANPPPConnection") == std::string::npos) continue;
        g.svc = type; g.ctrl = HaTag(svc, "controlURL");
        break;
    }
    if(g.ctrl.empty()) { why = "the router has no port mapping service"; return false; }
    if(!g.ctrl.compare(0, 7, "http://")) { std::string h; uint16_t pt; std::string pa; if(HaSplitUrl(g.ctrl, h, pt, pa)) { g.host = h; g.port = pt; g.ctrl = pa; } }
    if(g.ctrl[0] != '/') g.ctrl = "/" + g.ctrl;
    // our LAN address as the router sees it
    int u = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in ra; memset(&ra, 0, sizeof(ra)); ra.sin_family = AF_INET; ra.sin_port = htons(g.port);
    if(u >= 0 && inet_pton(AF_INET, g.host.c_str(), &ra.sin_addr) == 1 && connect(u, (sockaddr*)&ra, sizeof(ra)) == 0)
    {
        sockaddr_in la; socklen_t ll = sizeof(la);
        if(getsockname(u, (sockaddr*)&la, &ll) == 0) { char ip[32]; inet_ntop(AF_INET, &la.sin_addr, ip, sizeof(ip)); g.localIp = ip; }
    }
    if(u >= 0) close(u);
    if(g.localIp.empty()) { why = "own LAN address unknown"; return false; }
    return true;
}
static bool HaSoap(const HaIgd& g, const char* action, const std::string& args, std::string& resp)
{
    std::string body = "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
                       std::string(action) + " xmlns:u=\"" + g.svc + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    std::string req = "POST " + g.ctrl + " HTTP/1.1\r\nHost: " + g.host + ":" + std::to_string(g.port) + "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + g.svc + "#" + action +
                      "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    if(!HaHttpPlain(g.host, g.port, req, resp, 5000)) return false;
    return resp.compare(0, 12, "HTTP/1.1 200") == 0 || resp.compare(0, 12, "HTTP/1.0 200") == 0;
}
static bool HaMap(const HaIgd& g, uint16_t port, std::string& why)
{
    std::string resp;
    for(int lease : { 3600, 0 })   // some routers only take permanent mappings (error 725)
    {
        char a[512];
        snprintf(a, sizeof(a), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>%u</NewInternalPort>"
                               "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>MP Multiplayer</NewPortMappingDescription>"
                               "<NewLeaseDuration>%d</NewLeaseDuration>", port, port, g.localIp.c_str(), lease);
        if(HaSoap(g, "AddPortMapping", a, resp)) return true;
    }
    std::string code = HaTag(resp, "errorCode"), desc = HaTag(resp, "errorDescription");
    why = "the router refused the port mapping" + (code.empty() ? std::string() : " (" + code + (desc.empty() ? "" : " " + desc) + ")");
    return false;
}
static void HaUnmap(const HaIgd& g, uint16_t port)
{
    char a[256]; std::string resp;
    snprintf(a, sizeof(a), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>", port);
    HaSoap(g, "DeletePortMapping", a, resp);
}

// ---- the worker ----------------------------------------------------------------------------------
static void HostAddrThread()
{
    HaIgd igd; bool mapped = false; uint16_t mappedPort = 0;
    uint64_t locAt = 0, upnpAt = 0, duckAt = 0;
    while(s_bStarted)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const bool hosting = s_bHaHosting.load() && !s_bHostDedicated;
        const uint16_t port = s_nHaPort.load();
        const uint64_t now = MP_NowMs();
        int mode; std::string manual, dname, dtok;
        { std::lock_guard<std::mutex> lk(s_HaMx); mode = s_nHaMode; manual = s_HaManual; dname = s_HaDuckName; dtok = s_HaDuckTok; }
        if(!hosting || mode == 0)
        {
            if(mapped) { HaUnmap(igd, mappedPort); mapped = false; }
            std::lock_guard<std::mutex> lk(s_HaMx);
            s_Ha.announce[0] = 0; s_Ha.hosting = false; upnpAt = 0;
            if(!hosting) continue;
            MP_CopyStr(s_Ha.upnp, "off (Direct connections: Off)", sizeof(s_Ha.upnp));
            continue;
        }
        if(!locAt || now - locAt > 10 * 60 * 1000)
        {
            Online::HostAddrInfo o; { std::lock_guard<std::mutex> lk(s_HaMx); o = s_Ha; }
            bool ok = HaLocate(o);
            locAt = ok ? now : now - 9 * 60 * 1000;   // a failed lookup: again in 1 minute
            if(ok) { std::lock_guard<std::mutex> lk(s_HaMx); MP_CopyStr(s_Ha.publicIp, o.publicIp, sizeof(s_Ha.publicIp)); MP_CopyStr(s_Ha.city, o.city, sizeof(s_Ha.city)); MP_CopyStr(s_Ha.region, o.region, sizeof(s_Ha.region)); MP_CopyStr(s_Ha.cc, o.cc, sizeof(s_Ha.cc)); MP_CopyStr(s_Ha.isp, o.isp, sizeof(s_Ha.isp)); }
            OLOG("5.00.04: host location %s, %s, %s (%s) %s", o.city, o.region, o.cc, o.publicIp, ok ? "" : "- lookup failed");
        }
        if(port && (!upnpAt || now - upnpAt > 20 * 60 * 1000 || mappedPort != port))
        {
            upnpAt = now;
            if(mapped && mappedPort != port) { HaUnmap(igd, mappedPort); mapped = false; }
            std::string why, st;
            HaIgd g; bool ok = HaDiscover(g, why) && HaMap(g, port, why);
            bool cgnat = false; std::string wan;
            if(ok)
            {
                igd = g; mapped = true; mappedPort = port;
                std::string resp;
                if(HaSoap(g, "GetExternalIPAddress", "", resp)) wan = HaTag(resp, "NewExternalIPAddress");
                uint32_t w = 0; cgnat = HaParseV4(wan, &w) && HaIsPrivateV4(w);
                st = "UDP " + std::to_string(port) + " open on the router (" + g.localIp + ")" + (cgnat ? " - but the router itself is behind carrier NAT (" + wan + ")" : "");
            }
            else st = why;
            std::lock_guard<std::mutex> lk(s_HaMx);
            MP_CopyStr(s_Ha.upnp, st.c_str(), sizeof(s_Ha.upnp));
            s_Ha.upnpOk = ok && !cgnat; s_Ha.cgnat = cgnat;
            OLOG("5.00.04: UPnP: %s", st.c_str());
        }
        if(!dname.empty() && !dtok.empty() && (!duckAt || now - duckAt > 5 * 60 * 1000))
        {
            duckAt = now;
            std::string sub = dname; size_t d = sub.find(".duckdns.org"); if(d != std::string::npos) sub = sub.substr(0, d);
            std::string body, why;
            bool ok = HttpsGet("https://www.duckdns.org/update?domains=" + sub + "&token=" + dtok + "&ip=", body, 8000, why, 1024) && body.compare(0, 2, "OK") == 0;
            std::lock_guard<std::mutex> lk(s_HaMx);
            snprintf(s_Ha.duck, sizeof(s_Ha.duck), "%s.duckdns.org %s", sub.c_str(), ok ? "points at this phone (updated every 5 min)" : ("update failed: " + (body.empty() ? why : body)).c_str());
        }
        else if(dname.empty()) { std::lock_guard<std::mutex> lk(s_HaMx); s_Ha.duck[0] = 0; duckAt = 0; }
        // what we put into the listing
        std::string ann;
        if(!dname.empty()) { ann = dname; if(ann.find('.') == std::string::npos) ann += ".duckdns.org"; }
        else if(!manual.empty()) ann = manual;
        else
        {
            std::lock_guard<std::mutex> lk(s_HaMx);
            uint32_t ip = 0;
            if(HaParseV4(s_Ha.publicIp, &ip) && !HaIsPrivateV4(ip) && (mode == 2 || s_Ha.upnpOk)) ann = s_Ha.publicIp;
        }
        for(char& c : ann) if(c == '|' || c == '^' || c == ':' || c == '/' || c == ' ') c = 0;
        ann = ann.c_str();
        if(ann.size() > 47) ann.clear();
        std::lock_guard<std::mutex> lk(s_HaMx);
        if(ann.empty()) s_Ha.announce[0] = 0;
        else snprintf(s_Ha.announce, sizeof(s_Ha.announce), "%s/%u", ann.c_str(), port);
        s_Ha.hosting = true;
    }
}
static std::string HaAnnounceNow()
{
    std::lock_guard<std::mutex> lk(s_HaMx);
    return s_Ha.announce;
}

void Online::SetHostAddrConfig(int mode, const char* manualHost, const char* duckName, const char* duckToken)
{
    std::lock_guard<std::mutex> lk(s_HaMx);
    s_nHaMode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
    s_HaManual = manualHost ? manualHost : "";
    s_HaDuckName = duckName ? duckName : "";
    s_HaDuckTok = duckToken ? duckToken : "";
}
void Online::HostAddrTick(bool customHosting, uint16_t port)
{
    s_bHaHosting = customHosting; s_nHaPort = port;
    if(customHosting && s_bStarted && !s_bHaThread.exchange(true)) std::thread(HostAddrThread).detach();
}
void Online::GetHostAddr(HostAddrInfo& out)
{
    std::lock_guard<std::mutex> lk(s_HaMx);
    out = s_Ha;
}
bool Online::DirectHostOf(const char* relayIp, char* out, size_t n, int* rttMs)
{
    std::lock_guard<std::mutex> lk(s_M);
    uint32_t tok = TokenForRelayIp(relayIp);
    if(!tok) return false;
    auto l = s_List.find(tok);
    if(l == s_List.end() || !l->second.direct[0]) return false;
    auto d = s_Direct.find(tok);
    bool ok = d != s_Direct.end() && d->second.ok && MP_NowMs() - d->second.at < 90000;
    snprintf(out, n, "%s:%u", l->second.direct, l->second.directPort ? l->second.directPort : l->second.port);
    if(rttMs) *rttMs = ok ? d->second.rtt : -1;
    return true;
}
