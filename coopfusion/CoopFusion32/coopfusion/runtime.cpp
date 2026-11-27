#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "mod/amlmod.h"
#include "mod/config.h"
#include "mod/logger.h"
#include "isautils.h"
#include "transport/coop_client.h"
#include "transport/embedded_server.h"
#include "world/game_hooks.h"
#include "diagnostics/weapon_log.h"
#include "ui/fx.h"

namespace CoopFusion {
namespace {
CoopFusionNet::CoopClient g_client;
EmbeddedServer g_embeddedServer;
ISAUtils* g_saUtils = nullptr;
std::thread g_networkThread;
std::atomic<bool> g_running{false};
std::atomic<bool> g_networkActive{false};
std::atomic<bool> g_stopRequested{false};
std::atomic<bool> g_menuOpen{true};
std::atomic<bool> g_initialized{false};
std::mutex g_statusMutex;
std::string g_status = "Ready";
char g_host[64] = "127.0.0.1";
char g_name[32] = "Player";
char g_portText[8] = "6767";
char g_hostAddress[48] = "";

UI::Fx::Tween g_panelTween;
UI::Fx::Tween g_launcherTween;
UI::Fx::Tween g_keyboardTween;
bool g_panelClosing = false;
UI::Fx::Tween g_settingsTween;
bool g_settingsOpen = false;
bool g_keyboardShift = false;

enum class KeyboardTarget { None, Nickname, Host, Port };
KeyboardTarget g_keyboardTarget = KeyboardTarget::None;
char g_keyboardBuffer[96]{};
size_t g_keyboardMax = 0;
bool g_keyboardNumeric = false;

void SetStatus(const char* value) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_status = value ? value : "";
}

std::string GetStatus() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_status;
}

int PortValue() {
    const long parsed = std::strtol(g_portText, nullptr, 10);
    return parsed >= 1 && parsed <= 65535 ? static_cast<int>(parsed) : 6767;
}

void SaveConnectionSettings() {
    if (!cfg) return;
    ConfigEntry* host = cfg->Bind("ServerHost", "127.0.0.1", "CoopFusion");
    if (host) { host->SetString(g_host); delete host; }
    ConfigEntry* port = cfg->Bind("ServerPort", 6767, "CoopFusion");
    if (port) { port->SetInt(PortValue()); delete port; }
    ConfigEntry* nickname = cfg->Bind("Nickname", "Player", "CoopFusion");
    if (nickname) { nickname->SetString(g_name); delete nickname; }
    ConfigEntry* theme = cfg->Bind("Theme", 4, "CoopFusion");
    if (theme) { theme->SetInt(UI::Fx::CurrentTheme()); delete theme; }
    cfg->Save();
}

void StopClient() {
    g_stopRequested.store(true);
    g_running.store(false);
    g_networkActive.store(false);
    g_client.ShutdownSocket();
    if (g_networkThread.joinable() &&
        g_networkThread.get_id() != std::this_thread::get_id()) {
        g_networkThread.join();
    }
    g_client.Disconnect();
}

void NetworkLoop() {
    if (!g_client.Connect()) {
        g_running.store(false);
        g_networkActive.store(false);
        if (!g_stopRequested.load())
            SetStatus("Connection failed. Check the address and port.");
        g_client.Disconnect();
        return;
    }

    g_running.store(true);
    SetStatus("Connected");
    while (g_running.load() && g_client.IsConnected()) {
        g_client.Pump();
        g_client.PumpGameTime();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    const bool requested = g_stopRequested.load();
    g_running.store(false);
    g_networkActive.store(false);
    g_client.Disconnect();
    if (!requested) SetStatus("Connection closed");
}

void StartClient(const char* host, int port) {
    StopClient();
    g_stopRequested.store(false);
    g_client.Configure(host, port, g_name);
    SetStatus("Connecting...");
    g_running.store(true);
    g_networkActive.store(true);
    g_networkThread = std::thread(NetworkLoop);
}

std::string FindLanAddress() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return "Unavailable";
    alignas(struct ifreq) char buffer[2048]{};
    struct ifconf config{};
    config.ifc_len = sizeof(buffer);
    config.ifc_buf = buffer;
    if (ioctl(fd, SIOCGIFCONF, &config) < 0) {
        close(fd);
        return "Unavailable";
    }
    std::string result = "Unavailable";
    const int count = config.ifc_len / static_cast<int>(sizeof(struct ifreq));
    for (int i = 0; i < count; ++i) {
        const struct ifreq& interface = config.ifc_req[i];
        if (interface.ifr_addr.sa_family != AF_INET ||
            std::strncmp(interface.ifr_name, "lo", 2) == 0) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(&interface.ifr_addr);
        char text[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) {
            result = text;
            break;
        }
    }
    close(fd);
    return result;
}

