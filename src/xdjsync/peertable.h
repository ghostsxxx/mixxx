#pragma once

#include <QMap>
#include <QString>
#include <optional>

#include "util/duration.h"
#include "xdjsync/beatphase.h"
#include "xdjsync/clockestimator.h"
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
        ClockEstimator clock;
        Duration lastSeen;
    };

    Peer& touch(const QString& playerId, Duration receivedAt);

    const QString m_ownPlayerId;
    QMap<QString, Peer> m_peers;
};

} // namespace mixxx::xdjsync
