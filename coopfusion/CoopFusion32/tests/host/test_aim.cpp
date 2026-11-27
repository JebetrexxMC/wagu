#include "remote_aim.h"
#include <cstdio>
static int g_fail = 0, g_pass = 0;
#define CHECK(c, m) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
using namespace coop_aim;
int main() {
    CHECK(std::fabs(NormalizeYawRadians(1.0f) - 1.0f) < 1e-6f, "radians pass through");
    CHECK(std::fabs(NormalizeYawRadians(90.0f) - kPi / 2) < 1e-4f, "degrees are converted");
    CHECK(std::fabs(ClampPitch(5.0f, kMaxPlayerPitch) - kMaxPlayerPitch) < 1e-6f, "pitch clamps high");
    CHECK(std::fabs(ClampPitch(-5.0f, kMaxPlayerPitch) + kMaxPlayerPitch) < 1e-6f, "pitch clamps low");
    CHECK(ClampPitch(NAN, kMaxPlayerPitch) == 0.0f, "NaN pitch becomes level");
    CHECK(std::fabs(PitchFromDirection(0, 1, 0)) < 1e-6f, "level view gives zero pitch");
    CHECK(std::fabs(PitchFromDirection(0, 0, 1) - kPi / 2) < 1e-5f, "straight up gives +pi/2");
    CHECK(PitchFromDirection(0, 1, -0.5f) < 0.0f, "looking down gives negative pitch");
    CHECK(std::fabs(YawFromDirection(0, 1)) < 1e-6f, "forward camera ray has zero yaw");
    CHECK(std::fabs(YawFromDirection(-1, 0) - kPi / 2) < 1e-6f, "left camera ray has positive yaw");
    CHECK(std::fabs(YawFromDirection(1, 0) + kPi / 2) < 1e-6f, "right camera ray has negative yaw");

    Point3 o{100, 50, 10};
    Point3 t = FallbackAimTarget(o, 0.0f, 0.0f);
    CHECK(std::fabs(t.x - 100) < 1e-4f && std::fabs(t.y - 58) < 1e-4f, "level fallback target is 8 m ahead on +y");
    CHECK(std::fabs(t.z - (10 + kShoulderHeight)) < 1e-4f, "level fallback target is at shoulder height");
    Point3 steep = FallbackAimTarget(o, 0.0f, 3.0f);
    CHECK(steep.z - o.z < kShoulderHeight + kFallbackRange * std::sin(kMaxPlayerPitch) + 1e-3f, "pitch is clamped in the fallback");
    CHECK(std::fabs(steep.y - o.y) <= kFallbackRange + 1e-4f, "horizontal reach stays within 8 m");
    CHECK(FallbackAimTarget(o, 0, 0.4f).z > FallbackAimTarget(o, 0, 0.0f).z, "positive pitch aims up");
    CHECK(FallbackAimTarget(o, 0, -0.4f).z < FallbackAimTarget(o, 0, 0.0f).z, "negative pitch aims down");
    Point3 side = FallbackAimTarget(o, kPi / 2, 0.0f);
    CHECK(side.x < o.x - 7.9f && std::fabs(side.y - o.y) < 1e-3f, "yaw pi/2 points along -x");
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
