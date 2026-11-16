// 4.13.64: Vice City avatars move like San Andreas MP avatars (src/vc/vc_puppet.h)
#include "../src/vc/vc_puppet.h"
#include <stdio.h>
#include <string.h>
static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while(0)
static VcPuppet::In Base()
{
    VcPuppet::In i; memset(&i, 0, sizeof(i));
    i.dt = 1.0f / 30.0f; i.intervalMs = 100; i.lastGait = -1; return i;
}
int main()
{
    using namespace VcPuppet;
    {   // standing player: stand, smooth heading turn (no snap)
        In i = Base(); i.heading = 90; i.pedHeading = 0;
        Out o = Step(i);
        CHECK(!o.snap && o.gait == GAIT_STAND && o.stop && o.turn && o.newHeading > 0 && o.newHeading < 90);
    }
    {   // walking north at 1.5 m/s: walk goal 4 m ahead of the player
        In i = Base(); i.vel[1] = 1.5f; i.tgt[1] = 0.2f;
        Out o = Step(i);
        CHECK(o.gait == GAIT_WALK && o.issue && fabsf(o.goal[0]) < 1e-3f && fabsf(o.goal[1] - 4.2f) < 1e-3f);
        // same goal next frame: not re-issued (the walk cycle doesn't restart)
        i.lastGait = GAIT_WALK; i.lastGoal[0] = o.goal[0]; i.lastGoal[1] = o.goal[1]; i.msSinceGoal = 100;
        Out o2 = Step(i); CHECK(!o2.issue);
        // about to reach the goal: re-issued (no stop-and-go)
        i.cur[1] = 2.5f; i.tgt[1] = 2.6f; Out o3 = Step(i); CHECK(o3.issue);
    }
    {   // run / sprint thresholds like San Andreas
        In i = Base(); i.vel[0] = 5.0f; CHECK(Step(i).gait == GAIT_RUN);
        i.vel[0] = 7.5f; CHECK(Step(i).gait == GAIT_SPRINT);
    }
    {   // behind the player by 3 m: faster gait; ahead by 2 m: slower
        In i = Base(); i.vel[1] = 1.5f; i.tgt[1] = 3.0f; CHECK(Step(i).gait == GAIT_RUN);
        i.tgt[1] = -2.0f; i.vel[1] = 4.0f; CHECK(Step(i).gait == GAIT_WALK);
    }
    {   // the sync is late (lag): stand still, no walking off like a bot
        In i = Base(); i.vel[1] = 4.0f; i.staleMs = 400; i.lastGait = GAIT_RUN;
        Out o = Step(i); CHECK(o.gait == GAIT_STAND && o.stop && !o.issue);
        i.intervalMs = 300; i.staleMs = 400; CHECK(Step(i).gait != GAIT_STAND);   // slow sender: 2.5 x interval
    }
    {   // 4 m off or 3 m height: snap (was 8 m); 3 m off: smooth pull
        In i = Base(); i.tgt[0] = 4.5f; CHECK(Step(i).snap);
        i.tgt[0] = 0; i.tgt[2] = 3.5f; CHECK(Step(i).snap);
        i.tgt[2] = 0; i.tgt[0] = 3.0f; Out o = Step(i); CHECK(!o.snap && o.step[0] > 0 && o.step[0] < 3.0f);
    }
    {   // running ahead of the network position: never pulled backwards hard (moonwalk)
        In i = Base(); i.vel[1] = 5.0f; i.cur[1] = 1.0f; i.tgt[1] = 0.0f;
        Out o = Step(i); CHECK(o.step[1] < 0 && o.step[1] > -0.05f);
    }
    {   // shooting: face the aim, no running off
        In i = Base(); i.shooting = true; i.aimDir[0] = 1.0f; i.vel[1] = 6.0f;
        Out o = Step(i); CHECK(o.turn && fabsf(o.newHeading - 270.0f) < 0.5f && o.gait == GAIT_WALK);
    }
    printf(fails ? "vc_puppet_test: %d failed\n" : "vc_puppet_test: all passed\n", fails);
    return fails ? 1 : 0;
}
