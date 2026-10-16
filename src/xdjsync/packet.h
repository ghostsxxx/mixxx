#pragma once

#include <QByteArray>
#include <QString>
#include <optional>
#include <variant>

#include "util/duration.h"

namespace mixxx::xdjsync {

/// Wire format, little-endian:
///   magic "XDJS" (4) | version (u8) | type (u8) | payload
/// Strings are u8 byte length + UTF-8. Times are i64 nanoseconds on the
/// sender's clock. Decoders ignore trailing bytes, so fields may be appended
/// without a version bump; anything else incompatible needs a new version.
constexpr quint8 kProtocolVersion = 1;
constexpr int kMaxPlayerIdBytes = 32;

enum class PlayerSyncMode : quint8 {
    Free = 0,
    Bpm = 1,
    Beat = 2,
    Master = 3,
};

/// Broadcast periodically by every player.
struct StatePacket {
    QString playerId;
    quint32 seq;
    bool playing;
    PlayerSyncMode syncMode;
    /// Current tempo including pitch; 0 if no track or no beatgrid.
    double bpm;
    /// Position within the current beat, 0..1.
    double beatDistance;
    /// Index of the current beat in the track's beatgrid.
    qint64 beatIndex;
    /// When this phase leaves the sender's audio output, sender clock.
    Duration atDac;
};

/// Clock alignment request, unicast to one peer.
struct PingPacket {
    QString playerId;
    quint32 seq;
    /// Sender clock (t1).
    Duration sent;
};

/// Answer to a PingPacket.
struct PongPacket {
    /// The answering player.
    QString playerId;
    /// The player that sent the ping.
    QString toPlayerId;
    quint32 seq;
    /// Echo of PingPacket::sent, pinging player's clock (t1).
    Duration pingSent;
    /// Answering player's clock (t2, t3).
    Duration pingReceived;
    Duration pongSent;
};

using Packet = std::variant<StatePacket, PingPacket, PongPacket>;

QByteArray encodePacket(const Packet& packet);

/// Empty for anything that is not a valid packet of kProtocolVersion.
std::optional<Packet> decodePacket(const QByteArray& bytes);

} // namespace mixxx::xdjsync
