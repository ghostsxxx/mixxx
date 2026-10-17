#pragma once

#include <QByteArray>
#include <QList>
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
/// Upcoming hot cues per state packet.
constexpr int kMaxUpcomingCues = 8;

enum class PlayerSyncMode : quint8 {
    Free = 0,
    Bpm = 1,
    Beat = 2,
    Master = 3,
};

/// A hot cue near the play position, for the beat lanes.
struct UpcomingCue {
    /// Mixxx hot cue index, 0-based.
    quint8 number;
    /// Beats from the play position at atDac; negative = just passed.
    double beatsAhead;
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
    /// How long the deck has been playing without a stop; 0 if stopped.
    /// Used for the automatic master (D18). Optional on the wire (appended).
    Duration playingFor{};
    /// beatIndex counts from the grid's first downbeat (bar position known).
    bool beatIndexValid = false;
    /// Beats from atDac to the next cue; empty if none. Optional on the wire
    /// (appended).
    std::optional<double> cueCountdownBeats{};
    /// Hot cues from a little behind to ahead of the play position, at most
    /// kMaxUpcomingCues. Optional on the wire (appended).
    QList<UpcomingCue> upcomingCues{};
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
