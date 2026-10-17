#include "xdjsync/peertable.h"

#include <QList>
#include <algorithm>
#include <cmath>

namespace mixxx::xdjsync {

namespace {

/// `seq` came before `lastSeq`; a big step back is a restarted sender.
bool isOlder(quint32 seq, quint32 lastSeq) {
    constexpr quint32 kRestartGap = 100;
    return seq <= lastSeq && lastSeq - seq < kRestartGap;
}

double positionBeats(const StatePacket& state) {
    return static_cast<double>(state.beatIndex) + state.beatDistance;
}

/// `next` is not where `previous` would have played to: cue, beat jump or
/// new track. Both on the sender's clock.
bool isPositionJump(const StatePacket& previous, const StatePacket& next) {
    const double elapsedBeats = previous.playing
            ? (next.atDac - previous.atDac).toDoubleSeconds() * previous.bpm / 60.0
            : 0.0;
    const double expected = positionBeats(previous) + elapsedBeats;
    return std::abs(positionBeats(next) - expected) > PeerTable::kPositionJumpBeats;
}

} // namespace

PeerTable::PeerTable(QString ownPlayerId)
        : m_ownPlayerId(std::move(ownPlayerId)) {
}

std::optional<PongPacket> PeerTable::handlePacket(const Packet& packet, Duration receivedAt) {
    if (const auto* pState = std::get_if<StatePacket>(&packet)) {
        if (pState->playerId != m_ownPlayerId) {
            Peer& peer = touch(pState->playerId, receivedAt);
            if (peer.state && isOlder(pState->seq, peer.state->seq)) {
                return std::nullopt; // reordered on the network
            }
            if (pState->beatIndexValid) {
                if (peer.lastPositioned && isPositionJump(*peer.lastPositioned, *pState)) {
                    ++peer.positionJumps;
                }
                peer.lastPositioned = *pState;
            }
            peer.state = *pState;
            peer.stateReceivedAt = receivedAt;
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

std::optional<QString> PeerTable::masterPlayerId(
        const PhaseSample& local, Duration localPlayingFor, Duration now) const {
    struct Candidate {
        QString playerId;
        Duration startedAt; // local clock
    };
    QList<Candidate> candidates;
    if (local.playing && local.bpm > 0.0) {
        candidates.append({m_ownPlayerId, now - localPlayingFor});
    }
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it->state && it->state->playing && it->state->bpm > 0.0) {
            candidates.append({it.key(), it->stateReceivedAt - it->state->playingFor});
        }
    }
    if (candidates.isEmpty()) {
        return std::nullopt;
    }
    Duration earliest = candidates.first().startedAt;
    for (const Candidate& candidate : std::as_const(candidates)) {
        earliest = std::min(earliest, candidate.startedAt);
    }
    std::optional<QString> master;
    for (const Candidate& candidate : std::as_const(candidates)) {
        if (candidate.startedAt < earliest + kMasterTieWindow &&
                (!master || candidate.playerId < *master)) {
            master = candidate.playerId;
        }
    }
    return master;
}

std::optional<ReferenceStatus> PeerTable::reference(
        const PhaseSample& local, Duration localPlayingFor, Duration now) const {
    const auto playerId = masterPlayerId(local, localPlayingFor, now);
    if (!playerId || *playerId == m_ownPlayerId) {
        return std::nullopt;
    }
    const Peer& peer = *m_peers.constFind(*playerId);
    const StatePacket& state = *peer.state;
    ReferenceStatus status{
            *playerId, state.playing, state.bpm, peer.clock.estimate(), std::nullopt};
    status.positionJumps = peer.positionJumps;
    if (status.clock) {
        const PhaseSample referencePhase{
                state.playing,
                state.bpm,
                state.beatDistance,
                status.clock->toLocal(state.atDac),
                state.beatIndexValid ? std::optional(state.beatIndex) : std::nullopt,
        };
        status.offset = beatOffset(local, referencePhase, now);
        status.barPosition = barPosition(referencePhase, now);
        if (state.beatIndexValid && state.bpm > 0.0) {
            LaneState lane;
            lane.playing = state.playing;
            lane.bpm = state.bpm;
            lane.beatPosition = static_cast<double>(state.beatIndex) + state.beatDistance;
            lane.at = referencePhase.atDac;
            for (const UpcomingCue& cue : state.upcomingCues) {
                lane.cues.append(LaneCue{lane.beatPosition + cue.beatsAhead, cue.number});
            }
            status.lane = lane;
        }
        if (state.cueCountdownBeats) {
            // The countdown runs down with the reference's play position.
            const double beatsSinceSample =
                    projectPhase(referencePhase, now) - referencePhase.beatDistance;
            status.cueCountdownBeats =
                    std::max(0.0, *state.cueCountdownBeats - beatsSinceSample);
        }
    }
    return status;
}

PeerTable::Peer& PeerTable::touch(const QString& playerId, Duration receivedAt) {
    Peer& peer = m_peers[playerId];
    peer.lastSeen = receivedAt;
    return peer;
}

} // namespace mixxx::xdjsync
