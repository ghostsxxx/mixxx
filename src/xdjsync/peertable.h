#pragma once

#include <QMap>
#include <QString>
#include <optional>

#include "util/duration.h"
#include "xdjsync/beatphase.h"
#include "xdjsync/clockestimator.h"
#include "xdjsync/lanegeometry.h"
#include "xdjsync/packet.h"

namespace mixxx::xdjsync {

/// What this player shows about its reference player.
struct ReferenceStatus {
    QString playerId;
    bool playing;
    /// Reference tempo including pitch.
    double bpm;
    /// Empty until the clock is aligned with the reference.
    std::optional<ClockEstimate> clock;
    /// Empty without clock alignment or without a valid tempo on both sides.
    std::optional<BeatOffset> offset;
    /// Reference's beats into its bar now, [0, 4); empty without clock
    /// alignment or bar position.
    std::optional<double> barPosition{};
    /// Reference's beats to its next cue, now; empty if none or unknown.
    std::optional<double> cueCountdownBeats{};
    /// For the beat lanes, on the local clock; empty without clock alignment
    /// or bar position.
    std::optional<LaneState> lane{};
    /// How often the reference's play position jumped (cue, beat jump, new
    /// track) since it was first seen.
    int positionJumps = 0;
};

/// Known players on the network, without any I/O. All times are on the
/// local clock and passed in, so this is fully deterministic.
class PeerTable {
  public:
    /// A peer that sent nothing for this long is forgotten.
    static constexpr Duration kPeerTimeout = Duration::fromMillis(1000);

    explicit PeerTable(QString ownPlayerId);

    /// Returns the pong to broadcast if `packet` is a ping from a peer.
    /// The caller sets PongPacket::pongSent right before sending.
    std::optional<PongPacket> handlePacket(const Packet& packet, Duration receivedAt);

    void expire(Duration now);

    /// Peers that have sent their state.
    int peerCount() const;

    /// Starts closer than this count as simultaneous (network delay makes
    /// players see each other's start times a few ms apart).
    static constexpr Duration kMasterTieWindow = Duration::fromMillis(100);

    /// A peer's position differing from its projected previous position by
    /// more than this (beats) counts as a jump (cue, beat jump, new track).
    static constexpr double kPositionJumpBeats = 0.25;

    /// D18: the player (peer or this one) that has been playing longest with
    /// a known tempo; among near-simultaneous starts the lowest id. Every
    /// player computes the same answer from the shared state.
    std::optional<QString> masterPlayerId(
            const PhaseSample& local, Duration localPlayingFor, Duration now) const;

    /// The master, unless that is this player.
    std::optional<ReferenceStatus> reference(
            const PhaseSample& local, Duration localPlayingFor, Duration now) const;

  private:
    struct Peer {
        std::optional<StatePacket> state;
        /// When the state arrived, to place playingFor on the local clock.
        Duration stateReceivedAt;
        int positionJumps = 0;
        /// Last state with a known bar position, to detect position jumps.
        std::optional<StatePacket> lastPositioned;
        ClockEstimator clock;
        Duration lastSeen;
    };

    Peer& touch(const QString& playerId, Duration receivedAt);

    const QString m_ownPlayerId;
    QMap<QString, Peer> m_peers;
};

} // namespace mixxx::xdjsync
