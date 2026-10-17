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

/// Reference at 128 BPM, playing; this player `offsetMs` ahead.
ReferenceStatus reference(double offsetMs,
        bool playing = true,
        double clockUncertaintyMs = 0.2,
        double localBpm = kRefBpm) {
    const double bpmDelta = localBpm - kRefBpm;
    return ReferenceStatus{
            QStringLiteral("ref"),
            playing,
            kRefBpm,
            ClockEstimate{ms(0), ms(2 * clockUncertaintyMs), ms(clockUncertaintyMs)},
            BeatOffset{offsetMs, offsetMs / kMsPerBeat, bpmDelta, std::abs(bpmDelta) >= 0.01, 0.0},
    };
}

/// Like reference(), but this player is `wholeBeats` off in the bar.
ReferenceStatus referenceOffInBar(int wholeBeats, double offsetMs) {
    ReferenceStatus ref = reference(offsetMs);
    ref.offset->barOffsetBeats = wholeBeats + offsetMs / kMsPerBeat;
    ref.offset->beatsOffInBar = wholeBeats;
    return ref;
}

TEST(XdjSyncPhaseFollowerTest, BeatModeAlignsBar) {
    // On the beat, but one beat ahead in the bar: jump back one beat (plus the
    // fraction), even though the in-beat offset is tiny.
    PhaseFollower follower;
    const auto action = follower.update(
            PlayerSyncMode::Beat, true, kRefBpm, referenceOffInBar(1, 0.3), ms(0));
    ASSERT_TRUE(action.beatJump.has_value());
    EXPECT_NEAR(-(1.0 + 0.3 / kMsPerBeat), *action.beatJump, 1e-12);
}

