// mpnet-ws: MP NetWire live status over WebSocket, on POCO (fork: github.com/JebetrexxMC/poco).
//
// mpnetd writes wires.json every round; mpnet-ws watches that file and pushes it to every
// connected browser / app the moment it changes, so a status page or the PC launcher gets
// updates without polling GitHub.
//
//   GET  /wires.json   the current file (HTTP, CORS open)
//   GET  /health       "ok <clients>"
//   WS   /live         sends wires.json on connect and on every change; "ping" -> "pong"
//
//   make ws && build/mpnet-ws -f wires.json -p 9090
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Net/NetException.h>
#include <Poco/File.h>
#include <Poco/Thread.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <cstring>

using namespace Poco::Net;

static std::string g_path = "wires.json";
static std::mutex g_mu;
static std::string g_doc = "{}";
static std::set<WebSocket*> g_clients;
static std::atomic<int> g_count{0};

static void Broadcast(const std::string& s) {
    std::lock_guard<std::mutex> l(g_mu);
    for (auto it = g_clients.begin(); it != g_clients.end();) {
        try { (*it)->sendFrame(s.data(), (int)s.size(), WebSocket::FRAME_TEXT); ++it; }
        catch (...) { it = g_clients.erase(it); }
    }
}

// WebSocket objects live on the handler's stack; remove the one that is leaving.
class LiveHandlerSafe : public HTTPRequestHandler {
public:
    void handleRequest(HTTPServerRequest& req, HTTPServerResponse& res) override {
        WebSocket* me = nullptr;
        try {
            WebSocket ws(req, res);
            me = &ws;
            ws.setReceiveTimeout(Poco::Timespan(60, 0));
            {
                std::lock_guard<std::mutex> l(g_mu);
                ws.sendFrame(g_doc.data(), (int)g_doc.size(), WebSocket::FRAME_TEXT);
                g_clients.insert(&ws);
            }
            g_count++;
            char buf[512];
            int flags = 0, n = 0;
            for (;;) {
                try { n = ws.receiveFrame(buf, sizeof buf, flags); }
                catch (Poco::TimeoutException&) {
                    std::lock_guard<std::mutex> l(g_mu);
                    ws.sendFrame("", 0, WebSocket::FRAME_OP_PING | WebSocket::FRAME_FLAG_FIN);
                    continue;
                }
                if (n <= 0 || (flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE) break;
                if (n == 4 && !memcmp(buf, "ping", 4)) {
                    std::lock_guard<std::mutex> l(g_mu);
                    ws.sendFrame("pong", 4, WebSocket::FRAME_TEXT);
                }
            }
            std::lock_guard<std::mutex> l(g_mu);
            g_clients.erase(&ws);
        } catch (...) {
            std::lock_guard<std::mutex> l(g_mu);
            if (me) g_clients.erase(me);
        }
        if (me) g_count--;
    }
};

class PageHandler : public HTTPRequestHandler {
public:
    void handleRequest(HTTPServerRequest& req, HTTPServerResponse& res) override {
        res.set("Access-Control-Allow-Origin", "*");
        res.set("Cache-Control", "no-store");
        if (req.getURI() == "/health") {
            res.setContentType("text/plain");
            res.send() << "ok " << g_count.load() << "\n";
            return;
        }
        if (req.getURI() == "/wires.json") {
            std::string d;
            { std::lock_guard<std::mutex> l(g_mu); d = g_doc; }
            res.setContentType("application/json");
            res.setContentLength(d.size());
            res.send() << d;
            return;
        }
        res.setStatusAndReason(HTTPResponse::HTTP_NOT_FOUND);
        res.setContentType("text/plain");
        res.send() << "MP NetWire live: /wires.json, /health, ws /live\n";
    }
};

class Factory : public HTTPRequestHandlerFactory {
public:
    HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& req) override {
        if (req.getURI() == "/live" && req.find("Upgrade") != req.end() &&
            Poco::icompare(req["Upgrade"], "websocket") == 0)
            return new LiveHandlerSafe;
        return new PageHandler;
    }
};

static bool Load(Poco::Timestamp& last) {
    try {
        Poco::File f(g_path);
        if (!f.exists() || f.getLastModified() == last) return false;
        last = f.getLastModified();
        std::ifstream in(g_path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string s = ss.str();
        if (s.empty() || s[0] != '{') return false;
        std::lock_guard<std::mutex> l(g_mu);
        if (s == g_doc) return false;
        g_doc = s;
        return true;
    } catch (...) { return false; }
}

int main(int argc, char** argv) {
    int port = 9090;
    for (int i = 1; i + 1 < argc; i++) {
        std::string a = argv[i];
        if (a == "-f") g_path = argv[++i];
        else if (a == "-p") port = std::atoi(argv[++i]);
    }
    Poco::Timestamp last(0);
    Load(last);
    auto* params = new HTTPServerParams;
    params->setMaxThreads(256);
    params->setKeepAlive(true);
    HTTPServer srv(new Factory, ServerSocket((Poco::UInt16)port), params);
    srv.start();
    std::cout << "mpnet-ws on :" << port << " watching " << g_path << std::endl;
    for (;;) {
        Poco::Thread::sleep(1000);
        if (Load(last)) {
            std::string d;
            { std::lock_guard<std::mutex> l(g_mu); d = g_doc; }
            Broadcast(d);
        }
    }
}
