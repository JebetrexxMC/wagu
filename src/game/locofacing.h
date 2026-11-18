// MP 5.01.03: which way a remote player's body faces while it moves on foot (San Andreas + Vice City).
//
// "Moonwalk" bug: a player runs back towards their own camera (stick pulled down), the body on their phone turns
// round and runs, but on the other phones the avatar kept the old facing and slid backwards. The fix of 4.13.19 only
// covered mirrored walk / run anims of the default walk styles and only when the aim flag was off; with a weapon walk
// style, a short run-stop / turn anim in the blend, the game's own walk task (no anim mirror, PC cross-play players,
// old builds) or the Vice City puppet the avatar still moonwalked.
//
// Rule (GTA SA / VC on foot): a ped only moves backwards or sideways while it aims (strafe) or shoots. So when the
// player is not aiming / shooting and really moves (>= 0.8 m/s), the body faces the direction of travel; small
// differences (< 30 deg) keep the sent heading (leaning into a curve). Pure functions: test/facing_test.cpp.
#pragma once
#include <math.h>

inline float MP_TravelHeading(float vx, float vy)
{
    float h = atan2f(-vx, vy) * 57.2957795f;   // GTA heading: 0 = north (+y), 90 = west (-x)
    return h < 0 ? h + 360.0f : h;
}
inline float MP_AngleDiff(float a, float b) { return fmodf(a - b + 540.0f, 360.0f) - 180.0f; }   // a - b in -180..180
inline float MP_LerpHeading(float from, float to, float k)
{
    if(k > 1.0f) k = 1.0f;
    if(k < 0.0f) k = 0.0f;
    float h = from + MP_AngleDiff(to, from) * k;
    while(h < 0) h += 360.0f;
    while(h >= 360.0f) h -= 360.0f;
    return h;
}
// the heading the avatar should have: the travel direction while moving without aiming, else the sent one
inline float MP_LocoFacing(float sent, float vx, float vy, bool aimingOrShooting)
{
    const float speed = sqrtf(vx * vx + vy * vy);
    if(aimingOrShooting || speed < 0.8f) return sent;
    const float th = MP_TravelHeading(vx, vy);
    return fabsf(MP_AngleDiff(th, sent)) > 30.0f ? th : sent;
}
// how fast the avatar turns to it (per second): a reversal (> 90 deg off) turns at once like the player's own
// 180 turn, so it never slides backwards for a moment
inline float MP_TurnRate(float cur, float want) { return fabsf(MP_AngleDiff(want, cur)) > 90.0f ? 30.0f : 12.0f; }

// 5.01.03 (sender): the way the player's stick points in the world. GTA's stick is camera-relative: pulling it
// down = run back towards the camera. camHeading = where the camera looks; sx = right (+), sy = down (+) (-128..128).
// Returns -1 when the stick is not really pushed.
inline float MP_StickHeading(float camHeading, int sx, int sy)
{
    if(sx * sx + sy * sy < 40 * 40) return -1.0f;
    const float hr = camHeading * 0.0174533f;
    const float fx = -sinf(hr), fy = cosf(hr);    // camera forward
    const float rx = fy, ry = -fx;                // camera right
    const float wx = fx * (float)(-sy) + rx * (float)sx, wy = fy * (float)(-sy) + ry * (float)sx;
    return MP_TravelHeading(wx, wy);
}
// the heading the sender puts in its sync: while running / walking without aiming, where the body goes - the
// direction of travel, or the stick direction when the player just pulled the stick round (turning: the body on
// this phone turns at once, the other phones must turn with it and never see the old facing slide backwards)
inline float MP_SendHeading(float body, float vx, float vy, float stickHeading, bool aimingOrShooting)
{
    if(aimingOrShooting) return body;
    float h = MP_LocoFacing(body, vx, vy, false);
    const float speed = sqrtf(vx * vx + vy * vy);
    if(stickHeading >= 0 && speed >= 0.3f && fabsf(MP_AngleDiff(stickHeading, h)) > 90.0f) h = stickHeading;
    return h;
}
