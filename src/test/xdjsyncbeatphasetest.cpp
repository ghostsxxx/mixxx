#include <gtest/gtest.h>

#include "xdjsync/beatphase.h"

namespace {

using mixxx::Duration;
using mixxx::xdjsync::BeatOffset;
using mixxx::xdjsync::PhaseSample;
using mixxx::xdjsync::beatOffset;
using mixxx::xdjsync::inSync;
using mixxx::xdjsync::kInSyncMs;
using mixxx::xdjsync::projectPhase;
using mixxx::xdjsync::wrapHalfBeat;

constexpr double kMsTolerance = 1e-6;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

PhaseSample playing(double bpm, double beatDistance, Duration atDac) {
    return PhaseSample{true, bpm, beatDistance, atDac};
}

TEST(XdjSyncBeatPhaseTest, WrapHalfBeat) {
    EXPECT_DOUBLE_EQ(0.0, wrapHalfBeat(0.0));
    EXPECT_DOUBLE_EQ(0.25, wrapHalfBeat(0.25));
    EXPECT_DOUBLE_EQ(-0.25, wrapHalfBeat(0.75));
    EXPECT_DOUBLE_EQ(-0.25, wrapHalfBeat(-0.25));
    EXPECT_DOUBLE_EQ(0.25, wrapHalfBeat(-0.75));
    EXPECT_DOUBLE_EQ(-0.5, wrapHalfBeat(0.5));
    EXPECT_NEAR(0.1, wrapHalfBeat(3.1), 1e-12);
}

TEST(XdjSyncBeatPhaseTest, ProjectPhaseAdvancesWhilePlaying) {
    // 120 BPM = 2 beats per second; 250 ms later = half a beat further.
    const auto sample = playing(120.0, 0.1, ms(1000));
    EXPECT_NEAR(0.6, projectPhase(sample, ms(1250)), 1e-9);
}

TEST(XdjSyncBeatPhaseTest, ProjectPhaseBackwardsInTime) {
    const auto sample = playing(120.0, 0.1, ms(1000));
    EXPECT_NEAR(-0.4, projectPhase(sample, ms(750)), 1e-9);
}

TEST(XdjSyncBeatPhaseTest, ProjectPhaseHoldsWhenPaused) {
    const PhaseSample sample{false, 120.0, 0.3, ms(1000)};
    EXPECT_DOUBLE_EQ(0.3, projectPhase(sample, ms(5000)));
}

TEST(XdjSyncBeatPhaseTest, InSyncIsZero) {
    const auto local = playing(174.0, 0.2, ms(1000));
    const auto peer = playing(174.0, 0.2, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1010));
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(0.0, offset->offsetMs, kMsTolerance);
    EXPECT_FALSE(offset->drifting);
}

TEST(XdjSyncBeatPhaseTest, LocalAheadIsPositive) {
    // 120 BPM: one beat = 500 ms. 0.1 beat ahead = +50 ms.
    const auto local = playing(120.0, 0.3, ms(1000));
    const auto peer = playing(120.0, 0.2, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1000));
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(50.0, offset->offsetMs, kMsTolerance);
    EXPECT_NEAR(0.1, offset->offsetBeats, 1e-9);
}

TEST(XdjSyncBeatPhaseTest, LocalBehindIsNegative) {
    const auto local = playing(120.0, 0.2, ms(1000));
    const auto peer = playing(120.0, 0.3, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1000));
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(-50.0, offset->offsetMs, kMsTolerance);
}

TEST(XdjSyncBeatPhaseTest, OffsetWrapsAcrossBeatBoundary) {
    // Local just past a beat, peer just before the next one: local is
    // 0.1 beat ahead, not 0.9 beat behind.
    const auto local = playing(120.0, 0.05, ms(1000));
    const auto peer = playing(120.0, 0.95, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1000));
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(50.0, offset->offsetMs, kMsTolerance);
}

