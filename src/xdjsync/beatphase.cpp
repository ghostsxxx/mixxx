#include "xdjsync/beatphase.h"

#include <cmath>

namespace mixxx::xdjsync {

namespace {

constexpr double kSecondsPerMinute = 60.0;
constexpr double kMsPerMinute = 60000.0;
constexpr int kBeatsPerBar = 4;

/// Wraps a difference in beats within a 4/4 bar into [-2, 2).
double wrapHalfBar(double beats) {
    return beats - kBeatsPerBar * std::floor((beats + kBeatsPerBar / 2.0) / kBeatsPerBar);
}

/// Beats into the bar (0..4, not wrapped beyond the projection).
double barBeats(const PhaseSample& sample, Duration now) {
    return (beatInBar(*sample.beatIndex) - 1) + projectPhase(sample, now);
}

} // namespace

int beatInBar(qint64 beatIndex) {
    return static_cast<int>(((beatIndex % kBeatsPerBar) + kBeatsPerBar) % kBeatsPerBar) + 1;
}

std::optional<double> barPosition(const PhaseSample& sample, Duration now) {
    if (!sample.beatIndex) {
        return std::nullopt;
    }
    const double beats = barBeats(sample, now);
    return beats - kBeatsPerBar * std::floor(beats / kBeatsPerBar);
}

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
    BeatOffset offset{
            offsetBeats * msPerBeat,
            offsetBeats,
            bpmDelta,
            std::abs(bpmDelta) >= kDriftBpmThreshold,
            bpmDelta / kSecondsPerMinute * msPerBeat,
    };
    if (local.beatIndex && reference.beatIndex) {
        const double barOffset = wrapHalfBar(barBeats(local, now) - barBeats(reference, now));
        offset.barOffsetBeats = barOffset;
        offset.beatsOffInBar = static_cast<int>(std::lround(barOffset - offsetBeats));
    }
    return offset;
}

bool inSync(const BeatOffset& offset, bool ignoreDrift) {
    return (ignoreDrift || !offset.drifting) && offset.beatsOffInBar == 0 &&
            std::abs(offset.offsetMs) <= kInSyncMs;
}

} // namespace mixxx::xdjsync
