#include "xdjsync/phasefollower.h"

#include <algorithm>
#include <cmath>

#include "xdjsync/tempofollow.h"

namespace mixxx::xdjsync {

SyncAction PhaseFollower::update(PlayerSyncMode ownMode,
        bool localPlaying,
        double localBpm,
        const std::optional<ReferenceStatus>& reference,
        Duration now,
        bool userNudging) {
    Q_UNUSED(now);
    const auto referenceBpm = reference ? std::optional(reference->bpm) : std::nullopt;
    const SyncAction tempoOnly{followTempo(ownMode, localBpm, referenceBpm), std::nullopt};

    // Engaging (D23): SYNC turned on, play started, or a different master.
    // Only then may the phase jump. The same master back after a network
    // drop is caught up by tempo.
    const bool beatMode = ownMode == PlayerSyncMode::Beat;
    const bool otherMaster = reference && reference->playerId != m_lastReferenceId;
    if ((beatMode && !m_wasBeatMode) || (localPlaying && !m_wasPlaying) || otherMaster) {
        m_engagePending = true;
        m_userOffsetBeats = 0.0;
    }
    m_wasBeatMode = beatMode;
    m_wasPlaying = localPlaying;
    if (reference) {
        m_lastReferenceId = reference->playerId;
    }

    // Phase only in BEAT mode, with a measured offset, tempo on both sides,
    // both playing and a well aligned clock.
    if (!beatMode || !reference || !reference->offset || !reference->clock ||
            reference->bpm <= 0.0 || localBpm <= 0.0 || !localPlaying ||
            !reference->playing ||
            reference->clock->uncertainty.toDoubleMillis() > kMaxClockUncertaintyMs) {
        m_wasNudging = false;
        return tempoOnly;
    }
    const BeatOffset& offset = *reference->offset;

    // The DJ nudges: hands off the phase, then keep where they left it.
    if (userNudging) {
        m_wasNudging = true;
        return SyncAction{std::optional(reference->bpm), std::nullopt};
    }
    if (m_wasNudging) {
        m_wasNudging = false;
        m_userOffsetBeats = offset.offsetBeats;
    }

    if (m_engagePending) {
        m_engagePending = false;
        // With bar alignment the target is the reference's beat in the bar.
        const bool barKnown = m_alignBars && offset.barOffsetBeats.has_value();
        const bool wrongBeatInBar = barKnown && offset.beatsOffInBar != 0;
        if (wrongBeatInBar || std::abs(offset.offsetMs) >= kDeadbandMs) {
            return SyncAction{tempoOnly.targetBpm,
                    barKnown ? -*offset.barOffsetBeats : -offset.offsetBeats};
        }
    }

    const double msPerBeat = 60000.0 / reference->bpm;
    const double errorBeats = wrapHalfBeat(offset.offsetBeats - m_userOffsetBeats);
    const double errorMs = errorBeats * msPerBeat;
    if (std::abs(errorMs) < kDeadbandMs) {
        return tempoOnly;
    }
    // Ahead (positive) -> slower: d(error)/dt = trim, time constant
    // kSettleSeconds; way off -> full trim to catch up.
    const double trim = std::abs(errorBeats) > kTrainWreckBeats
            ? (errorBeats > 0 ? -kMaxTrim : kMaxTrim)
            : std::clamp(-errorMs / 1000.0 / kSettleSeconds, -kMaxTrim, kMaxTrim);
    const double targetBpm = reference->bpm * (1.0 + trim);
    if (std::abs(targetBpm - localBpm) < kTempoTolerance) {
        return SyncAction{};
    }
    return SyncAction{targetBpm, std::nullopt};
}

} // namespace mixxx::xdjsync
