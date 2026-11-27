#pragma once

#include <cstddef>
#include <cmath>

namespace coop_cutscene {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct PartnerSpot {
    float x;
    float y;
    float z;
    float heading;
};

struct ShotTracker {
    Vec3 camera{};
    Vec3 forward{};
    Vec3 cj{};
    unsigned int shotIndex = 0;
    unsigned long long lastShotAtMs = 0;
    bool initialized = false;
};

struct ActorMotionTracker {
    Vec3 previous{};
    Vec3 direction{0.0f, 1.0f, 0.0f};
    float speed = 0.0f;
    unsigned int movingSamples = 0;
    unsigned int stillSamples = 0;
    unsigned long long lastSampleAtMs = 0;
    bool initialized = false;
    bool moving = false;
};

inline bool UpdateActorMotion(ActorMotionTracker& state, const Vec3& position,
                              unsigned long long nowMs, bool cameraCut) {
    const bool valid = std::isfinite(position.x) && std::isfinite(position.y) &&
                       std::isfinite(position.z);
    if (!valid) {
        state.moving = false;
        state.speed = 0.0f;
        state.movingSamples = state.stillSamples = 0;
        state.initialized = false;
        return false;
    }

    if (!state.initialized || cameraCut || nowMs <= state.lastSampleAtMs ||
        nowMs - state.lastSampleAtMs > 250ULL) {
        state.previous = position;
        state.lastSampleAtMs = nowMs;
        state.speed = 0.0f;
        state.movingSamples = state.stillSamples = 0;
        state.moving = false;
        state.initialized = true;
        return false;
    }

    const float dt = static_cast<float>(nowMs - state.lastSampleAtMs) / 1000.0f;
    const float dx = position.x - state.previous.x;
    const float dy = position.y - state.previous.y;
    const float dz = position.z - state.previous.z;
    const float horizontalDistance = std::sqrt(dx * dx + dy * dy);
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    state.previous = position;
    state.lastSampleAtMs = nowMs;

    if (!std::isfinite(distance) || distance > 0.60f || dt < 0.008f) {
        state.speed = 0.0f;
        state.movingSamples = state.stillSamples = 0;
        state.moving = false;
        return false;
    }

    state.speed = horizontalDistance / dt;
    const float startWalkSpeed = 0.22f;
    const float stopWalkSpeed = 0.12f;
    const float maxWalkSpeed = 5.5f;
    const bool withinWalkSpeed = state.speed <= maxWalkSpeed;
    const bool hasHorizontalMovement = horizontalDistance >= 0.003f;
    if (withinWalkSpeed && hasHorizontalMovement &&
        (state.moving ? state.speed >= stopWalkSpeed : state.speed >= startWalkSpeed)) {
        state.direction = {dx / horizontalDistance, dy / horizontalDistance, 0.0f};
        ++state.movingSamples;
        state.stillSamples = 0;
        if (state.movingSamples >= 2) state.moving = true;
    } else if (state.speed < stopWalkSpeed) {
        ++state.stillSamples;
        state.movingSamples = 0;
        if (state.stillSamples >= 3) state.moving = false;
    } else if (!withinWalkSpeed) {
        state.speed = 0.0f;
        state.movingSamples = state.stillSamples = 0;
        state.moving = false;
    } else {
        state.movingSamples = 0;
        state.stillSamples = 0;
    }
    return state.moving;
}

inline float DistanceSquared(const Vec3& a, const Vec3& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z;
}

inline bool UpdateShotTracker(ShotTracker& state,
                              const Vec3& camera,
                              const Vec3& forward,
                              const Vec3& cj,
                              unsigned long long nowMs) {
    const float cameraLen2 = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
    const bool valid = std::isfinite(camera.x) && std::isfinite(camera.y) && std::isfinite(camera.z) &&
                       std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
                       std::isfinite(cj.x) && std::isfinite(cj.y) && std::isfinite(cj.z) &&
                       std::isfinite(cameraLen2) && cameraLen2 > 0.01f;
    if (!valid) return false;

    const float invLen = 1.0f / std::sqrt(cameraLen2);
    const Vec3 normalized{forward.x * invLen, forward.y * invLen, forward.z * invLen};
    if (!state.initialized) {
        state.camera = camera;
        state.forward = normalized;
        state.cj = cj;
        state.shotIndex = 1;
        state.lastShotAtMs = nowMs;
        state.initialized = true;
        return true;
    }

    const float cameraJump2 = DistanceSquared(camera, state.camera);
    const float cjJump2 = DistanceSquared(cj, state.cj);
    const float facingDot = normalized.x * state.forward.x +
                            normalized.y * state.forward.y +
                            normalized.z * state.forward.z;
    const bool hardCameraCut = cameraJump2 >= (2.5f * 2.5f) || facingDot <= 0.70f ||
                               cjJump2 >= (2.0f * 2.0f);
    const bool newShot = hardCameraCut && nowMs >= state.lastShotAtMs + 250ULL;

    if (newShot) {
        ++state.shotIndex;
        state.lastShotAtMs = nowMs;
    }

    state.camera = camera;
    state.forward = normalized;
    state.cj = cj;
    return newShot;
}

inline Vec3 CameraScreenRight(const Vec3& forward, const Vec3& fallback) {
    const float len2 = forward.x * forward.x + forward.y * forward.y;
    if (!std::isfinite(len2) || len2 < 0.04f) return fallback;
    const float invLen = 1.0f / std::sqrt(len2);
    return {forward.y * invLen, -forward.x * invLen, 0.0f};
}

inline float HeadingToward(const Vec3& from, const Vec3& to, float fallback) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy < 0.04f)
        return fallback;
    return std::atan2(-dx, dy);
}

inline PartnerSpot MakePartnerSpot(const Vec3& origin,
                                   const Vec3& right,
                                   const Vec3& forward,
                                   std::size_t rank) {
    const float side = (rank % 2 == 0) ? 1.15f : -1.15f;
    const float row = static_cast<float>(rank / 2);
    const float behind = 0.15f + row * 1.25f;
    return {
        origin.x + right.x * side - forward.x * behind,
        origin.y + right.y * side - forward.y * behind,
        origin.z,
        std::atan2(-forward.x, forward.y)
    };
}

inline PartnerSpot MakeWalkingPartnerSpot(const Vec3& origin,
                                          const Vec3& forward,
                                          std::size_t rank) {
    const float side = (rank % 2 == 0) ? 0.75f : -0.75f;
    const float row = static_cast<float>(rank / 2);
    const float behind = 1.70f + row * 1.10f;
    const Vec3 right{forward.y, -forward.x, 0.0f};
    return {
        origin.x - forward.x * behind + right.x * side,
        origin.y - forward.y * behind + right.y * side,
        origin.z,
        std::atan2(-forward.x, forward.y)
    };
}

inline bool NeedsReposition(float oldX, float oldY, float oldZ, float oldHeading,
                            float newX, float newY, float newZ, float newHeading,
                            bool hasPrevious) {
    if (!hasPrevious) return true;
    const float dx = newX - oldX;
    const float dy = newY - oldY;
    const float dz = newZ - oldZ;
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    const float headingDelta = std::remainder(newHeading - oldHeading, 6.28318530718f);
    return distanceSquared > (0.65f * 0.65f) || std::fabs(headingDelta) > 0.45f;
}

}
