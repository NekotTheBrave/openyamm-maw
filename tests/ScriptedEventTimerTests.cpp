#include "doctest/doctest.h"

#include "game/events/ScriptedEventTimer.h"

#include <cstdint>
#include <vector>

namespace
{
OpenYAMM::Game::ScriptedEventTimerDefinition makeLegacyTimer(
    OpenYAMM::Game::ScriptedEventTimerScheduleKind scheduleKind,
    uint8_t triggerStep = 0)
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition = {};
    definition.scope = OpenYAMM::Game::ScriptedEventScope::Map;
    definition.origin = OpenYAMM::Game::ScriptedEventTimerOrigin::Legacy;
    definition.triggerKind = OpenYAMM::Game::ScriptedEventTimerTriggerKind::LongTimer;
    definition.scheduleKind = scheduleKind;
    definition.eventId = 130;
    definition.sourceEventId = 130;
    definition.triggerStep = triggerStep;
    definition.repeating = true;
    return definition;
}
}

TEST_CASE("legacy daily refill timer fires once on first visit and restarts its full period")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Daily);
    definition.startSecond = 1;

    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 540.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(540.0));
    CHECK_EQ(states[0].eligibleGameMinutes, doctest::Approx(540.5));
    CHECK(states[0].initialCalendarFirePending);

    int executionCount = 0;
    CHECK_FALSE(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        540.49,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 0);

    CHECK(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        540.5,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 1);
    CHECK_FALSE(states[0].initialCalendarFirePending);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(1980.5));
}

TEST_CASE("legacy exact daily timer waits for S'ton's ten o'clock alarm")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Daily);
    definition.triggerKind = OpenYAMM::Game::ScriptedEventTimerTriggerKind::Timer;
    definition.eventId = 500;
    definition.sourceEventId = 500;
    definition.startHour = 10;

    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 540.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(600.0));
    CHECK_FALSE(states[0].initialCalendarFirePending);

    int executionCount = 0;
    CHECK_FALSE(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        599.99,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 0);

    CHECK(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        600.0,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 1);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(2040.0));
}

TEST_CASE("legacy exact daily timer advances past an alarm that already elapsed")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Daily);
    definition.triggerKind = OpenYAMM::Game::ScriptedEventTimerTriggerKind::Timer;
    definition.startHour = 10;

    const std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 650.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(2040.0));
}

TEST_CASE("legacy normal calendar timer waits a complete period on first visit")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Weekly);
    definition.triggerKind = OpenYAMM::Game::ScriptedEventTimerTriggerKind::Timer;

    const std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 100.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(10180.0));
    CHECK_FALSE(states[0].initialCalendarFirePending);
}

TEST_CASE("legacy exact daily timer uses the next clock alarm after the current time")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Daily);
    definition.triggerKind = OpenYAMM::Game::ScriptedEventTimerTriggerKind::Timer;
    definition.startHour = 12;

    const int64_t morningVisit = OpenYAMM::Game::legacyTimerTicksFromGameMinutes(600.0);
    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 650.0, morningVisit, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(720.0));

    const int64_t afternoonVisit = OpenYAMM::Game::legacyTimerTicksFromGameMinutes(800.0);
    states = OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 850.0, afternoonVisit, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(2160.0));
}

TEST_CASE("legacy interval timer starts after its complete half-minute interval")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Interval);
    definition.intervalHalfMinutes = 513;

    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 100.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(356.5));

    int executionCount = 0;
    CHECK(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        356.5,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 1);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(613.0));
}

TEST_CASE("MM8 Abandoned Temple interval timer advances from OE combat turn ticks")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Interval);
    definition.intervalHalfMinutes = 5;

    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 100.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);

    const double turnGameMinutes = OpenYAMM::Game::legacyTimerGameMinutesFromTicks(213);
    int executionCount = 0;

    for (int turn = 1; turn <= 3; ++turn)
    {
        CHECK_FALSE(OpenYAMM::Game::updateScriptedEventTimers(
            states,
            100.0 + static_cast<double>(turn) * turnGameMinutes,
            [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
            {
                ++executionCount;
                return true;
            }));
    }

    CHECK(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        100.0 + 4.0 * turnGameMinutes,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 1);
}

TEST_CASE("legacy calendar timer skips missed periods without a catch-up storm")
{
    OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Weekly);
    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 10.0, 0, false);

    int executionCount = 0;
    CHECK(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        100000.0,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 1);
    CHECK_GT(states[0].nextAlarmGameMinutes, 100000.0);

    CHECK_FALSE(OpenYAMM::Game::updateScriptedEventTimers(
        states,
        100000.0,
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        }));
    CHECK_EQ(executionCount, 1);
}

TEST_CASE("legacy timer identity keeps duplicate opcodes at different trigger steps")
{
    const OpenYAMM::Game::ScriptedEventTimerDefinition first =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Yearly, 25);
    const OpenYAMM::Game::ScriptedEventTimerDefinition second =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Yearly, 26);

    CHECK_FALSE(OpenYAMM::Game::sameScriptedEventTimerIdentity(first, second));

    const std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({}, {first, second}, 540.0, 0, false);
    CHECK_EQ(states.size(), 2u);
}

