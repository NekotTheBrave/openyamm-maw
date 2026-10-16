#include "doctest/doctest.h"

#include "game/gameplay/GameplayActorHitFeedback.h"

using OpenYAMM::Game::GameplayActorHitFeedback;

TEST_CASE("actor hit feedback fades the flash while the hitstop runs out")
{
    GameplayActorHitFeedback feedback;
    feedback.trigger(3, 0.1f, 0.5f, 0.05f);

    CHECK(feedback.hitstopActive(3));
    CHECK_FALSE(feedback.hitstopActive(4));
    CHECK(feedback.flashAmount(3) == doctest::Approx(0.5f));
    CHECK(feedback.flashAmount(4) == doctest::Approx(0.0f));

    feedback.update(0.04f);
    CHECK(feedback.hitstopActive(3));
    CHECK(feedback.flashAmount(3) == doctest::Approx(0.18f));

    feedback.update(0.02f);
    CHECK_FALSE(feedback.hitstopActive(3));
    CHECK(feedback.flashAmount(3) == doctest::Approx(0.08f));

    feedback.update(0.05f);
    CHECK(feedback.flashAmount(3) == doctest::Approx(0.0f));
}

TEST_CASE("actor hit feedback without a flash still holds the hitstop")
{
    GameplayActorHitFeedback feedback;
    feedback.trigger(2, 0.0f, 0.0f, 0.05f);

    CHECK(feedback.hitstopActive(2));
    CHECK(feedback.flashAmount(2) == doctest::Approx(0.0f));
    feedback.update(0.06f);
    CHECK_FALSE(feedback.hitstopActive(2));
}

TEST_CASE("actor hit feedback keeps the longer hitstop and restarts the flash")
{
    GameplayActorHitFeedback feedback;
    feedback.trigger(1, 0.1f, 0.7f, 0.09f);
    feedback.update(0.02f);
    feedback.trigger(1, 0.08f, 0.3f, 0.0f);

    CHECK(feedback.flashAmount(1) == doctest::Approx(0.3f));
    feedback.update(0.06f);
    CHECK(feedback.hitstopActive(1));

    feedback.clear();
    CHECK_FALSE(feedback.hitstopActive(1));
    CHECK(feedback.flashAmount(1) == doctest::Approx(0.0f));
}

TEST_CASE("actor hit feedback kicks the camera along the blow and eases it back")
{
    GameplayActorHitFeedback feedback;
    CHECK(feedback.cameraKickYawRadians() == doctest::Approx(0.0f));

    // A blow to the right and down turns the view right (negative yaw) and down.
    feedback.triggerCameraKick(1.0f, -0.5f, 0.02f);
    feedback.update(0.03f);
    CHECK(feedback.cameraKickYawRadians() == doctest::Approx(-0.02f));
    CHECK(feedback.cameraKickPitchRadians() == doctest::Approx(-0.01f));

    feedback.update(0.08f);
    CHECK(feedback.cameraKickYawRadians() == doctest::Approx(-0.02f * 0.5f * 0.5f));

    feedback.update(0.1f);
    CHECK(feedback.cameraKickYawRadians() == doctest::Approx(0.0f));
    CHECK(feedback.cameraKickPitchRadians() == doctest::Approx(0.0f));
}
