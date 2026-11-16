// 4.13.64: Vice City remote players move like San Andreas MP avatars (src/mp.cpp on-foot movement).
// Before, an avatar was a street ped with a walk objective 0.5 s ahead: it stopped at every goal, walked on by itself when
// the sync was late (a "bot"), and only snapped back at 8 m. This is the San Andreas puppet logic, game-free (host test:
// test/vc_puppet_test.cpp):
//   - gait from the real speed (SA MoveStateForSpeed thresholds), walk / run / sprint goal 4 m ahead along the travel,
//     re-issued only when it changes, the goal moved > 1.5 m, the ped is about to reach it or after 1 s
//   - catch up with the legs: ahead of the network position = slower gait, behind = faster (no sliding)
//   - no new sync for 2.5 x the send interval (min 300 ms): stand still (no walking off on its own)
//   - the position error is pulled out smoothly (sideways firmly, along the way gently, never backwards = no moonwalk)
//   - snap only when 4 m off or 3 m in height (SA), heading turned smoothly, facing the aim while shooting
#pragma once
#include <math.h>

namespace VcPuppet
{
enum { GAIT_STAND = 0, GAIT_WALK = 1, GAIT_RUN = 2, GAIT_SPRINT = 3 };

struct In
{
    float cur[3];       // the avatar now
    float tgt[3];       // where the player is (network, extrapolated)
    float vel[3];       // their velocity (m/s)
    float heading;      // sent heading (degrees)
    float pedHeading;   // the avatar's heading now (degrees)
    float staleMs;      // ms since the last sync
    float intervalMs;   // their send interval (ms, 0 = unknown)
    bool  shooting;     // SYNC_SHOOTING / a shot in the last 600 ms
    float aimDir[2];    // where they shoot (0,0 = unknown)
    float dt;           // frame time (s)
    // the last walk objective
    int   lastGait;     // -1 = none
    float lastGoal[2];
    float msSinceGoal;
};
struct Out
{
    bool  snap;          // teleport to tgt
    int   gait;          // GAIT_*
    bool  issue;         // give the walk / run / sprint objective again (goal)
    bool  stop;          // clear the objective (stand)
    float goal[2];
    float step[3];       // position correction this frame
    bool  turn;          // set heading
    float newHeading;    // degrees
};

inline float Clamp01(float k) { return k > 1.0f ? 1.0f : (k < 0.0f ? 0.0f : k); }
inline float Dist2(const float* a, const float* b) { const float x = a[0] - b[0], y = a[1] - b[1]; return sqrtf(x * x + y * y); }
inline float LerpAngle(float a, float b, float k)
{
    float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    float r = a + d * Clamp01(k);
    if(r < 0) r += 360.0f;
    if(r >= 360.0f) r -= 360.0f;
    return r;
}
inline float TravelHeading(float vx, float vy) { float h = atan2f(-vx, vy) * 57.2957795f; return h < 0 ? h + 360.0f : h; }
// San Andreas MoveStateForSpeed: idle < 0.6, walk < 3.2, run < 7, sprint
inline int GaitForSpeed(float v) { return v < 0.6f ? GAIT_STAND : v < 3.2f ? GAIT_WALK : v < 7.0f ? GAIT_RUN : GAIT_SPRINT; }

inline Out Step(const In& in)
{
    Out o; o.snap = false; o.gait = GAIT_STAND; o.issue = false; o.stop = false; o.goal[0] = o.goal[1] = 0;
    o.step[0] = o.step[1] = o.step[2] = 0; o.turn = false; o.newHeading = in.pedHeading;
    const float speed = sqrtf(in.vel[0] * in.vel[0] + in.vel[1] * in.vel[1]);
    const float d = Dist2(in.cur, in.tgt), dz = in.tgt[2] - in.cur[2];
    if(d > 4.0f || fabsf(dz) > 3.0f) { o.snap = true; o.stop = in.lastGait != 0; return o; }

    float staleLimit = in.intervalMs * 2.5f; if(staleLimit < 300.0f) staleLimit = 300.0f;
    const bool frozen = in.staleMs > staleLimit;
    int g = GaitForSpeed(speed);
    if(frozen || speed < 0.12f) g = GAIT_STAND;
    else if(g == GAIT_STAND) g = GAIT_WALK;   // any real movement walks (a slow push used to stand + slide)

    float dx, dy;
    if(speed > 0.3f) { dx = in.vel[0] / speed; dy = in.vel[1] / speed; }
    else { const float hr = in.heading * 0.0174533f; dx = -sinf(hr); dy = cosf(hr); }
    if(g != GAIT_STAND && !in.shooting)
    {   // catch up with the legs: ahead -> slower gait, behind -> faster
        const float along = (in.tgt[0] - in.cur[0]) * dx + (in.tgt[1] - in.cur[1]) * dy;
        if(along < -1.0f) g = g >= GAIT_RUN ? GAIT_WALK : GAIT_STAND;
        else if(along > 2.0f) g = g == GAIT_WALK ? GAIT_RUN : GAIT_SPRINT;
    }
    if(in.shooting) g = speed > 0.6f && !frozen ? GAIT_WALK : GAIT_STAND;   // gun up: walk while shooting, never run off
    o.gait = g;
    if(g == GAIT_STAND) o.stop = in.lastGait != 0;
    else
    {
        o.goal[0] = in.tgt[0] + dx * 4.0f; o.goal[1] = in.tgt[1] + dy * 4.0f;
        o.issue = g != in.lastGait || Dist2(o.goal, in.lastGoal) > 1.5f || Dist2(in.cur, in.lastGoal) < 2.5f || in.msSinceGoal > 1000.0f;
    }
    // the remaining position error, smoothly
    if(d > 0.05f || fabsf(dz) > 0.35f)
    {
        const float ex = in.tgt[0] - in.cur[0], ey = in.tgt[1] - in.cur[1];
        if(g != GAIT_STAND && speed > 0.3f && !in.shooting)
        {
            const float fx = in.vel[0] / speed, fy = in.vel[1] / speed;
            const float al = ex * fx + ey * fy, lx = ex - al * fx, ly = ey - al * fy;
            const float kl = Clamp01(in.dt * 6.0f);
            float ka = fabsf(al) > 0.3f ? in.dt * 3.0f : 0.0f;
            if(al < 0.0f) ka = al < -1.5f ? in.dt * 3.0f : in.dt * 0.6f;   // no backwards slide (moonwalk)
            ka = Clamp01(ka);
            o.step[0] = lx * kl + fx * al * ka; o.step[1] = ly * kl + fy * al * ka;
        }
        else { const float k = Clamp01(in.dt * 8.0f); o.step[0] = ex * k; o.step[1] = ey * k; }
        o.step[2] = fabsf(dz) > 0.35f ? dz * Clamp01(in.dt * 6.0f) : 0.0f;
    }
    // heading: the aim while shooting, the sent heading while standing (smooth), the walk task turns a moving ped
    float want = -1;
    if(in.shooting && (in.aimDir[0] != 0 || in.aimDir[1] != 0)) want = TravelHeading(in.aimDir[0], in.aimDir[1]);
    else if(g == GAIT_STAND) want = in.heading;
    if(want >= 0)
    {
        const float hd = fmodf(want - in.pedHeading + 540.0f, 360.0f) - 180.0f;
        if(fabsf(hd) > 3.0f || in.shooting) { o.turn = true; o.newHeading = in.shooting ? want : LerpAngle(in.pedHeading, want, in.dt * 10.0f); }
    }
    return o;
}
}   // namespace VcPuppet
