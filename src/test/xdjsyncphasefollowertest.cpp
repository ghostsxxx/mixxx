#include <gtest/gtest.h>

#include "xdjsync/phasefollower.h"

namespace {

using mixxx::Duration;
using namespace mixxx::xdjsync;

constexpr double kRefBpm = 128.0;
constexpr double kMsPerBeat = 60000.0 / kRefBpm;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

/// Reference "ref" at 128 BPM, playing; this player `offsetMs` ahead.
ReferenceStatus reference(double offsetMs,
        bool playing = true,
        double clockUncertaintyMs = 0.2,
        const QString& playerId = QStringLiteral("ref")) {
    return ReferenceStatus{
            playerId,
            playing,
            kRefBpm,
            ClockEstimate{ms(0), ms(2 * clockUncertaintyMs), ms(clockUncertaintyMs)},
            BeatOffset{offsetMs, offsetMs / kMsPerBeat, 0.0, false, 0.0},
    };
}

/// Like reference(), but this player is `wholeBeats` off in the bar.
ReferenceStatus referenceOffInBar(int wholeBeats, double offsetMs) {
    ReferenceStatus ref = reference(offsetMs);
    ref.offset->barOffsetBeats = wholeBeats + offsetMs / kMsPerBeat;
    ref.offset->beatsOffInBar = wholeBeats;
    return ref;
}

SyncAction beat(PhaseFollower* pFollower,
        const std::optional<ReferenceStatus>& ref,
        Duration now = ms(0),
        bool nudging = false,
        double localBpm = kRefBpm) {
    return pFollower->update(PlayerSyncMode::Beat, true, localBpm, ref, now, nudging);
}

/// A follower that has already engaged (and used its one jump).
PhaseFollower engaged() {
    PhaseFollower follower;
    beat(&follower, reference(0.0));
    return follower;
}

TEST(XdjSyncPhaseFollowerTest, FreeAndMasterDoNothing) {
    PhaseFollower follower;
    for (auto mode : {PlayerSyncMode::Free, PlayerSyncMode::Master}) {
        const auto action = follower.update(mode, true, 127.0, reference(50.0), ms(0), false);
        EXPECT_FALSE(action.targetBpm.has_value());
        EXPECT_FALSE(action.beatJump.has_value());
    }
}

TEST(XdjSyncPhaseFollowerTest, BpmModeIgnoresPhase) {
    PhaseFollower follower;
    const auto action =
            follower.update(PlayerSyncMode::Bpm, true, 127.0, reference(50.0), ms(0), false);
    EXPECT_EQ(kRefBpm, action.targetBpm);
    EXPECT_FALSE(action.beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, JumpsOnceWhenEngaged) {
    PhaseFollower follower;
    const auto first = beat(&follower, reference(50.0), ms(0), false, 127.0);
    ASSERT_TRUE(first.beatJump.has_value());
    EXPECT_NEAR(-50.0 / kMsPerBeat, *first.beatJump, 1e-12);
    EXPECT_EQ(kRefBpm, first.targetBpm);
    // Still off afterwards (e.g. measurement not updated yet): no second jump.
    EXPECT_FALSE(beat(&follower, reference(50.0), ms(20)).beatJump.has_value());
    EXPECT_FALSE(beat(&follower, reference(50.0), ms(5000)).beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, EngageJumpAlignsBar) {
    PhaseFollower follower;
    const auto action = beat(&follower, referenceOffInBar(1, 0.3));
    ASSERT_TRUE(action.beatJump.has_value());
    EXPECT_NEAR(-(1.0 + 0.3 / kMsPerBeat), *action.beatJump, 1e-12);
}

TEST(XdjSyncPhaseFollowerTest, BarAlignmentCanBeOff) {
    PhaseFollower follower(false);
    const auto action = beat(&follower, referenceOffInBar(-2, 30.0));
    ASSERT_TRUE(action.beatJump.has_value());
    EXPECT_NEAR(-30.0 / kMsPerBeat, *action.beatJump, 1e-12);
}

TEST(XdjSyncPhaseFollowerTest, NoJumpWhenEngagedAligned) {
    PhaseFollower follower;
    const auto action = beat(&follower, reference(0.2));
    EXPECT_FALSE(action.beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, JumpsAgainWhenMasterChanges) {
    PhaseFollower follower = engaged();
    EXPECT_FALSE(beat(&follower, reference(40.0)).beatJump.has_value());
    const auto action = beat(&follower, reference(40.0, true, 0.2, QStringLiteral("other")));
    EXPECT_TRUE(action.beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, NoJumpWhenSameMasterReturns) {
    // Network drop: the master expired, then came back. Same master, so
    // catch up by tempo.
    PhaseFollower follower = engaged();
    beat(&follower, std::nullopt, ms(1000));
    const auto action = beat(&follower, reference(40.0), ms(5000));
    EXPECT_FALSE(action.beatJump.has_value());
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_LT(*action.targetBpm, kRefBpm);
}

TEST(XdjSyncPhaseFollowerTest, JumpsWhenOtherMasterAfterDrop) {
    PhaseFollower follower = engaged();
    beat(&follower, std::nullopt, ms(1000));
    EXPECT_TRUE(beat(&follower, reference(40.0, true, 0.2, QStringLiteral("other")), ms(5000))
                    .beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, JumpsAgainWhenSyncReEngaged) {
    PhaseFollower follower = engaged();
    follower.update(PlayerSyncMode::Free, true, kRefBpm, reference(40.0), ms(100), false);
    EXPECT_TRUE(beat(&follower, reference(40.0), ms(200)).beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, JumpsAgainWhenPlayStarts) {
    PhaseFollower follower = engaged();
    follower.update(PlayerSyncMode::Beat, false, kRefBpm, reference(40.0), ms(100), false);
    EXPECT_TRUE(beat(&follower, reference(40.0), ms(200)).beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, EngageWaitsForPhaseCorrectionConditions) {
    // Engaged while the clock is not aligned yet: jump once it is.
    PhaseFollower follower;
    const auto uncertain =
            reference(40.0, true, PhaseFollower::kMaxClockUncertaintyMs + 1.0);
    EXPECT_FALSE(beat(&follower, uncertain).beatJump.has_value());
    EXPECT_TRUE(beat(&follower, reference(40.0), ms(500)).beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, TrimsWithTempo) {
    // 4 ms ahead: slow down by 4 ms / settle time.
    PhaseFollower follower = engaged();
    const auto action = beat(&follower, reference(4.0));
    EXPECT_FALSE(action.beatJump.has_value());
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_NEAR(kRefBpm * (1.0 - 0.004 / PhaseFollower::kSettleSeconds), *action.targetBpm, 1e-9);

    const auto behind = beat(&follower, reference(-4.0));
    ASSERT_TRUE(behind.targetBpm.has_value());
    EXPECT_GT(*behind.targetBpm, kRefBpm);
}

TEST(XdjSyncPhaseFollowerTest, LargeErrorsAreTrimmedNotJumped) {
    // 60 ms (13 % of a beat) after engaging: speed, capped at 5 %.
    PhaseFollower follower = engaged();
    const auto action = beat(&follower, reference(60.0));
    EXPECT_FALSE(action.beatJump.has_value());
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_NEAR(kRefBpm * (1.0 - PhaseFollower::kMaxTrim), *action.targetBpm, 1e-9);
}

TEST(XdjSyncPhaseFollowerTest, TrainWreckUsesFullTrim) {
    // More than 20 % of a beat behind: full speed-up, even though the
    // proportional term would be smaller at a long settle time.
    PhaseFollower follower = engaged();
    const auto action = beat(&follower, reference(-0.3 * kMsPerBeat));
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_NEAR(kRefBpm * (1.0 + PhaseFollower::kMaxTrim), *action.targetBpm, 1e-9);
}

TEST(XdjSyncPhaseFollowerTest, DeadbandHoldsReferenceTempo) {
    PhaseFollower follower = engaged();
    const double tiny = PhaseFollower::kDeadbandMs / 2;
    EXPECT_EQ(kRefBpm, beat(&follower, reference(tiny), ms(0), false, kRefBpm * 0.998).targetBpm);
    EXPECT_FALSE(beat(&follower, reference(tiny)).targetBpm.has_value());
}

TEST(XdjSyncPhaseFollowerTest, NoCorrectionWhileNudging) {
    PhaseFollower follower = engaged();
    const auto action = beat(&follower, reference(15.0), ms(100), true, kRefBpm * 1.01);
    EXPECT_FALSE(action.beatJump.has_value());
    // Tempo still follows the master, phase is left to the DJ.
    EXPECT_EQ(kRefBpm, action.targetBpm);
}

TEST(XdjSyncPhaseFollowerTest, NudgeBecomesIntentionalOffset) {
    PhaseFollower follower = engaged();
    beat(&follower, reference(15.0), ms(100), true);
    // Nudge over at +15 ms: keep it.
    EXPECT_FALSE(beat(&follower, reference(15.0), ms(200)).targetBpm.has_value());
    // Drifts to +19 ms: pull back to +15, not to 0.
    const auto action = beat(&follower, reference(19.0), ms(300));
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_NEAR(kRefBpm * (1.0 - 0.004 / PhaseFollower::kSettleSeconds), *action.targetBpm, 1e-9);
}

TEST(XdjSyncPhaseFollowerTest, EngagingClearsIntentionalOffset) {
    PhaseFollower follower = engaged();
    beat(&follower, reference(15.0), ms(100), true);
    beat(&follower, reference(15.0), ms(200));
    // New master: jump fully into line, offset forgotten.
    const auto action = beat(&follower, reference(15.0, true, 0.2, QStringLiteral("other")), ms(300));
    ASSERT_TRUE(action.beatJump.has_value());
    EXPECT_NEAR(-15.0 / kMsPerBeat, *action.beatJump, 1e-12);
}

TEST(XdjSyncPhaseFollowerTest, NoPhaseCorrectionUnlessBothPlay) {
    PhaseFollower follower;
    const auto localStopped =
            follower.update(PlayerSyncMode::Beat, false, 127.0, reference(50.0), ms(0), false);
    EXPECT_FALSE(localStopped.beatJump.has_value());
    EXPECT_EQ(kRefBpm, localStopped.targetBpm);
    const auto refStopped = beat(&follower, reference(50.0, false), ms(0), false, 127.0);
    EXPECT_FALSE(refStopped.beatJump.has_value());
    EXPECT_EQ(kRefBpm, refStopped.targetBpm);
}

TEST(XdjSyncPhaseFollowerTest, TempoOnlyWithoutOffset) {
    PhaseFollower follower;
    auto ref = reference(50.0);
    ref.offset.reset();
    ref.clock.reset();
    const auto action = beat(&follower, ref, ms(0), false, 127.0);
    EXPECT_FALSE(action.beatJump.has_value());
    EXPECT_EQ(kRefBpm, action.targetBpm);
}

TEST(XdjSyncPhaseFollowerTest, NoReferenceNoAction) {
    PhaseFollower follower;
    const auto action = beat(&follower, std::nullopt, ms(0), false, 127.0);
    EXPECT_FALSE(action.targetBpm.has_value());
    EXPECT_FALSE(action.beatJump.has_value());
}

} // namespace
