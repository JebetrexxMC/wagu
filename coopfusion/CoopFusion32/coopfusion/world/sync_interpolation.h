#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace coopfusion_sync {

inline float BlendForElapsedMs(uint64_t elapsedMs, float timeConstantMs) noexcept {
    if (elapsedMs == 0) return 0.0f;
    if (!std::isfinite(timeConstantMs) || timeConstantMs <= 0.0f) return 1.0f;
    const float dt = static_cast<float>(std::min<uint64_t>(elapsedMs, 250));
    const float alpha = 1.0f - std::exp(-dt / timeConstantMs);
    return std::max(0.0f, std::min(1.0f, alpha));
}

inline uint64_t ElapsedMs(uint64_t nowMs, uint64_t& lastMs) noexcept {
    if (lastMs == 0) {
        lastMs = nowMs;
        return 16;
    }
    if (nowMs <= lastMs) return 0;
    const uint64_t elapsed = nowMs - lastMs;
    lastMs = nowMs;
    return std::min<uint64_t>(elapsed, 250);
}

struct Quaternion {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    static Quaternion FromBasis(float rx, float ry, float rz,
                                float fx, float fy, float fz,
                                float ux, float uy, float uz) noexcept {
        const float m00 = rx, m01 = fx, m02 = ux;
        const float m10 = ry, m11 = fy, m12 = uy;
        const float m20 = rz, m21 = fz, m22 = uz;
        Quaternion q{};
        const float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            const float s = std::sqrt(std::max(0.0f, trace + 1.0f)) * 2.0f;
            if (s < 1e-6f) return {};
            q.w = 0.25f * s;
            q.x = (m21 - m12) / s;
            q.y = (m02 - m20) / s;
            q.z = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(std::max(0.0f, 1.0f + m00 - m11 - m22)) * 2.0f;
            if (s < 1e-6f) return {};
            q.w = (m21 - m12) / s;
            q.x = 0.25f * s;
            q.y = (m01 + m10) / s;
            q.z = (m02 + m20) / s;
        } else if (m11 > m22) {
            const float s = std::sqrt(std::max(0.0f, 1.0f + m11 - m00 - m22)) * 2.0f;
            if (s < 1e-6f) return {};
            q.w = (m02 - m20) / s;
            q.x = (m01 + m10) / s;
            q.y = 0.25f * s;
            q.z = (m12 + m21) / s;
        } else {
            const float s = std::sqrt(std::max(0.0f, 1.0f + m22 - m00 - m11)) * 2.0f;
            if (s < 1e-6f) return {};
            q.w = (m10 - m01) / s;
            q.x = (m02 + m20) / s;
            q.y = (m12 + m21) / s;
            q.z = 0.25f * s;
        }
        return q.Normalized();
    }

    Quaternion Normalized() const noexcept {
        const float n2 = x*x + y*y + z*z + w*w;
        if (!std::isfinite(n2) || n2 < 1e-12f) return {};
        const float inv = 1.0f / std::sqrt(n2);
        return {x*inv, y*inv, z*inv, w*inv};
    }

    static Quaternion Slerp(const Quaternion& from, const Quaternion& toIn, float t) noexcept {
        t = std::max(0.0f, std::min(1.0f, t));
        const Quaternion a = from.Normalized();
        Quaternion b = toIn.Normalized();
        float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        if (dot < 0.0f) {
            dot = -dot;
            b = {-b.x, -b.y, -b.z, -b.w};
        }
        Quaternion out{};
        if (dot > 0.9995f) {
            out = {a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t,
                   a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t};
            return out.Normalized();
        }
        dot = std::max(-1.0f, std::min(1.0f, dot));
        const float theta = std::acos(dot);
        const float sinTheta = std::sin(theta);
        if (std::fabs(sinTheta) < 1e-6f) return a;
        const float wa = std::sin((1.0f-t)*theta) / sinTheta;
        const float wb = std::sin(t*theta) / sinTheta;
        out = {a.x*wa+b.x*wb, a.y*wa+b.y*wb,
               a.z*wa+b.z*wb, a.w*wa+b.w*wb};
        return out.Normalized();
    }

    void ToBasis(float& rx, float& ry, float& rz,
                 float& fx, float& fy, float& fz,
                 float& ux, float& uy, float& uz) const noexcept {
        const Quaternion q = Normalized();
        const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
        const float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
        const float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
        rx = 1.0f - 2.0f*(yy+zz); ry = 2.0f*(xy+wz); rz = 2.0f*(xz-wy);
        fx = 2.0f*(xy-wz); fy = 1.0f - 2.0f*(xx+zz); fz = 2.0f*(yz+wx);
        ux = 2.0f*(xz+wy); uy = 2.0f*(yz-wx); uz = 1.0f - 2.0f*(xx+yy);
    }
};

}
