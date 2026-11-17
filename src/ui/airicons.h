#pragma once
// 5.00.04: aircraft icons (helicopter, fighter jet, plane), side view with the nose to the right.
//  - kill feed: a player who died flying shows the aircraft instead of the weapon, with what got them
//    (shot = bullet sparks, rocket / explosion = fire burst, fire = flames, crash = nose down + smoke + impact)
//  - hexagon HUD speedometer: the aircraft you fly instead of the car
#include "../../imgui/imgui.h"
#include <math.h>
#include <stdint.h>

enum { MP_AIR_NONE = 0, MP_AIR_HELI, MP_AIR_JET, MP_AIR_PLANE };

// GTA San Andreas vehicle models
inline int MP_AirKind(int model)
{
    switch(model)
    {
        case 417: case 425: case 447: case 465: case 469: case 487: case 488: case 497: case 501: case 548: case 563:
            return MP_AIR_HELI;    // Leviathan, Hunter, Seasparrow, RC Raider, Sparrow, Maverick, News Chopper, Police Maverick, RC Goblin, Cargobob, Raindance
        case 520:
            return MP_AIR_JET;     // Hydra (fighter jet)
        case 460: case 464: case 476: case 511: case 512: case 513: case 519: case 553: case 577: case 592: case 593:
            return MP_AIR_PLANE;   // Skimmer, RC Baron, Rustler, Beagle, Cropduster, Stuntplane, Shamal, Nevada, AT-400, Andromada, Dodo
    }
    return MP_AIR_NONE;
}

// Kill feed causes on an aircraft
enum { MP_AIRHIT_CRASH = 0, MP_AIRHIT_SHOT, MP_AIRHIT_BLAST, MP_AIRHIT_FIRE };
inline int MP_AirHitKind(int weapon)
{
    if(weapon == 37 || weapon == 18) return MP_AIRHIT_FIRE;                                              // flamethrower, molotov
    if(weapon == 16 || weapon == 35 || weapon == 36 || weapon == 39 || weapon == 51) return MP_AIRHIT_BLAST;   // grenade, rockets, satchel, explosion
    if((weapon >= 22 && weapon <= 34) || weapon == 38) return MP_AIRHIT_SHOT;                            // guns + minigun
    return MP_AIRHIT_CRASH;                                                                              // ram / heli blades / fall / world / unknown
}

struct MPAirPen
{
    ImDrawList* dl; ImVec2 c; float r; float ca, sa;   // centre, unit size, rotation
    ImU32 fill, line; float t;                         // outline style (line != 0) or solid (line = 0)
    ImVec2 P(float x, float y) const { const float rx = x * ca - y * sa, ry = x * sa + y * ca; return ImVec2(c.x + rx * r, c.y + ry * r); }
    void Poly(const float* xy, int n) const
    {
        ImVec2 p[24]; if(n > 24) n = 24;
        for(int i = 0; i < n; ++i) p[i] = P(xy[i * 2], xy[i * 2 + 1]);
        dl->AddConvexPolyFilled(p, n, fill);
        if(line) dl->AddPolyline(p, n, line, ImDrawFlags_Closed, t);
    }
    void Oval(float cx, float cy, float rx, float ry) const
    {
        ImVec2 p[24];
        for(int i = 0; i < 24; ++i) { const float a = i * 6.2831853f / 24; p[i] = P(cx + cosf(a) * rx, cy + sinf(a) * ry); }
        dl->AddConvexPolyFilled(p, 24, fill);
        if(line) dl->AddPolyline(p, 24, line, ImDrawFlags_Closed, t);
    }
    void Bar(float x0, float y0, float x1, float y1, float w) const
    {   // a thick line (rotor, skid, propeller)
        dl->AddLine(P(x0, y0), P(x1, y1), line ? line : fill, fmaxf(1.0f, w * r));
    }
    void Glass(float cx, float cy, float rx, float ry, ImU32 col) const
    {
        ImVec2 p[16];
        for(int i = 0; i < 16; ++i) { const float a = i * 6.2831853f / 16; p[i] = P(cx + cosf(a) * rx, cy + sinf(a) * ry); }
        dl->AddConvexPolyFilled(p, 16, col);
    }
};

