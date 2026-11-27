#include "cutscene_partner_logic.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}
}

int main() {
    const coop_cutscene::Vec3 origin{10.0f, 20.0f, 3.0f};
    const coop_cutscene::Vec3 right{1.0f, 0.0f, 0.0f};
    const coop_cutscene::Vec3 forward{0.0f, 1.0f, 0.0f};

    const auto first = coop_cutscene::MakePartnerSpot(origin, right, forward, 0);
    assert(Near(first.x, 11.15f));
    assert(Near(first.y, 19.85f));
    assert(Near(first.z, 3.0f));
    assert(Near(first.heading, 0.0f));

    const auto second = coop_cutscene::MakePartnerSpot(origin, right, forward, 1);
    assert(Near(second.x, 8.85f));
    assert(Near(second.y, 19.85f));

    const auto third = coop_cutscene::MakePartnerSpot(origin, right, forward, 2);
    assert(Near(third.x, 11.15f));
    assert(Near(third.y, 18.6f));

    const auto walkingFirst = coop_cutscene::MakeWalkingPartnerSpot(origin, forward, 0);
    const auto walkingSecond = coop_cutscene::MakeWalkingPartnerSpot(origin, forward, 1);
    const auto walkingThird = coop_cutscene::MakeWalkingPartnerSpot(origin, forward, 2);
    assert(Near(walkingFirst.x, 10.75f) && Near(walkingFirst.y, 18.3f));
    assert(Near(walkingSecond.x, 9.25f) && Near(walkingSecond.y, 18.3f));
    assert(Near(walkingThird.x, 10.75f) && Near(walkingThird.y, 17.2f));
    assert(Near(walkingFirst.heading, 0.0f));

    const coop_cutscene::Vec3 rotatedForward{-1.0f, 0.0f, 0.0f};
    const coop_cutscene::Vec3 rotatedRight{0.0f, 1.0f, 0.0f};
    const auto rotated = coop_cutscene::MakePartnerSpot(
        origin, rotatedRight, {rotatedForward.x, rotatedForward.y, 0.6f}, 0);
    assert(Near(rotated.x, 10.15f));
    assert(Near(rotated.y, 21.15f));
    assert(Near(rotated.z, origin.z));
    assert(Near(rotated.heading, 1.57079632679f));

    coop_cutscene::ShotTracker shots;
    assert(coop_cutscene::UpdateShotTracker(shots, {0, 0, 5}, {0, 1, 0}, {0, 0, 0}, 1000));
    assert(shots.shotIndex == 1);
    assert(!coop_cutscene::UpdateShotTracker(shots, {0.1f, 0, 5}, {0, 1, 0}, {0, 0, 0}, 1100));
    assert(!coop_cutscene::UpdateShotTracker(shots, {0.2f, 0, 5}, {0.05f, 0.99875f, 0}, {0, 0, 0}, 1200));
    assert(coop_cutscene::UpdateShotTracker(shots, {3.0f, 0, 5}, {0, 1, 0}, {0, 0, 0}, 1600));
    assert(shots.shotIndex == 2);
    assert(coop_cutscene::UpdateShotTracker(shots, {3.0f, 0, 5}, {1, 0, 0}, {0, 0, 0}, 1900));
    assert(shots.shotIndex == 3);
    assert(!coop_cutscene::UpdateShotTracker(shots, {8.0f, 0, 5}, {1, 0, 0}, {0, 0, 0}, 2000));
    assert(coop_cutscene::UpdateShotTracker(shots, {8.0f, 0, 5}, {1, 0, 0}, {2.5f, 0, 0}, 2300));
    assert(shots.shotIndex == 4);
    assert(!coop_cutscene::UpdateShotTracker(shots, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 2600));

    coop_cutscene::ActorMotionTracker motion;
    assert(!coop_cutscene::UpdateActorMotion(motion, {0, 0, 0}, 1000, false));
    assert(!coop_cutscene::UpdateActorMotion(motion, {0.05f, 0, 0}, 1050, false));
    assert(coop_cutscene::UpdateActorMotion(motion, {0.10f, 0, 0}, 1100, false));
    assert(Near(motion.direction.x, 1.0f) && Near(motion.direction.y, 0.0f));
    assert(coop_cutscene::UpdateActorMotion(motion, {0.10f, 0, 0}, 1150, false));
    assert(coop_cutscene::UpdateActorMotion(motion, {0.10f, 0, 0}, 1200, false));
    assert(!coop_cutscene::UpdateActorMotion(motion, {0.10f, 0, 0}, 1250, false));
    assert(!motion.moving);
    assert(!coop_cutscene::UpdateActorMotion(motion, {8.0f, 0, 0}, 1300, true));
    assert(!coop_cutscene::UpdateActorMotion(motion, {8.8f, 0, 0}, 1350, false));
    assert(!motion.moving);

    const auto screenRight = coop_cutscene::CameraScreenRight({0, 1, 0}, {0, 0, 0});
    assert(Near(screenRight.x, 1.0f) && Near(screenRight.y, 0.0f));
    const auto reverseScreenRight = coop_cutscene::CameraScreenRight({0, -1, 0}, {0, 0, 0});
    assert(Near(reverseScreenRight.x, -1.0f) && Near(reverseScreenRight.y, 0.0f));
    const auto verticalFallback = coop_cutscene::CameraScreenRight({0, 0, 1}, {0, 1, 0});
    assert(Near(verticalFallback.x, 0.0f) && Near(verticalFallback.y, 1.0f));

    assert(Near(coop_cutscene::HeadingToward({0, 0, 0}, {0, 5, 0}, 1.0f), 0.0f));
    assert(Near(coop_cutscene::HeadingToward({0, 0, 0}, {5, 0, 0}, 1.0f), -1.57079632679f));
    assert(Near(coop_cutscene::HeadingToward({0, 0, 0}, {0.1f, 0.1f, 0}, 0.75f), 0.75f));

    assert(coop_cutscene::NeedsReposition(0, 0, 0, 0, 0.1f, 0.0f, 0.0f, 0.02f, true) == false);
    assert(coop_cutscene::NeedsReposition(0, 0, 0, 0, 0.8f, 0.0f, 0.0f, 0.02f, true) == true);
    assert(coop_cutscene::NeedsReposition(0, 0, 0, 0, 0, 0, 0, 0.7f, true) == true);
    assert(coop_cutscene::NeedsReposition(0, 0, 0, 0, 0, 0, 0, 0, false) == true);

    std::puts("cutscene partner placement/shot/motion logic: 30 scenarios passed");
    return 0;
}
