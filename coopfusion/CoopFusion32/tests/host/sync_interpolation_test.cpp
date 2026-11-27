#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../../coopfusion/world/sync_interpolation.h"

static void Check(bool condition, const char* name, int& failed) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failed;
    }
}
static bool Near(float a, float b, float eps=1e-4f) { return std::fabs(a-b) <= eps; }

int main() {
    int failed = 0;
    const float oneSecond = coopfusion_sync::BlendForElapsedMs(100, 50.0f);
    const float twoHalfSteps = 1.0f - std::pow(1.0f - coopfusion_sync::BlendForElapsedMs(50, 50.0f), 2.0f);
    Check(Near(oneSecond, twoHalfSteps), "elapsed-time blend composes across frame rates", failed);
    Check(coopfusion_sync::BlendForElapsedMs(0, 50.0f) == 0.0f, "zero elapsed time does not advance smoothing", failed);
    Check(coopfusion_sync::BlendForElapsedMs(5000, 50.0f) <= 1.0f, "large frame gap is bounded", failed);

    uint64_t last = 100;
    Check(coopfusion_sync::ElapsedMs(100, last) == 0 && last == 100,
          "duplicate same-millisecond update does not invent a frame delta", failed);
    Check(coopfusion_sync::ElapsedMs(116, last) == 16 && last == 116,
          "elapsed clock advances from actual time", failed);

    const auto identity = coopfusion_sync::Quaternion::FromBasis(
        1,0,0, 0,1,0, 0,0,1);
    float rx=0,ry=0,rz=0, fx=0,fy=0,fz=0, ux=0,uy=0,uz=0;
    identity.ToBasis(rx,ry,rz,fx,fy,fz,ux,uy,uz);
    Check(Near(rx,1)&&Near(ry,0)&&Near(rz,0)&&Near(fx,0)&&Near(fy,1)&&Near(fz,0)&&Near(ux,0)&&Near(uy,0)&&Near(uz,1),
          "identity basis survives quaternion conversion", failed);

    const float half = std::sqrt(0.5f);
    const auto quarterTurn = coopfusion_sync::Quaternion::FromBasis(
        0,1,0, -1,0,0, 0,0,1);
    const auto halfway = coopfusion_sync::Quaternion::Slerp(identity, quarterTurn, 0.5f);
    halfway.ToBasis(rx,ry,rz,fx,fy,fz,ux,uy,uz);
    Check(Near(rx,half,2e-4f)&&Near(ry,half,2e-4f)&&Near(fx,-half,2e-4f)&&Near(fy,half,2e-4f)&&Near(uz,1,2e-4f),
          "vehicle rotation follows shortest-path quaternion slerp", failed);

    const auto sameOpposite = coopfusion_sync::Quaternion::Slerp(identity,
        {-identity.x,-identity.y,-identity.z,-identity.w}, 0.5f);
    Check(Near(std::fabs(sameOpposite.w),1.0f), "quaternion sign ambiguity does not spin a full turn", failed);

    if (failed) return 1;
    std::puts("sync interpolation: 8 checks passed");
    return 0;
}