inline void MP_DrawAircraft(ImDrawList* dl, ImVec2 c, float r, int kind, ImU32 fill, ImU32 line, float angle = 0.0f)
{
    MPAirPen pen{ dl, c, r, cosf(angle), sinf(angle), fill, line, fmaxf(1.2f, r * 0.10f) };
    const ImU32 glass = line ? line : IM_COL32(40, 60, 80, 255);
    if(kind == MP_AIR_HELI)
    {
        // tail boom + fin + tail rotor
        { const float b[] = { -0.30f, -0.12f, -0.98f, -0.10f, -0.98f, 0.00f, -0.30f, 0.10f }; pen.Poly(b, 4); }
        { const float f[] = { -0.98f, -0.10f, -0.86f, -0.10f, -0.94f, -0.38f, -1.02f, -0.38f }; pen.Poly(f, 4); }
        pen.Bar(-0.95f, -0.50f, -0.95f, -0.18f, 0.07f);
        // body + cockpit glass
        pen.Oval(0.08f, 0.02f, 0.52f, 0.30f);
        pen.Glass(0.32f, -0.04f, 0.20f, 0.15f, glass);
        // main rotor + mast
        pen.Bar(0.02f, -0.28f, 0.02f, -0.44f, 0.08f);
        pen.Bar(-0.88f, -0.46f, 0.92f, -0.46f, 0.08f);
        // skids
        pen.Bar(-0.18f, 0.30f, -0.22f, 0.50f, 0.06f); pen.Bar(0.30f, 0.28f, 0.34f, 0.50f, 0.06f);
        pen.Bar(-0.45f, 0.50f, 0.62f, 0.50f, 0.07f);
    }
    else if(kind == MP_AIR_JET)
    {
        // fuselage (Hydra): pointed nose, flat belly, engine at the back
        { const float f[] = { 1.00f, 0.06f, 0.55f, -0.10f, -0.20f, -0.14f, -0.92f, -0.10f, -0.95f, 0.12f, -0.20f, 0.16f, 0.55f, 0.14f }; pen.Poly(f, 7); }
        // tail fin
        { const float t[] = { -0.48f, -0.12f, -0.72f, -0.62f, -0.90f, -0.62f, -0.88f, -0.10f }; pen.Poly(t, 4); }
        // delta wing (seen from the side, below)
        { const float w[] = { 0.25f, 0.14f, -0.55f, 0.14f, -0.70f, 0.42f, -0.35f, 0.42f }; pen.Poly(w, 4); }
        // canopy
        pen.Glass(0.42f, -0.13f, 0.20f, 0.08f, glass);
        // afterburner
        { const ImU32 o = IM_COL32(255, 150, 30, 255); dl->AddTriangleFilled(pen.P(-0.95f, -0.06f), pen.P(-0.95f, 0.10f), pen.P(-1.18f, 0.02f), o); }
    }
    else   // MP_AIR_PLANE
    {
        // fuselage
        { const float f[] = { 0.82f, -0.10f, 0.90f, 0.02f, 0.82f, 0.14f, -0.10f, 0.16f, -0.92f, 0.04f, -0.92f, -0.06f, -0.10f, -0.14f }; pen.Poly(f, 7); }
        // tail fin + elevator
        { const float t[] = { -0.62f, -0.08f, -0.80f, -0.48f, -0.94f, -0.48f, -0.92f, -0.06f }; pen.Poly(t, 4); }
        // straight wing (top) and cockpit
        { const float w[] = { 0.30f, -0.12f, 0.10f, -0.22f, -0.32f, -0.22f, -0.22f, -0.12f }; pen.Poly(w, 4); }
        pen.Glass(0.42f, -0.06f, 0.16f, 0.07f, glass);
        // propeller + wheels
        pen.Bar(0.93f, -0.32f, 0.93f, 0.36f, 0.06f);
        pen.Bar(0.25f, 0.16f, 0.30f, 0.36f, 0.05f);
        dl->AddCircleFilled(pen.P(0.30f, 0.40f), 0.07f * r, line ? line : fill, 10);
    }
}

