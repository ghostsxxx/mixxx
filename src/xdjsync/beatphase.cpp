#include "xdjsync/beatphase.h"

#include <cmath>

namespace mixxx::xdjsync {

namespace {

constexpr double kSecondsPerMinute = 60.0;
constexpr double kMsPerMinute = 60000.0;

} // namespace

double wrapHalfBeat(double beats) {
    return beats - std::floor(beats + 0.5);
}

double projectPhase(const PhaseSample& sample, Duration now) {
    if (!sample.playing) {
        return sample.beatDistance;
    }
    const double elapsedSeconds = (now - sample.atDac).toDoubleSeconds();
    return sample.beatDistance + elapsedSeconds * sample.bpm / kSecondsPerMinute;
}

std::optional<BeatOffset> beatOffset(
        const PhaseSample& local, const PhaseSample& reference, Duration now) {
    if (local.bpm <= 0.0 || reference.bpm <= 0.0) {
        return std::nullopt;
    }
    const double msPerBeat = kMsPerMinute / local.bpm;
    const double offsetBeats = wrapHalfBeat(
            projectPhase(local, now) - projectPhase(reference, now));
    const double bpmDelta = local.bpm - reference.bpm;
    return BeatOffset{
            offsetBeats * msPerBeat,
            offsetBeats,
            bpmDelta,
            std::abs(bpmDelta) >= kDriftBpmThreshold,
            bpmDelta / kSecondsPerMinute * msPerBeat,
    };
}

bool inSync(const BeatOffset& offset) {
    return !offset.drifting && std::abs(offset.offsetMs) <= kInSyncMs;
}

} // namespace mixxx::xdjsync