void StopHost() {
    StopClient();
    g_embeddedServer.Stop();
    g_hostAddress[0] = '\0';
    SetStatus("LAN server stopped");
}

void HostLan() {
    if (g_embeddedServer.IsRunning()) {
        StopHost();
        return;
    }

    StopClient();
    SaveConnectionSettings();
    const int configuredPort = PortValue();
    int activePort = 0;
    for (int candidate = configuredPort;
         candidate <= std::min(65535, configuredPort + 5); ++candidate) {
        EmbeddedServer::SetName(std::string(g_name) + "'s co-op");
        if (g_embeddedServer.Start(static_cast<uint16_t>(candidate))) {
            activePort = candidate;
            break;
        }
    }

    if (!activePort) {
        SetStatus("Could not host: the selected ports are busy.");
        return;
    }
    if (activePort != configuredPort) {
        std::snprintf(g_portText, sizeof(g_portText), "%d", activePort);
        SaveConnectionSettings();
    }
    const std::string address = FindLanAddress();
    std::snprintf(g_hostAddress, sizeof(g_hostAddress), "%s", address.c_str());
    SetStatus("LAN server started. Joining your local session...");
    StartClient("127.0.0.1", activePort);
}

void ConnectOrDisconnect() {
    if (g_networkActive.load() || g_running.load() || g_client.IsConnected()) {
        StopClient();
        SetStatus(g_embeddedServer.IsRunning()
            ? "Disconnected. LAN server is still running."
            : "Disconnected");
        return;
    }
    SaveConnectionSettings();
    StartClient(g_host, PortValue());
}

char* ActiveField() {
    switch (g_keyboardTarget) {
        case KeyboardTarget::Nickname: return g_name;
        case KeyboardTarget::Host: return g_host;
        case KeyboardTarget::Port: return g_portText;
        default: return nullptr;
    }
}

void OpenKeyboard(KeyboardTarget target) {
    g_keyboardTarget = target;
    char* field = ActiveField();
    if (!field) return;
    g_keyboardNumeric = target == KeyboardTarget::Port;
    g_keyboardShift = false;
    g_keyboardMax = target == KeyboardTarget::Nickname ? 31 :
                    target == KeyboardTarget::Host ? 63 : 5;
    std::snprintf(g_keyboardBuffer, sizeof(g_keyboardBuffer), "%s", field);
}

void CommitKeyboard() {
    char* field = ActiveField();
    if (!field) return;
    std::snprintf(field, g_keyboardMax + 1, "%s", g_keyboardBuffer);
    if (g_keyboardTarget == KeyboardTarget::Nickname && !field[0])
        std::snprintf(g_name, sizeof(g_name), "Player");
    if (g_keyboardTarget == KeyboardTarget::Port) {
        const long value = std::strtol(g_portText, nullptr, 10);
        if (value < 1 || value > 65535) {
            SetStatus("Port must be between 1 and 65535.");
            std::snprintf(g_portText, sizeof(g_portText), "6767");
        }
    }
    SaveConnectionSettings();
    g_keyboardTarget = KeyboardTarget::None;
}

void AppendKey(const char* key) {
    const size_t length = std::strlen(g_keyboardBuffer);
    const size_t keyLength = std::strlen(key);
    if (length + keyLength > g_keyboardMax) return;
    std::memcpy(g_keyboardBuffer + length, key, keyLength + 1);
}