TEST(XdjSyncBeatPhaseTest, SamplesTakenAtDifferentTimes) {
    // Same audible phase, but the peer's sample is 100 ms older.
    // 174 BPM: 100 ms = 0.29 beats.
    const double beatsPer100Ms = 174.0 / 60.0 * 0.1;
    const auto local = playing(174.0, 0.5, ms(2000));
    const auto peer = playing(174.0, 0.5 - beatsPer100Ms, ms(1900));
    const auto offset = beatOffset(local, peer, ms(2005));
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(0.0, offset->offsetMs, kMsTolerance);
}

TEST(XdjSyncBeatPhaseTest, Known40MsOffsetAt174Bpm) {
    // Matches testdata click_174_00_low_offset40ms vs click_174_00_low:
    // the offset track's beats come 40 ms later, so it is behind.
    const double beatsPer40Ms = 174.0 / 60.0 * 0.040;
    const auto local = playing(174.0, 0.5 - beatsPer40Ms, ms(1000));
    const auto peer = playing(174.0, 0.5, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1000));
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(-40.0, offset->offsetMs, kMsTolerance);
}

TEST(XdjSyncBeatPhaseTest, BpmDeltaAndDrift) {
    // Local 0.5 BPM faster at 128: gains 0.5/60 beats per second.
    // At local 128.5 BPM one beat is 60000/128.5 ms.
    const auto local = playing(128.5, 0.0, ms(1000));
    const auto peer = playing(128.0, 0.0, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1000));
    ASSERT_TRUE(offset.has_value());
    EXPECT_DOUBLE_EQ(0.5, offset->bpmDelta);
    EXPECT_TRUE(offset->drifting);
    EXPECT_NEAR(0.5 / 60.0 * 60000.0 / 128.5, offset->driftMsPerSecond, 1e-9);
}

TEST(XdjSyncBeatPhaseTest, TinyBpmDeltaIsNotDrifting) {
    const auto local = playing(174.004, 0.0, ms(1000));
    const auto peer = playing(174.0, 0.0, ms(1000));
    const auto offset = beatOffset(local, peer, ms(1000));
    ASSERT_TRUE(offset.has_value());
    EXPECT_FALSE(offset->drifting);
}

TEST(XdjSyncBeatPhaseTest, InSyncWithinThreshold) {
    const double msPerBeat = 60000.0 / 120.0;
    const auto offsetAt = [&](double offsetMs, double peerBpm = 120.0) {
        const auto local = playing(120.0, 0.5 + offsetMs / msPerBeat, ms(1000));
        const auto peer = playing(peerBpm, 0.5, ms(1000));
        return *beatOffset(local, peer, ms(1000));
    };
    EXPECT_TRUE(inSync(offsetAt(0.0)));
    EXPECT_TRUE(inSync(offsetAt(kInSyncMs - 0.1)));
    EXPECT_TRUE(inSync(offsetAt(-(kInSyncMs - 0.1))));
    EXPECT_FALSE(inSync(offsetAt(kInSyncMs + 0.1)));
    EXPECT_FALSE(inSync(offsetAt(-(kInSyncMs + 0.1))));
    // Aligned right now, but drifting apart: not in sync.
    EXPECT_FALSE(inSync(offsetAt(0.0, 121.0)));
    // Unless the tempo difference is a deliberate phase trim (BEAT mode).
    EXPECT_TRUE(inSync(offsetAt(0.0, 121.0), true));
    EXPECT_FALSE(inSync(offsetAt(kInSyncMs + 0.1, 121.0), true));
}

TEST(XdjSyncBeatPhaseTest, NoOffsetWithoutValidBpm) {
    const auto valid = playing(120.0, 0.0, ms(1000));
    const auto noBpm = playing(0.0, 0.0, ms(1000));
    EXPECT_FALSE(beatOffset(valid, noBpm, ms(1000)).has_value());
    EXPECT_FALSE(beatOffset(noBpm, valid, ms(1000)).has_value());
}

} // namespace
