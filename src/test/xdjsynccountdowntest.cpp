#include <gtest/gtest.h>

#include "xdjsync/countdown.h"

namespace {

using mixxx::xdjsync::CueCountdown;
using mixxx::xdjsync::nextCueCountdown;

TEST(XdjSyncCountdownTest, CountsToNextCueAhead) {
    // At beat 10.25; cues at beats 4 (behind), 74.25 and 200.
    const auto countdown = nextCueCountdown(10.25, {4.0, 200.0, 74.25});
    ASSERT_TRUE(countdown.has_value());
    EXPECT_DOUBLE_EQ(64.0, countdown->beats);
    // 64 beats = 16 bars, counted CDJ-style: bar 15, beat 4 (of 4..1).
    EXPECT_EQ(15, countdown->bars);
    EXPECT_EQ(4, countdown->beat);
}

TEST(XdjSyncCountdownTest, BarsAndBeatsRoundUpToWholeBeats) {
    // 61.5 beats left: 62 beats to count = 15 bars and 2 beats.
    const auto countdown = nextCueCountdown(0.0, {61.5});
    ASSERT_TRUE(countdown.has_value());
    EXPECT_EQ(15, countdown->bars);
    EXPECT_EQ(2, countdown->beat);
}

TEST(XdjSyncCountdownTest, CueRightHereIsPassed) {
    // Standing on the cue (e.g. just jumped there): count to the next one.
    const auto countdown = nextCueCountdown(32.0, {32.0, 48.0});
    ASSERT_TRUE(countdown.has_value());
    EXPECT_DOUBLE_EQ(16.0, countdown->beats);
}

TEST(XdjSyncCountdownTest, NoCueAhead) {
    EXPECT_FALSE(nextCueCountdown(100.0, {4.0, 99.0}).has_value());
    EXPECT_FALSE(nextCueCountdown(100.0, {}).has_value());
}

TEST(XdjSyncCountdownTest, FromRemainingBeats) {
    const CueCountdown countdown = CueCountdown::fromBeats(5.0);
    EXPECT_EQ(1, countdown.bars);
    EXPECT_EQ(1, countdown.beat);
    EXPECT_EQ(0, CueCountdown::fromBeats(0.2).bars);
    EXPECT_EQ(1, CueCountdown::fromBeats(0.2).beat);
    // Beats count 4, 3, 2, 1 within each bar, never 0.
    EXPECT_EQ(0, CueCountdown::fromBeats(4.0).bars);
    EXPECT_EQ(4, CueCountdown::fromBeats(4.0).beat);
    EXPECT_EQ(1, CueCountdown::fromBeats(8.0).bars);
    EXPECT_EQ(4, CueCountdown::fromBeats(8.0).beat);
}

} // namespace
