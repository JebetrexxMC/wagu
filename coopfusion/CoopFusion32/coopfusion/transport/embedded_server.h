#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class EmbeddedServer {
public:
    EmbeddedServer();
    ~EmbeddedServer();
    bool Start(uint16_t port);
    void Stop();
    bool IsRunning() const { return m_running.load(); }
    uint16_t Port() const { return m_port; }
    // AML Multiplayer 1.3: the name in INFO / LAN discovery answers
    static void SetName(const std::string& name);

private:
    void Run();
    void CloseClients();
    int m_listenSocket = -1;
    int m_udpSocket = -1;   // LAN discovery (port + 1): "COOP_DISCOVER" -> "COOP_HERE|name|port|players|max|version"
    uint16_t m_port = 0;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};