bool KbKey(const char* label, float w, float h, bool highlight = false, bool accent = false) {
    int colors = 0;
    if (accent) {
        const ImVec4 c = UI::Fx::AccentButtonColor();
        ImGui::PushStyleColor(ImGuiCol_Button, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
        colors = 3;
    } else if (highlight) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        colors = 1;
    }
    const bool pressed = ImGui::Button(label, ImVec2(w, h));
    if (colors) ImGui::PopStyleColor(colors);
    return pressed;
}

void KbBackspace() {
    const size_t length = std::strlen(g_keyboardBuffer);
    if (length) g_keyboardBuffer[length - 1] = '\0';
}

void DrawKeyboard() {
    const bool keyboardWanted = g_keyboardTarget != KeyboardTarget::None && !g_panelClosing;
    const float kbAnim = UI::Fx::EaseOutCubic(g_keyboardTween.Update(keyboardWanted, 0.22f));
    if (g_keyboardTween.t <= 0.0f && !keyboardWanted) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool numeric = g_keyboardNumeric;
    const float winW = numeric ? std::max(UI::S(560.0f), std::min(UI::S(900.0f), display.x * 0.42f))
                               : display.x * 0.96f;
    const float winH = display.y * 0.58f;
    const float margin = UI::S(10.0f);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((display.x - winW) * 0.5f,
                                   display.y - winH - margin + (1.0f - kbAnim) * display.y * 0.12f),
                            ImGuiCond_Always);
    UI::Fx::PushTheme(kbAnim);
    if (!ImGui::Begin("Keyboard##CoopFusion", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        UI::Fx::PopTheme();
        return;
    }
    UI::Fx::DrawPanelBackground(UI::S(16.0f), false, true);
    ImGui::SetWindowFontScale(std::max(1.0f, std::min(2.8f, UI::S(2.0f))));
    const float gap = UI::S(10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));

    const char* title = g_keyboardTarget == KeyboardTarget::Nickname ? "Player name"
                      : g_keyboardTarget == KeyboardTarget::Host ? "Server address" : "TCP port";
    ImGui::TextColored(UI::Fx::TitleColor(), "%s", title);
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u", static_cast<unsigned>(std::strlen(g_keyboardBuffer)),
                        static_cast<unsigned>(g_keyboardMax));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(UI::S(14.0f), UI::S(9.0f)));
    const float clearW = UI::S(150.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearW - gap);
    ImGui::InputText("##keyboard_preview", g_keyboardBuffer, sizeof(g_keyboardBuffer),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(clearW, ImGui::GetFrameHeight()))) g_keyboardBuffer[0] = '\0';
    ImGui::PopStyleVar();

    const float availX = ImGui::GetContentRegionAvail().x;
    const float availY = ImGui::GetContentRegionAvail().y;
    const float kh = std::max(UI::S(44.0f), std::min(UI::S(100.0f), (availY - gap * 4.0f) / 5.0f));

    auto character = [&](char ch, float w) {
        char shown = ch;
        const bool isLetter = ch >= 'a' && ch <= 'z';
        if (isLetter && g_keyboardShift) shown = static_cast<char>(ch - 'a' + 'A');
        char label[16];
        std::snprintf(label, sizeof(label), "%c##k%c", shown, ch);
        if (ImGui::Button(label, ImVec2(w, kh))) {
            const char key[2] = { shown, 0 };
            AppendKey(key);
            if (isLetter) g_keyboardShift = false;
        }
    };

    if (!numeric) {
        const float u = (availX - gap * 9.0f) / 10.0f;
        auto span = [&](float units) { return u * units + gap * (units - 1.0f); };
        const char* r0 = "1234567890";
        const char* r1 = "qwertyuiop";
        const char* r2 = "asdfghjkl";
        const char* r3 = "zxcvbnm";
        for (int i = 0; i < 10; ++i) { if (i) ImGui::SameLine(); character(r0[i], u); }
        for (int i = 0; i < 10; ++i) { if (i) ImGui::SameLine(); character(r1[i], u); }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (u + gap) * 0.5f);
        for (int i = 0; i < 9; ++i) { if (i) ImGui::SameLine(); character(r2[i], u); }
        if (KbKey("Shift##shift", span(1.5f), kh, g_keyboardShift)) g_keyboardShift = !g_keyboardShift;
        for (int i = 0; i < 7; ++i) { ImGui::SameLine(); character(r3[i], u); }
        ImGui::SameLine();
        if (KbKey("<-##bksp", span(1.5f), kh)) KbBackspace();
        if (KbKey("Cancel##cancel", span(2.0f), kh)) g_keyboardTarget = KeyboardTarget::None;
        ImGui::SameLine(); character('.', u);
        ImGui::SameLine(); character('-', u);
        ImGui::SameLine(); character('_', u);
        ImGui::SameLine();
        if (KbKey("Space##space", span(3.0f), kh)) AppendKey(" ");
        ImGui::SameLine();
        if (KbKey("Done##done", span(2.0f), kh, false, true)) CommitKeyboard();
    } else {
        const float u = (availX - gap * 2.0f) / 3.0f;
        const char* rows[3] = { "123", "456", "789" };
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < 3; ++i) { if (i) ImGui::SameLine(); character(rows[row][i], u); }
        character('0', u * 2.0f + gap);
        ImGui::SameLine();
        if (KbKey("<-##bksp", u, kh)) KbBackspace();
        const float half = (availX - gap) * 0.5f;
        if (KbKey("Cancel##cancel", half, kh)) g_keyboardTarget = KeyboardTarget::None;
        ImGui::SameLine();
        if (KbKey("Done##done", half, kh, false, true)) CommitKeyboard();
    }
    ImGui::PopStyleVar();
    ImGui::End();
    UI::Fx::PopTheme();
}

