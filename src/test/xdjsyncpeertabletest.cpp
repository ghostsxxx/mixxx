#include <gtest/gtest.h>

#include "xdjsync/peertable.h"

namespace {

using mixxx::Duration;
using namespace mixxx::xdjsync;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

StatePacket state(const QString& id,
        bool playing,
        PlayerSyncMode mode = PlayerSyncMode::Free,
        double bpm = 174.0,
        double beatDistance = 0.0,
        Duration atDac = ms(0),
        Duration playingFor = ms(0)) {
    return StatePacket{id, 1, playing, mode, bpm, beatDistance, 0, atDac, playingFor};
}

/// A peer that has been playing for `playingFor` at 174 BPM.
StatePacket playingPeer(const QString& id, Duration playingFor, double bpm = 174.0) {
    return state(id, true, PlayerSyncMode::Free, bpm, 0.0, ms(0), playingFor);
}

PhaseSample localStopped() {
    return PhaseSample{false, 174.0, 0.0, ms(0)};
}

PhaseSample localPlaying(double bpm, double beatDistance, Duration atDac) {
    return PhaseSample{true, bpm, beatDistance, atDac};
}

/// Feeds kMinSamples ping/pong exchanges with `peerId`, whose clock is
/// `peerAhead` ahead of ours, 1 ms each way.
void alignClock(PeerTable* pTable, const QString& peerId, Duration peerAhead, Duration start) {
    for (int i = 0; i < ClockEstimator::kMinSamples; ++i) {
        const Duration t1 = start + ms(250 * i);
        const Duration t2 = t1 + ms(1) + peerAhead;
        const PongPacket pong{peerId, QStringLiteral("me"), static_cast<quint32>(i), t1, t2, t2};
        pTable->handlePacket(pong, t1 + ms(2));
    }
}

TEST(XdjSyncPeerTableTest, IgnoresOwnPackets) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(state(QStringLiteral("me"), true), ms(0));
    EXPECT_EQ(0, table.peerCount());
    EXPECT_FALSE(table.handlePacket(PingPacket{QStringLiteral("me"), 1, ms(0)}, ms(0)));
}

TEST(XdjSyncPeerTableTest, CountsPeersFromState) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(state(QStringLiteral("p2"), false), ms(0));
    table.handlePacket(state(QStringLiteral("p3"), false), ms(0));
    table.handlePacket(state(QStringLiteral("p2"), true), ms(10));
    EXPECT_EQ(2, table.peerCount());
}

TEST(XdjSyncPeerTableTest, PeersTimeOut) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(state(QStringLiteral("p2"), true), ms(0));
    table.handlePacket(state(QStringLiteral("p3"), true), ms(600));
    table.expire(ms(1100));
    EXPECT_EQ(1, table.peerCount());
    table.expire(ms(1700));
    EXPECT_EQ(0, table.peerCount());
}

TEST(XdjSyncPeerTableTest, AnswersPingsWithPong) {
    PeerTable table(QStringLiteral("me"));
    const auto reply = table.handlePacket(PingPacket{QStringLiteral("p2"), 9, ms(100)}, ms(555));
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(QStringLiteral("me"), reply->playerId);
    EXPECT_EQ(QStringLiteral("p2"), reply->toPlayerId);
    EXPECT_EQ(9u, reply->seq);
    EXPECT_EQ(ms(100), reply->pingSent);
    EXPECT_EQ(ms(555), reply->pingReceived);
}

TEST(XdjSyncPeerTableTest, IgnoresPongsForOtherPlayers) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(state(QStringLiteral("p2"), true), ms(0));
    for (int i = 0; i < 5; ++i) {
        const PongPacket pong{QStringLiteral("p2"), QStringLiteral("p3"), 0, ms(0), ms(1), ms(1)};
        table.handlePacket(pong, ms(2));
    }
    const auto ref = table.reference(localStopped(), ms(0), ms(10));
    ASSERT_TRUE(ref.has_value());
    EXPECT_FALSE(ref->clock.has_value());
    EXPECT_FALSE(ref->offset.has_value());
}

TEST(XdjSyncPeerTableTest, LongestPlayingIsMaster) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(playingPeer(QStringLiteral("p2"), ms(3000)), ms(10000));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(9000)), ms(10000));
    table.handlePacket(playingPeer(QStringLiteral("p4"), ms(500)), ms(10000));
    EXPECT_EQ(QStringLiteral("p3"), table.masterPlayerId(localStopped(), ms(0), ms(10000)));
    const auto ref = table.reference(localStopped(), ms(0), ms(10000));
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(QStringLiteral("p3"), ref->playerId);
}

TEST(XdjSyncPeerTableTest, ArrivalTimeCountsForPlayingFor) {
    // p2 reported 1 s playing 4 s ago; p3 reports 2 s playing now. p2 started
    // first (5 s ago).
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(playingPeer(QStringLiteral("p2"), ms(1000)), ms(6000));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(2000)), ms(10000));
    EXPECT_EQ(QStringLiteral("p2"), table.masterPlayerId(localStopped(), ms(0), ms(10000)));
}

