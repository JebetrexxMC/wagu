// 5.01.03: moonwalk fix - running back towards your camera (stick pulled down) must turn the avatar round on the
// other phones too (src/game/locofacing.h, src/vc/vc_puppet.h). g++ -std=c++17 -I src test/facing_test.cpp
#include "../src/game/locofacing.h"
#include "../src/vc/vc_puppet.h"
#include <stdio.h>
#include <string.h>
static int fails = 0;
static void near(const char* what, float got, float want, float tol = 2.0f)
{
    float d = fabsf(MP_AngleDiff(got, want));
    if(d > tol) { printf("FAIL %s: %.1f (want %.1f)\n", what, got, want); ++fails; } else printf("ok   %s: %.1f\n", what, got);
}
int main()
{
    // stick -> world, camera looking north (heading 0)
    near("stick up = where the camera looks", MP_StickHeading(0, 0, -127), 0);
    near("stick down = back towards the camera", MP_StickHeading(0, 0, 127), 180);
    near("stick right = east (270)", MP_StickHeading(0, 127, 0), 270);
    near("stick left = west (90)", MP_StickHeading(0, -127, 0), 90);
    near("camera east, stick down = west", MP_StickHeading(270, 0, 127), 90);
    if(MP_StickHeading(0, 10, 10) >= 0) { printf("FAIL tiny stick counted\n"); ++fails; }

    // sender: body still faces the camera's way (0) but the player pulled the stick back and starts moving south
    near("sender, turning round (slow, stick back)", MP_SendHeading(0, 0, -0.5f, MP_StickHeading(0, 0, 127), false), 180);
    near("sender, running back (travel south)", MP_SendHeading(0, 0, -5.0f, -1, false), 180);
    near("sender, running forward keeps body heading", MP_SendHeading(5, -0.3f, 5.0f, MP_StickHeading(0, 0, -127), false), 5);
    near("sender, aiming / strafing back keeps body", MP_SendHeading(0, 0, -2.0f, MP_StickHeading(0, 0, 127), true), 0);
    near("sender, standing keeps body", MP_SendHeading(42, 0, 0, MP_StickHeading(0, 0, 127), false), 42);

    // receiver (San Andreas paths): old heading arrives, body moves the other way
    near("receiver, old heading + moving south", MP_LocoFacing(0, 0, -4.0f, false), 180);
    near("receiver, lean into a curve (20 deg) kept", MP_LocoFacing(20, 0, 4.0f, false), 20);
    near("receiver, aiming keeps sent", MP_LocoFacing(0, 0, -4.0f, true), 0);

    // Vice City puppet: avatar faces north, player runs south (old heading 0 in the sync): turned within 0.15 s,
    // and never slides backwards against its facing after that
    {
        VcPuppet::In in; memset(&in, 0, sizeof(in));
        float cur[3] = { 0, 0, 10 }, ped = 0;
        float worst = 0;
        for(int f = 0; f < 60; ++f)   // 1 s at 60 fps
        {
            float tgt[3] = { 0, -4.0f * (f / 60.0f) - 0.3f, 10 };
            memcpy(in.cur, cur, 12); memcpy(in.tgt, tgt, 12);
            in.vel[0] = 0; in.vel[1] = -4.0f; in.vel[2] = 0;
            in.heading = 0;           // stale sent heading (older sender)
            in.pedHeading = ped; in.dt = 1 / 60.0f; in.intervalMs = 100; in.staleMs = 20;
            VcPuppet::Out o = VcPuppet::Step(in);
            if(o.turn) ped = o.newHeading;
            for(int c = 0; c < 3; ++c) cur[c] += o.step[c];
            if(f >= 9) { float d = fabsf(MP_AngleDiff(ped, 180)); if(d > worst) worst = d; }
        }
        if(worst > 30) { printf("FAIL vc puppet still faces away %.1f deg after 0.15 s\n", worst); ++fails; }
        else printf("ok   vc puppet turned round (max %.1f deg off after 0.15 s)\n", worst);
    }
    printf(fails ? "facing_test: %d failed\n" : "facing_test: all passed\n", fails);
    return fails ? 1 : 0;
}