void DrawField(const char* label, const char* value, KeyboardTarget target) {
    ImGui::TextUnformatted(label);
    ImGui::PushID(static_cast<int>(target));
    if (ImGui::Button(value[0] ? value : "Tap to enter", ImVec2(-1.0f, 42.0f)))
        OpenKeyboard(target);
    ImGui::PopID();
}

void RequestClosePanel() {
    g_panelClosing = true;
    g_settingsOpen = false;
    g_keyboardTarget = KeyboardTarget::None;
}

void DrawThemesWindow() {
    const bool wanted = g_settingsOpen && !g_panelClosing;
    const float raw = g_settingsTween.Update(wanted, wanted ? 0.32f : 0.20f);
    if (raw <= 0.0f && !wanted) return;
    const float fade = UI::Fx::EaseOutCubic(std::min(1.0f, raw * 1.6f));
    const float slide = wanted ? UI::Fx::EaseOutBack(raw) : UI::Fx::EaseOutCubic(raw);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float winW = std::max(UI::S(560.0f), std::min(UI::S(1500.0f), display.x * 0.62f));
    const int columns = winW >= UI::S(1000.0f) ? 5 : 3;
    const int count = UI::Fx::ThemeCount();
    const int rows = (count + columns - 1) / columns;
    const float gap = UI::S(12.0f);
    const float swH = UI::S(190.0f);
    const float closeH = UI::S(62.0f);
    const float headerH = UI::S(70.0f);
    const float winH = headerH + rows * (swH + gap) + closeH + UI::S(40.0f);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((display.x - winW) * 0.5f,
                                   (display.y - winH) * 0.5f + (1.0f - slide) * UI::S(40.0f)),
                            ImGuiCond_Always);
    UI::Fx::PushTheme(fade);
    if (ImGui::Begin("Themes##CoopFusion", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings)) {
        UI::Fx::DrawPanelBackground(UI::S(16.0f), true, true);
        ImGui::SetWindowFontScale(std::max(1.0f, std::min(2.2f, UI::S(1.4f))));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));

        ImGui::TextColored(UI::Fx::TitleColor(), "THEMES");
        ImGui::SameLine();
        ImGui::TextDisabled("Pick a color theme");
        ImGui::Separator();

        const float availX = ImGui::GetContentRegionAvail().x;
        const float swW = (availX - gap * (columns - 1)) / columns;
        for (int i = 0; i < count; ++i) {
            if (i % columns) ImGui::SameLine();
            char id[24];
            std::snprintf(id, sizeof(id), "##theme%d", i);
            if (UI::Fx::ThemeSwatch(id, i, ImVec2(swW, swH), i == UI::Fx::CurrentTheme())) {
                UI::Fx::SetTheme(i);
                SaveConnectionSettings();
            }
        }
        if (ImGui::Button("Done##themes", ImVec2(-1.0f, closeH))) g_settingsOpen = false;
        ImGui::PopStyleVar();
    }
    ImGui::End();
    UI::Fx::PopTheme();
}

