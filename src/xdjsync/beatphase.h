#pragma once

#include <optional>

#include "util/duration.h"

namespace mixxx::xdjsync {

/// Beat phase of one player at the moment it is heard at the audio output.
struct PhaseSample {
    bool playing;
    /// Current tempo including pitch.
    double bpm;
    /// Position within the current beat, 0..1.
    double beatDistance;
    /// Time (on the receiver's clock) when this phase leaves the DAC.
    Duration atDac;
};

/// Phase difference of a local player against a reference player.
struct BeatOffset {
    /// Positive: local beat comes before the reference beat (local is ahead).
    double offsetMs;
    /// local bpm - reference bpm.
    double bpmDelta;
    /// True if the tempo difference makes the offset change noticeably.
    bool drifting;
    /// How fast offsetMs grows per second because of bpmDelta.
    double driftMsPerSecond;
};

/// Below this tempo difference the offset is treated as stable.
/// 0.01 BPM at 174 BPM drifts ~0.06 ms per second.
constexpr double kDriftBpmThreshold = 0.01;

/// Wraps a phase difference in beats into [-0.5, 0.5).
double wrapHalfBeat(double beats);

/// Phase of `sample` at time `now`, in beats (not wrapped).
double projectPhase(const PhaseSample& sample, Duration now);

/// Offset of `local` against `reference`, both projected to `now`.
/// Empty if either tempo is unknown.
std::optional<BeatOffset> beatOffset(
        const PhaseSample& local, const PhaseSample& reference, Duration now);

} // namespace mixxx::xdjsync
