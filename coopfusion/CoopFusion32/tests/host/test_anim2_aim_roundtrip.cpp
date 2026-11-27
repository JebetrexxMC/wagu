#include "coopfusion/transport/embedded_server.h"
#include "coopfusion/transport/coop_client.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <algorithm>

static void Pump(CoopFusionNet::CoopClient& a, CoopFusionNet::CoopClient& b, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        a.Pump(); b.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int main() {
    using namespace CoopFusionNet;
    constexpr uint16_t port = 7317;
    EmbeddedServer server;
    if (!server.Start(port)) {
        std::printf("embedded server did not start\n");
        return 2;
    }
    CoopClient sender, receiver;
    sender.Configure("127.0.0.1", port, "AimSender");
    receiver.Configure("127.0.0.1", port, "AimReceiver");
    if (!sender.Connect() || !receiver.Connect()) {
        std::printf("client connect failed\n");
        return 3;
    }
    Pump(sender, receiver, 250);

    sender.SetLocalTransform(10.0f, 20.0f, 3.0f, 90.0f);
    AnimationProfile profile{};
    profile.sequence = 1;
    profile.locomotion = kMoveStill;
    profile.action = AnimationAction::Aim;
    profile.flags = static_cast<uint16_t>(ANIM_AIMING | ANIM_UPPER_BODY | ANIM_AIM_FIRST_PERSON);
    profile.weaponState = 0;
    profile.animGroup = 0;
    profile.phase = 0.0f;
    profile.blend = 1.0f;
    profile.aimYaw = 0.73f;
    profile.aimPitch = -0.31f;
    sender.SetLocalAnimationProfile(profile);

    bool received = false;
    RemotePlayer remote{};
    for (int i = 0; i < 60 && !received; ++i) {
        Pump(sender, receiver, 50);
        auto players = receiver.GetRemotePlayers();
        auto it = std::find_if(players.begin(), players.end(),
            [&](const RemotePlayer& p) { return p.id == sender.LocalId(); });
        if (it != players.end() && it->animation.sequence == profile.sequence) {
            remote = *it;
            received = true;
        }
    }

    int failed = 0;
    auto check = [&](bool ok, const char* label) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failed;
    };
    check(received, "receiver got sender's ANIM2 animation profile");
    check(received && (remote.animation.flags & ANIM_AIM_FIRST_PERSON),
          "first-person bit 8 round-trips through the existing flags field");
    check(received && std::fabs(remote.animation.aimYaw - profile.aimYaw) < 1e-3f,
          "camera yaw round-trips");
    check(received && std::fabs(remote.animation.aimPitch - profile.aimPitch) < 1e-3f,
          "camera pitch round-trips");

    sender.Disconnect();
    receiver.Disconnect();
    server.Stop();
    std::printf("%d failed\n", failed);
    return failed ? 1 : 0;
}