void DrawConnectionPanel() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::max(320.0f, std::min(650.0f, display.x * 0.76f));
    const float height = std::max(390.0f, std::min(620.0f, display.y * 0.82f));

    const float raw = g_panelTween.Update(!g_panelClosing, g_panelClosing ? 0.20f : 0.40f);
    if (g_panelClosing && g_panelTween.t <= 0.0f) {
        g_panelClosing = false;
        g_keyboardTween.t = 0.0f;
        g_settingsTween.t = 0.0f;
        g_menuOpen.store(false);
        return;
    }
    const float fade = UI::Fx::EaseOutCubic(std::min(1.0f, raw * 1.6f));
    const float slide = g_panelClosing ? UI::Fx::EaseOutCubic(raw)
                                       : UI::Fx::EaseOutBack(raw);
    const float offsetY = (1.0f - slide) * UI::S(46.0f);
    const float overlay = std::max(UI::Fx::EaseOutCubic(g_keyboardTween.t),
                                   UI::Fx::EaseOutCubic(g_settingsTween.t));
    const bool blocked = g_keyboardTarget != KeyboardTarget::None || g_settingsOpen;

    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f,
                                   (display.y - height) * 0.5f + offsetY), ImGuiCond_Always);
    UI::Fx::PushTheme(fade * (1.0f - 0.4f * overlay));
    if (ImGui::Begin("CoopFusion32", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground)) {
        UI::Fx::DrawPanelBackground(UI::S(16.0f), true, true);
        ImGui::BeginDisabled(blocked);

        ImGui::TextColored(UI::Fx::TitleColor(), "COOPFUSION");
        const ImVec2 headerButton(46.0f, 34.0f);
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x -
                        headerButton.x * 2.0f - gap);
        if (UI::Fx::GearButton("##cf_gear", headerButton, g_settingsOpen))
            g_settingsOpen = !g_settingsOpen;
        ImGui::SameLine();
        if (ImGui::Button("X", headerButton)) RequestClosePanel();
        ImGui::Separator();
        DrawField("Player name", g_name, KeyboardTarget::Nickname);
        DrawField("Server address", g_host, KeyboardTarget::Host);
        DrawField("TCP port", g_portText, KeyboardTarget::Port);

        const std::string status = GetStatus();
        const ImVec4 statusColor = IsConnected()
            ? UI::Fx::GoodColor()
            : (g_embeddedServer.IsRunning() ? UI::Fx::WarnColor() : UI::Fx::IdleColor());
        ImGui::TextColored(statusColor, "Status: %s", status.c_str());
        ImGui::Text("Players: %d", RemotePlayerCount() + (IsConnected() ? 1 : 0));

        const float buttonWidth = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
        const char* connectLabel = (IsConnected() || g_networkActive.load())
            ? "Disconnect" : "Connect";
        if (ImGui::Button(connectLabel, ImVec2(buttonWidth, 52.0f)))
            ConnectOrDisconnect();
        ImGui::SameLine();
        if (ImGui::Button(g_embeddedServer.IsRunning() ? "Stop LAN host" : "Host LAN",
                          ImVec2(buttonWidth, 52.0f)))
            HostLan();

        if (g_embeddedServer.IsRunning()) {
            ImGui::TextColored(UI::Fx::GoodColor(),
                               "Your phone is hosting port %u", g_embeddedServer.Port());
            ImGui::Text("LAN address: %s:%u", g_hostAddress[0] ? g_hostAddress : "Unavailable",
                        g_embeddedServer.Port());
            ImGui::TextWrapped("Share this address with players connected to the same Wi-Fi network or hotspot.");
        } else {
            ImGui::TextWrapped("Connect to a hosted CoopFusion server, or host a session on your local Wi-Fi / hotspot.");
        }
        ImGui::Separator();
        if (ImGui::Button("Close panel", ImVec2(-1.0f, 40.0f)))
            RequestClosePanel();
        ImGui::EndDisabled();
    }
    ImGui::End();
    UI::Fx::PopTheme();
}
}

