#pragma once

#include <algorithm>
#include <cmath>

namespace coop_aim {

constexpr float kPi = 3.14159265358979f;
constexpr float kMaxPlayerPitch = 1.2f;
constexpr float kMaxNpcPitch = 0.5f;
constexpr float kFallbackRange = 8.0f;
constexpr float kShoulderHeight = 1.25f;

struct Point3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline float NormalizeYawRadians(float yaw) {
    if (!std::isfinite(yaw)) return 0.0f;
    if (std::fabs(yaw) > 2.0f * kPi && std::fabs(yaw) <= 360.0f) yaw *= kPi / 180.0f;
    return yaw;
}

inline float ClampPitch(float pitch, float limit) {
    if (!std::isfinite(pitch)) return 0.0f;
    return std::max(-limit, std::min(limit, pitch));
}

inline float PitchFromDirection(float fx, float fy, float fz) {
    const float horizontal = std::sqrt(fx * fx + fy * fy);
    if (!std::isfinite(horizontal) || !std::isfinite(fz)) return 0.0f;
    return std::atan2(fz, horizontal);
}

inline float YawFromDirection(float fx, float fy) {
    if (!std::isfinite(fx) || !std::isfinite(fy) ||
        (std::fabs(fx) < 1.0e-6f && std::fabs(fy) < 1.0e-6f)) return 0.0f;
    return std::atan2(-fx, fy);
}

inline Point3 FallbackAimTarget(Point3 origin, float yaw, float pitch) {
    yaw = NormalizeYawRadians(yaw);
    pitch = ClampPitch(pitch, kMaxPlayerPitch);
    const float cp = std::cos(pitch);
    return Point3{origin.x - std::sin(yaw) * cp * kFallbackRange,
                  origin.y + std::cos(yaw) * cp * kFallbackRange,
                  origin.z + kShoulderHeight + std::sin(pitch) * kFallbackRange};
}

}