TEST(XdjSyncPhaseFollowerTest, BarAlignmentCanBeOff) {
    PhaseFollower follower(false);
    const auto action = follower.update(
            PlayerSyncMode::Beat, true, kRefBpm, referenceOffInBar(-2, 0.3), ms(0));
    EXPECT_FALSE(action.beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, SnapAlsoAlignsBar) {
    // 30 ms behind and one beat behind in the bar: one jump fixes both.
    PhaseFollower follower;
    const auto action = follower.update(
            PlayerSyncMode::Beat, true, kRefBpm, referenceOffInBar(-1, -30.0), ms(0));
    ASSERT_TRUE(action.beatJump.has_value());
    EXPECT_NEAR(1.0 + 30.0 / kMsPerBeat, *action.beatJump, 1e-12);
}

TEST(XdjSyncPhaseFollowerTest, FreeAndMasterDoNothing) {
    PhaseFollower follower;
    for (auto mode : {PlayerSyncMode::Free, PlayerSyncMode::Master}) {
        const auto action = follower.update(mode, true, 127.0, reference(50.0), ms(0));
        EXPECT_FALSE(action.targetBpm.has_value());
        EXPECT_FALSE(action.beatJump.has_value());
    }
}

TEST(XdjSyncPhaseFollowerTest, BpmModeIgnoresPhase) {
    PhaseFollower follower;
    const auto action = follower.update(PlayerSyncMode::Bpm, true, 127.0, reference(50.0), ms(0));
    EXPECT_EQ(kRefBpm, action.targetBpm);
    EXPECT_FALSE(action.beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, BeatModeSnapsLargeOffset) {
    // 50 ms ahead: jump back by that many beats, at exactly the reference tempo.
    PhaseFollower follower;
    const auto action = follower.update(PlayerSyncMode::Beat, true, 127.0, reference(50.0), ms(0));
    ASSERT_TRUE(action.beatJump.has_value());
    EXPECT_NEAR(-50.0 / kMsPerBeat, *action.beatJump, 1e-12);
    EXPECT_EQ(kRefBpm, action.targetBpm);
}

TEST(XdjSyncPhaseFollowerTest, SnapHasCooldown) {
    // Right after a jump the measured offset is still the old one.
    PhaseFollower follower;
    EXPECT_TRUE(follower.update(PlayerSyncMode::Beat, true, kRefBpm, reference(-50.0), ms(0))
                    .beatJump.has_value());
    EXPECT_FALSE(follower.update(PlayerSyncMode::Beat, true, kRefBpm, reference(-50.0), ms(100))
                    .beatJump.has_value());
    EXPECT_TRUE(follower.update(PlayerSyncMode::Beat,
                                true,
                                kRefBpm,
                                reference(-50.0),
                                ms(0) + PhaseFollower::kSnapCooldown)
                    .beatJump.has_value());
}

TEST(XdjSyncPhaseFollowerTest, BeatModeTrimsSmallOffsetWithTempo) {
    // 4 ms ahead: slow down by 4 ms / settle time.
    PhaseFollower follower;
    const auto action = follower.update(PlayerSyncMode::Beat, true, kRefBpm, reference(4.0), ms(0));
    EXPECT_FALSE(action.beatJump.has_value());
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_NEAR(kRefBpm * (1.0 - 0.004 / PhaseFollower::kSettleSeconds), *action.targetBpm, 1e-9);

    // Behind: speed up.
    const auto behind = follower.update(PlayerSyncMode::Beat, true, kRefBpm, reference(-4.0), ms(0));
    ASSERT_TRUE(behind.targetBpm.has_value());
    EXPECT_GT(*behind.targetBpm, kRefBpm);
}

TEST(XdjSyncPhaseFollowerTest, TrimIsClamped) {
    PhaseFollower follower;
    const auto action = follower.update(PlayerSyncMode::Beat,
            true,
            kRefBpm,
            reference(PhaseFollower::kSnapMs - 0.1),
            ms(0));
    EXPECT_FALSE(action.beatJump.has_value());
    ASSERT_TRUE(action.targetBpm.has_value());
    EXPECT_NEAR(kRefBpm * (1.0 - PhaseFollower::kMaxTrim), *action.targetBpm, 1e-9);
}

TEST(XdjSyncPhaseFollowerTest, DeadbandHoldsReferenceTempo) {
    PhaseFollower follower;
    const double tiny = PhaseFollower::kDeadbandMs / 2;
    // Coming from a trimmed tempo: back to exactly the reference tempo.
    EXPECT_EQ(kRefBpm,
            follower.update(PlayerSyncMode::Beat, true, kRefBpm * 0.998, reference(tiny), ms(0))
                    .targetBpm);
    // Already there: nothing to do.
    EXPECT_FALSE(follower.update(PlayerSyncMode::Beat, true, kRefBpm, reference(tiny), ms(0))
                    .targetBpm.has_value());
}

TEST(XdjSyncPhaseFollowerTest, NoPhaseCorrectionUnlessBothPlay) {
    PhaseFollower follower;
    for (const auto& [localPlaying, ref] :
            {std::pair{false, reference(50.0)}, std::pair{true, reference(50.0, false)}}) {
        const auto action = follower.update(PlayerSyncMode::Beat, localPlaying, 127.0, ref, ms(0));
        EXPECT_FALSE(action.beatJump.has_value());
        EXPECT_EQ(kRefBpm, action.targetBpm);
    }
}

TEST(XdjSyncPhaseFollowerTest, NoPhaseCorrectionWithUncertainClock) {
    PhaseFollower follower;
    const auto ref = reference(50.0, true, PhaseFollower::kMaxClockUncertaintyMs + 1.0);
    const auto action = follower.update(PlayerSyncMode::Beat, true, 127.0, ref, ms(0));
    EXPECT_FALSE(action.beatJump.has_value());
    EXPECT_EQ(kRefBpm, action.targetBpm);
}

TEST(XdjSyncPhaseFollowerTest, TempoOnlyWithoutOffset) {
    PhaseFollower follower;
    auto ref = reference(50.0);
    ref.offset.reset();
    ref.clock.reset();
    const auto action = follower.update(PlayerSyncMode::Beat, true, 127.0, ref, ms(0));
    EXPECT_FALSE(action.beatJump.has_value());
    EXPECT_EQ(kRefBpm, action.targetBpm);
}

TEST(XdjSyncPhaseFollowerTest, NoReferenceNoAction) {
    PhaseFollower follower;
    const auto action = follower.update(PlayerSyncMode::Beat, true, 127.0, std::nullopt, ms(0));
    EXPECT_FALSE(action.targetBpm.has_value());
    EXPECT_FALSE(action.beatJump.has_value());
}

} // namespace