// AML Multiplayer 1.3: kill feed (combat sync KILL packets), top right under the COOPFUSION button
struct FeedLine { std::string text; std::chrono::steady_clock::time_point at; bool mine; };
std::vector<FeedLine> g_killFeed;

const char* WeaponName(unsigned w) {
    static const char* n[] = { "Fist", "Brass Knuckles", "Golf Club", "Nightstick", "Knife", "Bat", "Shovel", "Pool Cue",
        "Katana", "Chainsaw", "Dildo", "Dildo", "Vibrator", "Vibrator", "Flowers", "Cane", "Grenade", "Tear Gas",
        "Molotov", "Rocket", "Rocket", "Jetpack", "9mm", "Silenced 9mm", "Desert Eagle", "Shotgun", "Sawn-off",
        "Combat Shotgun", "Micro SMG", "MP5", "AK-47", "M4", "Tec-9", "Rifle", "Sniper", "RPG", "Heat-Seeker",
        "Flamethrower", "Minigun", "Satchel", "Detonator", "Spray Can", "Extinguisher", "Camera", "Night Vision",
        "Thermal", "Parachute" };
    return w < sizeof(n) / sizeof(n[0]) ? n[w] : "";
}

void DrawKillFeed(ImDrawList* drawList) {
    const auto now = std::chrono::steady_clock::now();
    for (const auto& k : g_client.DrainKills()) {
        FeedLine f;
        const bool byPlayer = k.killerId && k.killerName[0];
        const char* w = WeaponName(k.weapon);
        f.text = byPlayer ? std::string(k.killerName) + "  [" + (w[0] ? w : "kill") + "]  " + k.victimName
                          : std::string(k.victimName) + " died";
        f.mine = k.killerId == g_client.LocalId() || k.victimId == g_client.LocalId();
        f.at = now;
        g_killFeed.push_back(f);
        if (g_killFeed.size() > 5) g_killFeed.erase(g_killFeed.begin());
    }
    g_killFeed.erase(std::remove_if(g_killFeed.begin(), g_killFeed.end(), [&](const FeedLine& f) {
        return now - f.at > std::chrono::seconds(7); }), g_killFeed.end());
    if (!drawList || g_killFeed.empty()) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float y = 78.0f;
    for (const auto& f : g_killFeed) {
        const ImVec2 sz = ImGui::CalcTextSize(f.text.c_str());
        const float x = display.x - sz.x - 22.0f;
        drawList->AddRectFilled(ImVec2(x - 8.0f, y - 3.0f), ImVec2(display.x - 12.0f, y + sz.y + 3.0f),
                                f.mine ? IM_COL32(120, 20, 20, 170) : IM_COL32(0, 0, 0, 150), 4.0f);
        drawList->AddText(ImVec2(x, y), IM_COL32(255, 255, 255, 235), f.text.c_str());
        y += sz.y + 8.0f;
    }
}

// AML Multiplayer 1.3: the launcher writes AutoConnect=1 (+ ServerHost / ServerPort / Nickname) for a Co-op join
std::atomic<bool> g_autoConnect{false};
std::thread g_autoThread;

