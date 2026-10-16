#include "xdjsync/peertable.h"

namespace mixxx::xdjsync {

PeerTable::PeerTable(QString ownPlayerId)
        : m_ownPlayerId(std::move(ownPlayerId)) {
}

std::optional<PongPacket> PeerTable::handlePacket(const Packet& packet, Duration receivedAt) {
    if (const auto* pState = std::get_if<StatePacket>(&packet)) {
        if (pState->playerId != m_ownPlayerId) {
            touch(pState->playerId, receivedAt).state = *pState;
        }
        return std::nullopt;
    }
    if (const auto* pPing = std::get_if<PingPacket>(&packet)) {
        if (pPing->playerId == m_ownPlayerId) {
            return std::nullopt;
        }
        touch(pPing->playerId, receivedAt);
        return PongPacket{
                m_ownPlayerId,
                pPing->playerId,
                pPing->seq,
                pPing->sent,
                receivedAt,
                receivedAt,
        };
    }
    if (const auto* pPong = std::get_if<PongPacket>(&packet)) {
        if (pPong->toPlayerId != m_ownPlayerId || pPong->playerId == m_ownPlayerId) {
            return std::nullopt;
        }
        touch(pPong->playerId, receivedAt)
                .clock.addSample(ClockSample{
                        pPong->pingSent,
                        pPong->pingReceived,
                        pPong->pongSent,
                        receivedAt,
                });
    }
    return std::nullopt;
}

void PeerTable::expire(Duration now) {
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        if (now - it->lastSeen > kPeerTimeout) {
            it = m_peers.erase(it);
        } else {
            ++it;
        }
    }
}

int PeerTable::peerCount() const {
    int count = 0;
    for (const Peer& peer : m_peers) {
        if (peer.state) {
            ++count;
        }
    }
    return count;
}

std::optional<QString> PeerTable::referencePlayerId(PlayerSyncMode ownMode) const {
    if (ownMode == PlayerSyncMode::Master) {
        return std::nullopt;
    }
    // QMap iterates sorted by id, so the choice is deterministic on all
    // players if two peers claim master at the same time.
    std::optional<QString> onlyPlaying;
    int playingCount = 0;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (!it->state) {
            continue;
        }
        if (it->state->syncMode == PlayerSyncMode::Master) {
            return it.key();
        }
        if (it->state->playing) {
            ++playingCount;
            onlyPlaying = it.key();
        }
    }
    if (playingCount == 1) {
        return onlyPlaying;
    }
    return std::nullopt;
}

std::optional<ReferenceStatus> PeerTable::reference(
        const PhaseSample& local, PlayerSyncMode ownMode, Duration now) const {
    const auto playerId = referencePlayerId(ownMode);
    if (!playerId) {
        return std::nullopt;
    }
    const Peer& peer = *m_peers.constFind(*playerId);
    const StatePacket& state = *peer.state;
    ReferenceStatus status{*playerId, state.bpm, peer.clock.estimate(), std::nullopt};
    if (status.clock) {
        const PhaseSample referencePhase{
                state.playing,
                state.bpm,
                state.beatDistance,
                status.clock->toLocal(state.atDac),
        };
        status.offset = beatOffset(local, referencePhase, now);
    }
    return status;
}

PeerTable::Peer& PeerTable::touch(const QString& playerId, Duration receivedAt) {
    Peer& peer = m_peers[playerId];
    peer.lastSeen = receivedAt;
    return peer;
}

} // namespace mixxx::xdjsync
