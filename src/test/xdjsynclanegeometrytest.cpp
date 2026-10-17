#include <gtest/gtest.h>

#include "xdjsync/lanegeometry.h"

namespace {

using mixxx::Duration;
using namespace mixxx::xdjsync;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

// 120 BPM: 2 beats per second. 60 px per second = 30 px per beat.
constexpr double kCenter = 200.0;
constexpr double kWidth = 400.0;
constexpr double kPxPerSecond = 60.0;

LaneState lane(double beatPosition, bool playing = true, QList<LaneCue> cues = {}) {
    return LaneState{playing, 120.0, beatPosition, ms(1000), std::move(cues)};
}

const LaneTick* tickAt(const LaneView& view, double x) {
    for (const LaneTick& tick : view.ticks) {
        if (std::abs(tick.x - x) < 1e-6) {
            return &tick;
        }
    }
    return nullptr;
}

TEST(XdjSyncLaneGeometryTest, BeatUnderPlayhead) {
    const LaneView view = laneView(lane(8.0), ms(1000), kCenter, kWidth, kPxPerSecond);
    const LaneTick* pTick = tickAt(view, kCenter);
    ASSERT_NE(nullptr, pTick);
    EXPECT_TRUE(pTick->bar);
    EXPECT_EQ(3, pTick->barNumber); // beats 8..11 = bar 3
    const LaneTick* pNext = tickAt(view, kCenter + 30.0);
    ASSERT_NE(nullptr, pNext);
    EXPECT_FALSE(pNext->bar);
}

TEST(XdjSyncLaneGeometryTest, OnlyTicksInsideTheLane) {
    const LaneView view = laneView(lane(8.0), ms(1000), kCenter, kWidth, kPxPerSecond);
    for (const LaneTick& tick : view.ticks) {
        EXPECT_GE(tick.x, 0.0);
        EXPECT_LE(tick.x, kWidth);
    }
    // 400 px / 30 px per beat = 13 or 14 beats visible.
    EXPECT_GE(view.ticks.size(), 13);
    EXPECT_LE(view.ticks.size(), 14);
}

TEST(XdjSyncLaneGeometryTest, ProjectsWhilePlaying) {
    // 250 ms later at 120 BPM: half a beat on, beat 8 moved 15 px left.
    const LaneView view = laneView(lane(8.0), ms(1250), kCenter, kWidth, kPxPerSecond);
    EXPECT_NE(nullptr, tickAt(view, kCenter - 15.0));
}

TEST(XdjSyncLaneGeometryTest, HoldsWhilePaused) {
    const LaneView view = laneView(lane(8.0, false), ms(5000), kCenter, kWidth, kPxPerSecond);
    EXPECT_NE(nullptr, tickAt(view, kCenter));
}

TEST(XdjSyncLaneGeometryTest, TimeAxisShowsTempoDifference) {
    // Same px per second: at 60 BPM beats are twice as far apart.
    LaneState slow = lane(8.0);
    slow.bpm = 60.0;
    const LaneView view = laneView(slow, ms(1000), kCenter, kWidth, kPxPerSecond);
    EXPECT_NE(nullptr, tickAt(view, kCenter + 60.0));
    EXPECT_EQ(nullptr, tickAt(view, kCenter + 30.0));
}

TEST(XdjSyncLaneGeometryTest, CueMarks) {
    const LaneView view = laneView(lane(8.0, true, {LaneCue{10.0, 0}, LaneCue{100.0, 1}}),
            ms(1000),
            kCenter,
            kWidth,
            kPxPerSecond);
    ASSERT_EQ(1, view.cues.size()); // the far one is off the lane
    EXPECT_DOUBLE_EQ(kCenter + 60.0, view.cues[0].x);
    EXPECT_EQ(QStringLiteral("A"), view.cues[0].label);
}

TEST(XdjSyncLaneGeometryTest, NoTicksWithoutTempo) {
    LaneState noTempo = lane(8.0);
    noTempo.bpm = 0.0;
    EXPECT_TRUE(laneView(noTempo, ms(1000), kCenter, kWidth, kPxPerSecond).ticks.isEmpty());
}

TEST(XdjSyncLaneGeometryTest, BeatsToNextCue) {
    const LaneState state = lane(8.0, true, {LaneCue{4.0, 0}, LaneCue{72.0, 1}, LaneCue{40.0, 2}});
    // 500 ms later = 1 beat on.
    const auto beats = beatsToNextCue(state, ms(1500));
    ASSERT_TRUE(beats.has_value());
    EXPECT_NEAR(31.0, *beats, 1e-9);
    EXPECT_FALSE(beatsToNextCue(lane(80.0, true, {LaneCue{4.0, 0}}), ms(1000)).has_value());
}

TEST(XdjSyncLaneGeometryTest, CountdownText) {
    // Fixed width: bars padded to two digits, CDJ-style.
    EXPECT_EQ(QStringLiteral("15.4"), formatCountdown(64.0));
    EXPECT_EQ(QStringLiteral("01.4"), formatCountdown(8.0));
    EXPECT_EQ(QStringLiteral("00.4"), formatCountdown(4.0));
    EXPECT_EQ(QStringLiteral("15.2"), formatCountdown(61.5));
    EXPECT_EQ(QStringLiteral("01.1"), formatCountdown(5.0));
    EXPECT_EQ(QStringLiteral("00.1"), formatCountdown(0.2));
    EXPECT_EQ(QStringLiteral("--.-"), formatCountdown(std::nullopt));
}

TEST(XdjSyncLaneGeometryTest, UpcomingCuesForPeers) {
    // Cues far behind are dropped, the rest sorted, capped.
    QList<LaneCue> cues;
    for (int i = 0; i < 12; ++i) {
        cues.append(LaneCue{100.0 - 16.0 * i, i}); // 100, 84, ... -76
    }
    const QList<UpcomingCue> upcoming = upcomingCues(cues, 50.0);
    ASSERT_EQ(5, upcoming.size()); // 36 (-14), 52, 68, 84, 100
    EXPECT_EQ(4, upcoming[0].number);
    EXPECT_DOUBLE_EQ(-14.0, upcoming[0].beatsAhead);
    EXPECT_DOUBLE_EQ(50.0, upcoming[4].beatsAhead);

    QList<LaneCue> many;
    for (int i = 0; i < 20; ++i) {
        many.append(LaneCue{static_cast<double>(i), i});
    }
    EXPECT_EQ(kMaxUpcomingCues, upcomingCues(many, 0.0).size());
}

TEST(XdjSyncLaneGeometryTest, LanesBoardHandsOverSnapshot) {
    LanesSnapshot snapshot;
    snapshot.own = lane(8.0);
    snapshot.isMaster = true;
    publishLanes(snapshot);
    const LanesSnapshot latest = latestLanes();
    ASSERT_TRUE(latest.own.has_value());
    EXPECT_DOUBLE_EQ(8.0, latest.own->beatPosition);
    EXPECT_FALSE(latest.reference.has_value());
    EXPECT_TRUE(latest.isMaster);
    publishLanes(LanesSnapshot{});
}

} // namespace