TEST(XdjSyncPeerTableTest, ThisPlayerCanBeMaster) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(playingPeer(QStringLiteral("p2"), ms(3000)), ms(10000));
    const PhaseSample local{true, 174.0, 0.0, ms(10000)};
    EXPECT_EQ(QStringLiteral("me"), table.masterPlayerId(local, ms(20000), ms(10000)));
    EXPECT_FALSE(table.reference(local, ms(20000), ms(10000)).has_value());
}

TEST(XdjSyncPeerTableTest, StoppedPlayersAreNotMaster) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(state(QStringLiteral("p2"), false), ms(10000));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(1000)), ms(10000));
    // This player was playing long ago but is stopped now.
    EXPECT_EQ(QStringLiteral("p3"), table.masterPlayerId(localStopped(), ms(60000), ms(10000)));
}

TEST(XdjSyncPeerTableTest, MasterHandsOverWhenItStops) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(playingPeer(QStringLiteral("p2"), ms(9000)), ms(10000));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(4000)), ms(10000));
    EXPECT_EQ(QStringLiteral("p2"), table.masterPlayerId(localStopped(), ms(0), ms(10000)));
    table.handlePacket(state(QStringLiteral("p2"), false), ms(10020));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(4020)), ms(10020));
    EXPECT_EQ(QStringLiteral("p3"), table.masterPlayerId(localStopped(), ms(0), ms(10020)));
}

TEST(XdjSyncPeerTableTest, PlayersWithoutTempoAreNotMaster) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(playingPeer(QStringLiteral("p2"), ms(9000), 0.0), ms(10000));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(1000)), ms(10000));
    EXPECT_EQ(QStringLiteral("p3"), table.masterPlayerId(localStopped(), ms(0), ms(10000)));
}

TEST(XdjSyncPeerTableTest, NoMasterWhenNothingPlays) {
    PeerTable table(QStringLiteral("me"));
    table.handlePacket(state(QStringLiteral("p2"), false), ms(0));
    EXPECT_FALSE(table.masterPlayerId(localStopped(), ms(0), ms(0)).has_value());
    EXPECT_FALSE(table.reference(localStopped(), ms(0), ms(0)).has_value());
}

TEST(XdjSyncPeerTableTest, NearSimultaneousStartGoesToLowestId) {
    // Players see each other's start times with a few ms of network delay;
    // starts closer than the tie window must give the same master everywhere.
    PeerTable table(QStringLiteral("p4"));
    table.handlePacket(playingPeer(QStringLiteral("p3"), ms(5050)), ms(10000));
    table.handlePacket(playingPeer(QStringLiteral("p2"), ms(5000)), ms(10000));
    EXPECT_EQ(QStringLiteral("p2"), table.masterPlayerId(localStopped(), ms(0), ms(10000)));
    const PhaseSample local{true, 174.0, 0.0, ms(10000)};
    // This player too: started 5.06 s ago, also within the window.
    EXPECT_EQ(QStringLiteral("p2"), table.masterPlayerId(local, ms(5060), ms(10000)));
}

TEST(XdjSyncPeerTableTest, OffsetUsesPeerClock) {
    // Peer clock is 5 s ahead. Peer reports beat start at its 5000 ms,
    // i.e. our 0 ms. We are at beat distance 0.1 at our 0 ms: +0.1 beat.
    PeerTable table(QStringLiteral("me"));
    alignClock(&table, QStringLiteral("p2"), ms(5000), ms(0));
    // Playing for long, so p2 is master and our reference.
    table.handlePacket(state(QStringLiteral("p2"),
                               true,
                               PlayerSyncMode::Free,
                               120.0,
                               0.0,
                               ms(5000),
                               ms(60000)),
            ms(600));
    const auto ref = table.reference(localPlaying(120.0, 0.1, ms(0)), ms(0), ms(700));
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(QStringLiteral("p2"), ref->playerId);
    EXPECT_TRUE(ref->playing);
    EXPECT_DOUBLE_EQ(120.0, ref->bpm);
    ASSERT_TRUE(ref->clock.has_value());
    EXPECT_EQ(ms(5000), ref->clock->offset);
    ASSERT_TRUE(ref->offset.has_value());
    EXPECT_NEAR(50.0, ref->offset->offsetMs, 1e-6);
}

TEST(XdjSyncPeerTableTest, ClockSurvivesUntilPeerExpires) {
    PeerTable table(QStringLiteral("me"));
    alignClock(&table, QStringLiteral("p2"), ms(0), ms(0));
    table.handlePacket(state(QStringLiteral("p2"), true), ms(600));
    table.expire(ms(1500));
    EXPECT_EQ(1, table.peerCount());
    table.expire(ms(1700));
    EXPECT_EQ(0, table.peerCount());
    table.handlePacket(state(QStringLiteral("p2"), true), ms(1800));
    const auto ref = table.reference(localStopped(), ms(0), ms(1800));
    ASSERT_TRUE(ref.has_value());
    EXPECT_FALSE(ref->clock.has_value());
}

} // namespace
