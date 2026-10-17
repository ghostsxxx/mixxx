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

    /// D7: the master peer; else the only playing peer; else none.
    /// None if this player is master itself.
    std::optional<QString> referencePlayerId(PlayerSyncMode ownMode) const;

    std::optional<ReferenceStatus> reference(
            const PhaseSample& local, PlayerSyncMode ownMode, Duration now) const;

  private:
    struct Peer {
        std::optional<StatePacket> state;
        ClockEstimator clock;
        Duration lastSeen;
    };

    Peer& touch(const QString& playerId, Duration receivedAt);

    QString m_ownPlayerId;
    QMap<QString, Peer> m_peers;
};

} // namespace mixxx::xdjsync