bool Init(uintptr_t gameLibrary) {
    if (g_initialized.load()) return true;
    if (cfg) {
        std::snprintf(g_host, sizeof(g_host), "%s",
                      cfg->GetString("ServerHost", "127.0.0.1", "CoopFusion"));
        std::snprintf(g_name, sizeof(g_name), "%s",
                      cfg->GetString("Nickname", "Player", "CoopFusion"));
        UI::Fx::SetTheme(cfg->GetInt("Theme", 4, "CoopFusion"));
        const int port = cfg->GetInt("ServerPort", 6767, "CoopFusion");
        std::snprintf(g_portText, sizeof(g_portText), "%d",
                      std::max(1, std::min(65535, port)));
        g_autoConnect.store(cfg->GetInt("AutoConnect", 0, "CoopFusion") != 0);
    }

    g_saUtils = static_cast<ISAUtils*>(GetInterface("SAUtils"));
    if (!g_saUtils) {
        SetStatus("SAUtils 1.6 is missing; multiplayer hooks are unavailable.");
        if (logger) logger->Error("CoopFusion32: SAUtils 1.6 not found");
        return false;
    }

    WeaponFileLogReset();
    WeaponFileLog("COOPFUSION32_STANDALONE initialized; target=armeabi-v7a");
    g_client.Configure(g_host, PortValue(), g_name);
    InstallGameHooks(gameLibrary, g_client, g_saUtils);
    SetRemoteNameTagsVisible(true);
    SetRemoteHealthBarsVisible(true);
    SetRemoteNameTagScale(0.70f);
    g_initialized.store(true);
    SetStatus("Ready. Connect to a server or host a LAN session.");
    if (g_autoConnect.load()) {
        SetStatus("AML Multiplayer: joining the co-op server...");
        g_autoThread = std::thread([] {
            // wait until the game world is up (the player exists), then connect once
            for (int i = 0; i < 240 && !(g_saUtils && g_saUtils->IsGameInitialised()); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (!IsConnected()) StartClient(g_host, PortValue());
        });
        g_autoThread.detach();
    }
    return true;
}

void Shutdown() {
    StopClient();
    g_embeddedServer.Stop();
    g_initialized.store(false);
}

bool IsConnected() { return g_client.IsConnected(); }
bool IsHosting() { return g_embeddedServer.IsRunning(); }
uint16_t LocalId() { return g_client.LocalId(); }
int RemotePlayerCount() {
    return static_cast<int>(g_client.GetRemotePlayers().size());
}

void DrawHud(ImDrawList* drawList) {
    if (!IsConnected() || g_menuOpen.load()) return;
    DrawRemoteWeaponIcons();
    RenderRemotePlayerNameTags(drawList);
    DrawKillFeed(drawList);
}

void RenderConnectionUI() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool menuOpen = g_menuOpen.load();
    const float launcherRaw = g_launcherTween.Update(!menuOpen, 0.30f);
    if (menuOpen) {
        DrawConnectionPanel();
        DrawThemesWindow();
        DrawKeyboard();
    }
    if (launcherRaw > 0.0f) {
        const float slide = UI::Fx::EaseOutBack(launcherRaw);
        const ImVec2 winSize(176.0f, 64.0f);
        ImGui::SetNextWindowPos(ImVec2(display.x - winSize.x - 12.0f + (1.0f - slide) * 90.0f, 4.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, UI::Fx::EaseOutCubic(launcherRaw));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("##CoopFusionLauncher", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::SetCursorPos(ImVec2(13.0f, 9.0f));
            const ImU32 dot = IsConnected() ? IM_COL32(60, 220, 110, 255)
                : (g_embeddedServer.IsRunning() ? IM_COL32(255, 190, 50, 255) : 0);
            if (menuOpen == false &&
                UI::Fx::LauncherButton("##cf_open", "COOPFUSION", ImVec2(150.0f, 46.0f), dot)) {
                g_menuOpen.store(true);
                g_panelClosing = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}

bool MenuOpen() { return g_menuOpen.load(); }

bool WantsTouchAt(float x, float y, float screenWidth, float screenHeight) {
    if (g_menuOpen.load()) return true;
    return x >= screenWidth - 190.0f && y >= 0.0f && y <= 90.0f &&
           x <= screenWidth && y <= screenHeight;
}
}