// The kill feed icon: the aircraft + how it went down (s = the icon box size)
inline void MP_DrawAirKillIcon(ImDrawList* dl, ImVec2 c, float s, int kind, int weapon)
{
    const ImU32 W = IM_COL32(255, 255, 255, 255);
    const int hit = MP_AirHitKind(weapon);
    const float r = s * 0.40f;
    if(hit == MP_AIRHIT_CRASH)
    {   // nose down into the ground: smoke trail behind, an impact burst at the nose
        const float ang = 0.55f;
        ImVec2 ac(c.x + s * 0.02f, c.y + s * 0.02f);
        for(int i = 0; i < 3; ++i)   // smoke behind the tail, inside the box
            dl->AddCircleFilled(ImVec2(ac.x - s * (0.26f + i * 0.06f), ac.y - s * (0.14f + i * 0.07f)), s * (0.045f + i * 0.012f), IM_COL32(170, 170, 170, 210 - i * 50), 10);
        MP_DrawAircraft(dl, ac, r * 0.85f, kind, W, 0, ang);
        const ImVec2 hitp(ac.x + cosf(ang) * r * 0.85f, ac.y + sinf(ang) * r * 0.85f);
        const ImU32 O = IM_COL32(255, 150, 30, 255);
        for(int i = 0; i < 7; ++i) { const float a = 3.6f + i * 0.40f; dl->AddLine(ImVec2(hitp.x + cosf(a) * s * 0.03f, hitp.y + sinf(a) * s * 0.03f), ImVec2(hitp.x + cosf(a) * s * 0.13f, hitp.y + sinf(a) * s * 0.13f), O, fmaxf(1.0f, s * 0.035f)); }
        dl->AddLine(ImVec2(c.x - s * 0.46f, c.y + s * 0.40f), ImVec2(c.x + s * 0.46f, c.y + s * 0.40f), W, fmaxf(1.0f, s * 0.03f));   // the ground
        return;
    }
    MP_DrawAircraft(dl, c, r, kind, W, 0, 0.0f);
    if(hit == MP_AIRHIT_SHOT)
    {   // tracer lines coming in from the front-low side + yellow sparks on the body
        const ImU32 Y = IM_COL32(255, 220, 60, 255);
        for(int i = 0; i < 3; ++i)
        {
            ImVec2 p(c.x + s * (-0.12f + i * 0.14f), c.y + s * (0.02f - i * 0.03f));
            dl->AddLine(ImVec2(p.x + s * 0.20f, p.y + s * 0.28f), p, IM_COL32(255, 230, 120, 200), fmaxf(1.0f, s * 0.02f));
            for(int k = 0; k < 4; ++k) { const float a = k * 1.5708f + 0.4f; dl->AddLine(p, ImVec2(p.x + cosf(a) * s * 0.06f, p.y + sinf(a) * s * 0.06f), Y, fmaxf(1.0f, s * 0.025f)); }
            dl->AddCircleFilled(p, s * 0.025f, IM_COL32(20, 20, 20, 255), 8);
        }
    }
    else if(hit == MP_AIRHIT_BLAST)
    {   // a fire ball on the tail / body
        const ImVec2 b(c.x - s * 0.16f, c.y + s * 0.0f);
        dl->AddCircleFilled(b, s * 0.13f, IM_COL32(255, 120, 20, 235), 16);
        dl->AddCircleFilled(b, s * 0.075f, IM_COL32(255, 215, 70, 255), 14);
        for(int i = 0; i < 10; ++i) { const float a = i * 0.628f; dl->AddLine(ImVec2(b.x + cosf(a) * s * 0.13f, b.y + sinf(a) * s * 0.13f), ImVec2(b.x + cosf(a) * s * 0.21f, b.y + sinf(a) * s * 0.21f), IM_COL32(255, 150, 30, 255), fmaxf(1.0f, s * 0.03f)); }
    }
    else   // MP_AIRHIT_FIRE: flames on top of the body
    {
        for(int i = 0; i < 3; ++i)
        {
            const float x = c.x + s * (-0.18f + i * 0.14f), y = c.y - s * 0.04f, h = s * (0.20f + (i == 1 ? 0.08f : 0.0f));
            dl->AddTriangleFilled(ImVec2(x - s * 0.07f, y), ImVec2(x + s * 0.07f, y), ImVec2(x, y - h), IM_COL32(255, 130, 20, 240));
            dl->AddTriangleFilled(ImVec2(x - s * 0.035f, y), ImVec2(x + s * 0.035f, y), ImVec2(x, y - h * 0.55f), IM_COL32(255, 220, 70, 255));
        }
    }
}
