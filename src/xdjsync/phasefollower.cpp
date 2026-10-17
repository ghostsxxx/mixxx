#include "xdjsync/phasefollower.h"

#include <algorithm>
#include <cmath>

#include "xdjsync/tempofollow.h"

namespace mixxx::xdjsync {

SyncAction PhaseFollower::update(PlayerSyncMode ownMode,
        bool localPlaying,
        double localBpm,
        const std::optional<ReferenceStatus>& reference,
        Duration now) {
    const auto referenceBpm = reference ? std::optional(reference->bpm) : std::nullopt;
    const SyncAction tempoOnly{followTempo(ownMode, localBpm, referenceBpm), std::nullopt};
    // Phase only in BEAT mode, with a measured offset, tempo on both sides,
    // both playing and a well aligned clock.
    if (ownMode != PlayerSyncMode::Beat || !reference || !reference->offset ||
            !reference->clock || reference->bpm <= 0.0 || localBpm <= 0.0 ||
            !localPlaying || !reference->playing ||
            reference->clock->uncertainty.toDoubleMillis() > kMaxClockUncertaintyMs) {
        return tempoOnly;
    }

    const double offsetMs = reference->offset->offsetMs;
    if (std::abs(offsetMs) > kSnapMs) {
        if (m_lastSnap && now - *m_lastSnap < kSnapCooldown) {
            return tempoOnly;
        }
        m_lastSnap = now;
        return SyncAction{tempoOnly.targetBpm, -reference->offset->offsetBeats};
    }
    if (std::abs(offsetMs) < kDeadbandMs) {
        return tempoOnly;
    }
    // Ahead (positive) -> slower. Removes the offset with time constant
    // kSettleSeconds: d(offset)/dt = trim.
    const double trim = std::clamp(-offsetMs / 1000.0 / kSettleSeconds, -kMaxTrim, kMaxTrim);
    const double targetBpm = reference->bpm * (1.0 + trim);
    if (std::abs(targetBpm - localBpm) < kTempoTolerance) {
        return SyncAction{};
    }
    return SyncAction{targetBpm, std::nullopt};
}

} // namespace mixxx::xdjsync
