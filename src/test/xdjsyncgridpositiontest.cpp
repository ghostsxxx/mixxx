#include <gtest/gtest.h>

#include "track/beats.h"
#include "xdjsync/gridposition.h"

namespace {

using mixxx::Beats;
using mixxx::audio::FramePos;
using mixxx::audio::SampleRate;
using mixxx::xdjsync::beatPosition;
using mixxx::xdjsync::withDownbeatAt;

constexpr auto kRate = SampleRate(48000);

// 120 BPM at 48 kHz: one beat = 24000 frames. First downbeat at 1 s.
mixxx::BeatsPointer grid() {
    return Beats::fromConstTempo(kRate, FramePos(48000), mixxx::Bpm(120.0));
}

TEST(XdjSyncGridPositionTest, CountsFromFirstDownbeat) {
    EXPECT_DOUBLE_EQ(0.0, beatPosition(*grid(), FramePos(48000)));
    EXPECT_DOUBLE_EQ(1.5, beatPosition(*grid(), FramePos(48000 + 36000)));
    EXPECT_DOUBLE_EQ(10.0, beatPosition(*grid(), FramePos(48000 + 240000)));
}

TEST(XdjSyncGridPositionTest, NegativeBeforeFirstDownbeat) {
    EXPECT_DOUBLE_EQ(-1.0, beatPosition(*grid(), FramePos(24000)));
    EXPECT_DOUBLE_EQ(-1.5, beatPosition(*grid(), FramePos(12000)));
}

TEST(XdjSyncGridPositionTest, SetDownbeatAtNearestBeat) {
    // Pressed slightly after beat 5 (beat 2 of bar 2): that beat becomes 1.
    const FramePos press(48000 + 5 * 24000 + 1000);
    const auto newGrid = withDownbeatAt(*grid(), press);
    ASSERT_TRUE(newGrid.has_value());
    EXPECT_NEAR(4.0 + 1000.0 / 24000.0, beatPosition(**newGrid, press), 1e-3);
    // Beats themselves don't move; only their numbering does.
    EXPECT_NEAR(-1.0, beatPosition(**newGrid, FramePos(48000)), 1e-3);
}

TEST(XdjSyncGridPositionTest, SetDownbeatPressedEarly) {
    // Slightly before beat 3: beat 3 becomes bar 1.
    const FramePos press(48000 + 3 * 24000 - 2000);
    const auto newGrid = withDownbeatAt(*grid(), press);
    ASSERT_TRUE(newGrid.has_value());
    EXPECT_NEAR(-2000.0 / 24000.0, beatPosition(**newGrid, press), 1e-3);
}

TEST(XdjSyncGridPositionTest, SetDownbeatBeforeFirstMarker) {
    const FramePos press(24000); // beat -1
    const auto newGrid = withDownbeatAt(*grid(), press);
    ASSERT_TRUE(newGrid.has_value());
    EXPECT_NEAR(-4.0, beatPosition(**newGrid, press), 1e-3);
}

} // namespace
