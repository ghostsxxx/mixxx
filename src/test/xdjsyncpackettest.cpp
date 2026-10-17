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
}

TEST(XdjSyncPacketTest, StateWithoutPlayingForDecodesAsZero) {
    // Packets from before playingFor was appended (8 bytes at the end).
    QByteArray bytes = encodePacket(makeState());
    bytes.chop(8);
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
    // Up to the last mandatory field; playingFor is optional (see above).
    const QByteArray bytes = encodePacket(makeState()).chopped(8);
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
