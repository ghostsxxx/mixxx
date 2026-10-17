#include <gtest/gtest.h>

#include "xdjsync/packet.h"

namespace {

using mixxx::Duration;
using namespace mixxx::xdjsync;

StatePacket makeState() {
    return StatePacket{
            QStringLiteral("player2"),
            42,
            true,
            PlayerSyncMode::Master,
            174.03,
            0.4375,
            -3,
            Duration::fromNanos(123456789012345),
            Duration::fromMillis(95000),
            true,
            37.5,
    };
}

TEST(XdjSyncPacketTest, StateRoundTrip) {
    const StatePacket state = makeState();
    const auto decoded = decodePacket(encodePacket(state));
    ASSERT_TRUE(decoded.has_value());
    const auto* pState = std::get_if<StatePacket>(&*decoded);
    ASSERT_NE(nullptr, pState);
    EXPECT_EQ(state.playerId, pState->playerId);
    EXPECT_EQ(state.seq, pState->seq);
    EXPECT_EQ(state.playing, pState->playing);
    EXPECT_EQ(state.syncMode, pState->syncMode);
    EXPECT_EQ(state.bpm, pState->bpm);
    EXPECT_EQ(state.beatDistance, pState->beatDistance);
    EXPECT_EQ(state.beatIndex, pState->beatIndex);
    EXPECT_EQ(state.atDac, pState->atDac);
    EXPECT_EQ(state.playingFor, pState->playingFor);
    EXPECT_TRUE(pState->beatIndexValid);
    EXPECT_EQ(state.cueCountdownBeats, pState->cueCountdownBeats);
}

TEST(XdjSyncPacketTest, StateWithoutGridOrCue) {
    auto state = makeState();
    state.beatIndexValid = false;
    state.cueCountdownBeats.reset();
    const auto decoded = decodePacket(encodePacket(state));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(std::get<StatePacket>(*decoded).beatIndexValid);
    EXPECT_FALSE(std::get<StatePacket>(*decoded).cueCountdownBeats.has_value());
}

// Appended (optional) tail of makeState(): playingFor (8), cue countdown (8),
// upcoming cue count (1, no cues).
constexpr int kUpcomingCuesTail = 1;
constexpr int kCountdownTail = 8 + kUpcomingCuesTail;
constexpr int kOptionalTail = 8 + kCountdownTail;

TEST(XdjSyncPacketTest, UpcomingCuesRoundTrip) {
    auto state = makeState();
    state.upcomingCues = {UpcomingCue{0, -3.5}, UpcomingCue{4, 60.25}};
    const auto decoded = decodePacket(encodePacket(state));
    ASSERT_TRUE(decoded.has_value());
    const auto& cues = std::get<StatePacket>(*decoded).upcomingCues;
    ASSERT_EQ(2, cues.size());
    EXPECT_EQ(0, cues[0].number);
    EXPECT_DOUBLE_EQ(-3.5, cues[0].beatsAhead);
    EXPECT_EQ(4, cues[1].number);
    EXPECT_DOUBLE_EQ(60.25, cues[1].beatsAhead);
}

TEST(XdjSyncPacketTest, StateWithoutUpcomingCuesDecodesAsEmpty) {
    QByteArray bytes = encodePacket(makeState());
    bytes.chop(kUpcomingCuesTail);
    const auto decoded = decodePacket(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(std::get<StatePacket>(*decoded).upcomingCues.isEmpty());
    EXPECT_TRUE(std::get<StatePacket>(*decoded).cueCountdownBeats.has_value());
}

TEST(XdjSyncPacketTest, RejectsTruncatedUpcomingCues) {
    auto state = makeState();
    state.upcomingCues = {UpcomingCue{0, 1.0}, UpcomingCue{1, 2.0}};
    QByteArray bytes = encodePacket(state);
    bytes.chop(4);
    EXPECT_FALSE(decodePacket(bytes).has_value());
}

TEST(XdjSyncPacketTest, RejectsTooManyUpcomingCues) {
    auto state = makeState();
    for (int i = 0; i <= kMaxUpcomingCues; ++i) {
        state.upcomingCues.append(UpcomingCue{static_cast<quint8>(i), 1.0 * i});
    }
    EXPECT_FALSE(decodePacket(encodePacket(state)).has_value());
}

TEST(XdjSyncPacketTest, StateWithoutCueCountdownDecodesAsNone) {
    // Packets from before cueCountdownBeats was appended.
    QByteArray bytes = encodePacket(makeState());
    bytes.chop(kCountdownTail);
    const auto decoded = decodePacket(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(Duration::fromMillis(95000), std::get<StatePacket>(*decoded).playingFor);
    EXPECT_FALSE(std::get<StatePacket>(*decoded).cueCountdownBeats.has_value());
}

TEST(XdjSyncPacketTest, StateWithoutPlayingForDecodesAsZero) {
    // Packets from before playingFor was appended (8 bytes at the end).
    QByteArray bytes = encodePacket(makeState());
    bytes.chop(kOptionalTail);
    const auto decoded = decodePacket(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(Duration(), std::get<StatePacket>(*decoded).playingFor);
}

TEST(XdjSyncPacketTest, PingRoundTrip) {
    const PingPacket ping{QStringLiteral("player1"), 7, Duration::fromNanos(-5)};
    const auto decoded = decodePacket(encodePacket(ping));
    ASSERT_TRUE(decoded.has_value());
    const auto* pPing = std::get_if<PingPacket>(&*decoded);
    ASSERT_NE(nullptr, pPing);
    EXPECT_EQ(ping.playerId, pPing->playerId);
    EXPECT_EQ(ping.seq, pPing->seq);
    EXPECT_EQ(ping.sent, pPing->sent);
}

TEST(XdjSyncPacketTest, PongRoundTrip) {
    const PongPacket pong{
            QStringLiteral("player3"),
            QStringLiteral("player1"),
            7,
            Duration::fromNanos(100),
            Duration::fromNanos(5000),
            Duration::fromNanos(5100),
    };
    const auto decoded = decodePacket(encodePacket(pong));
    ASSERT_TRUE(decoded.has_value());
    const auto* pPong = std::get_if<PongPacket>(&*decoded);
    ASSERT_NE(nullptr, pPong);
    EXPECT_EQ(pong.playerId, pPong->playerId);
    EXPECT_EQ(pong.toPlayerId, pPong->toPlayerId);
    EXPECT_EQ(pong.seq, pPong->seq);
    EXPECT_EQ(pong.pingSent, pPong->pingSent);
    EXPECT_EQ(pong.pingReceived, pPong->pingReceived);
    EXPECT_EQ(pong.pongSent, pPong->pongSent);
}

TEST(XdjSyncPacketTest, StateIsSmall) {
    // Sent 50-100 times per second; keep well below one MTU.
    EXPECT_LT(encodePacket(makeState()).size(), 80);
}

TEST(XdjSyncPacketTest, RejectsEmptyAndGarbage) {
    EXPECT_FALSE(decodePacket(QByteArray()).has_value());
    EXPECT_FALSE(decodePacket(QByteArray("hello world, not a packet")).has_value());
}

TEST(XdjSyncPacketTest, RejectsWrongMagic) {
    QByteArray bytes = encodePacket(makeState());
    bytes[0] = 'Y';
    EXPECT_FALSE(decodePacket(bytes).has_value());
}

TEST(XdjSyncPacketTest, RejectsOtherVersion) {
    QByteArray bytes = encodePacket(makeState());
    bytes[4] = static_cast<char>(kProtocolVersion + 1);
    EXPECT_FALSE(decodePacket(bytes).has_value());
}

TEST(XdjSyncPacketTest, RejectsUnknownType) {
    QByteArray bytes = encodePacket(makeState());
    bytes[5] = static_cast<char>(99);
    EXPECT_FALSE(decodePacket(bytes).has_value());
}

TEST(XdjSyncPacketTest, RejectsEveryTruncation) {
    // Up to the last mandatory field; the appended fields are optional.
    const QByteArray bytes = encodePacket(makeState()).chopped(kOptionalTail);
    for (int size = 0; size < bytes.size(); ++size) {
        EXPECT_FALSE(decodePacket(bytes.left(size)).has_value()) << "size " << size;
    }
}

TEST(XdjSyncPacketTest, IgnoresTrailingBytes) {
    // Lets a later minor revision append fields without a version bump.
    QByteArray bytes = encodePacket(makeState());
    bytes.append("extra");
    EXPECT_TRUE(decodePacket(bytes).has_value());
}

TEST(XdjSyncPacketTest, RejectsTruncatedPlayingFor) {
    QByteArray bytes = encodePacket(makeState());
    bytes.chop(3);
    EXPECT_FALSE(decodePacket(bytes).has_value());
}

// Layout: magic(4) version(1) type(1) idLen(1) id(7) seq(4) flags(1) mode(1)
constexpr int kSyncModeOffset = 4 + 1 + 1 + 1 + 7 + 4 + 1;
constexpr int kBpmOffset = kSyncModeOffset + 1;
constexpr int kBeatDistanceOffset = kBpmOffset + 8;

// NaN/Inf are written as raw bit patterns: Mixxx compiles with -ffast-math,
// where NaN/Inf constants in code are not reliable.
QByteArray withDoubleBits(QByteArray bytes, int offset, quint64 bits) {
    for (int i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<char>((bits >> (8 * i)) & 0xff);
    }
    return bytes;
}

constexpr quint64 kNanBits = 0x7ff8000000000000ULL;
constexpr quint64 kInfBits = 0x7ff0000000000000ULL;

TEST(XdjSyncPacketTest, RejectsNonFiniteNumbers) {
    const QByteArray bytes = encodePacket(makeState());
    EXPECT_FALSE(decodePacket(withDoubleBits(bytes, kBpmOffset, kNanBits)).has_value());
    EXPECT_FALSE(decodePacket(withDoubleBits(bytes, kBpmOffset, kInfBits)).has_value());
    EXPECT_FALSE(decodePacket(withDoubleBits(bytes, kBeatDistanceOffset, kNanBits))
                    .has_value());
    EXPECT_FALSE(decodePacket(withDoubleBits(bytes, kBeatDistanceOffset, kInfBits))
                    .has_value());
}

TEST(XdjSyncPacketTest, RejectsInvalidState) {
    auto state = makeState();
    state.bpm = -1.0;
    EXPECT_FALSE(decodePacket(encodePacket(state)).has_value());

    state = makeState();
    state.playerId = QString();
    EXPECT_FALSE(decodePacket(encodePacket(state)).has_value());

    state = makeState();
    state.playerId = QString(kMaxPlayerIdBytes + 1, QChar('x'));
    EXPECT_FALSE(decodePacket(encodePacket(state)).has_value());
}

TEST(XdjSyncPacketTest, ZeroBpmMeansNoTrackAndIsValid) {
    auto state = makeState();
    state.bpm = 0.0;
    state.playing = false;
    EXPECT_TRUE(decodePacket(encodePacket(state)).has_value());
}

TEST(XdjSyncPacketTest, RejectsUnknownSyncMode) {
    QByteArray bytes = encodePacket(makeState());
    ASSERT_EQ(static_cast<char>(PlayerSyncMode::Master), bytes[kSyncModeOffset]);
    bytes[kSyncModeOffset] = static_cast<char>(42);
    EXPECT_FALSE(decodePacket(bytes).has_value());
}

} // namespace