TEST_CASE("legacy timer periods match the Might and Magic calendar")
{
    struct Case
    {
        OpenYAMM::Game::ScriptedEventTimerScheduleKind scheduleKind;
        double periodGameMinutes;
    };

    const std::vector<Case> cases = {
        {OpenYAMM::Game::ScriptedEventTimerScheduleKind::Weekly, 7.0 * 24.0 * 60.0},
        {OpenYAMM::Game::ScriptedEventTimerScheduleKind::Monthly, 28.0 * 24.0 * 60.0},
        {OpenYAMM::Game::ScriptedEventTimerScheduleKind::Yearly, 336.0 * 24.0 * 60.0},
    };

    for (const Case &testCase : cases)
    {
        const OpenYAMM::Game::ScriptedEventTimerDefinition definition =
            makeLegacyTimer(testCase.scheduleKind);
        const int64_t lastVisit = OpenYAMM::Game::legacyTimerTicksFromGameMinutes(1234.0);
        const std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
            OpenYAMM::Game::reconcileScriptedEventTimers({}, {definition}, 1300.0, lastVisit, false);
        REQUIRE_EQ(states.size(), 1u);
        CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(1234.0 + testCase.periodGameMinutes));
    }
}

TEST_CASE("legacy timer tick conversion round trips game minutes")
{
    const double gameMinutes = 9876.25;
    const int64_t ticks = OpenYAMM::Game::legacyTimerTicksFromGameMinutes(gameMinutes);
    CHECK_EQ(ticks, 2528320);
    CHECK_EQ(OpenYAMM::Game::legacyTimerGameMinutesFromTicks(ticks), doctest::Approx(gameMinutes));
}

TEST_CASE("version 66 timer migration converts its remaining duration exactly once")
{
    const OpenYAMM::Game::ScriptedEventTimerDefinition definition =
        makeLegacyTimer(OpenYAMM::Game::ScriptedEventTimerScheduleKind::Daily, 7);

    OpenYAMM::Game::ScriptedEventTimerState migratedState = {};
    migratedState.definition.scope = OpenYAMM::Game::ScriptedEventScope::Map;
    migratedState.definition.origin = OpenYAMM::Game::ScriptedEventTimerOrigin::Native;
    migratedState.definition.eventId = 130;
    migratedState.definition.sourceEventId = 130;
    migratedState.definition.registrationIndex = 1;
    migratedState.migratedFromLegacySave = true;
    migratedState.migratedRemainingGameMinutes = 12.5;

    const std::vector<OpenYAMM::Game::ScriptedEventTimerState> states =
        OpenYAMM::Game::reconcileScriptedEventTimers({migratedState}, {definition}, 100.0, 0, false);
    REQUIRE_EQ(states.size(), 1u);
    CHECK_EQ(states[0].definition.origin, OpenYAMM::Game::ScriptedEventTimerOrigin::Legacy);
    CHECK_EQ(states[0].definition.triggerStep, 7);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(112.5));
    CHECK_FALSE(states[0].migratedFromLegacySave);
    CHECK_EQ(states[0].migratedRemainingGameMinutes, 0.0);

    const std::vector<OpenYAMM::Game::ScriptedEventTimerState> reconciledAgain =
        OpenYAMM::Game::reconcileScriptedEventTimers(states, {definition}, 105.0, 0, false);
    REQUIRE_EQ(reconciledAgain.size(), 1u);
    CHECK_EQ(reconciledAgain[0].nextAlarmGameMinutes, doctest::Approx(112.5));
}

TEST_CASE("native repeating timer drops missed intervals instead of firing every frame")
{
    OpenYAMM::Game::ScriptedEventTimerState state = {};
    state.definition.scope = OpenYAMM::Game::ScriptedEventScope::Map;
    state.definition.origin = OpenYAMM::Game::ScriptedEventTimerOrigin::Native;
    state.definition.eventId = 210;
    state.definition.repeating = true;
    state.definition.intervalGameMinutes = 5.0;
    state.nextAlarmGameMinutes = 10.0;

    std::vector<OpenYAMM::Game::ScriptedEventTimerState> states = {state};
    int executionCount = 0;
    const auto execute =
        [&executionCount](const OpenYAMM::Game::ScriptedEventTimerDefinition &)
        {
            ++executionCount;
            return true;
        };

    // Eight hours of rest leave 96 intervals behind the clock.
    CHECK(OpenYAMM::Game::updateScriptedEventTimers(states, 490.0, execute));
    CHECK_EQ(executionCount, 1);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(495.0));

    CHECK_FALSE(OpenYAMM::Game::updateScriptedEventTimers(states, 490.1, execute));
    CHECK_EQ(executionCount, 1);

    CHECK(OpenYAMM::Game::updateScriptedEventTimers(states, 495.5, execute));
    CHECK_EQ(executionCount, 2);
    CHECK_EQ(states[0].nextAlarmGameMinutes, doctest::Approx(500.0));
}
