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
    /// Index of the current beat counted from the grid's first downbeat
    /// (4/4 bars); empty if unknown.
    std::optional<qint64> beatIndex{};
};

/// Phase difference of a local player against a reference player.
struct BeatOffset {
    /// Positive: local beat comes before the reference beat (local is ahead).
    double offsetMs;
    /// Same as offsetMs in beats, [-0.5, 0.5).
    double offsetBeats;
    /// local bpm - reference bpm.
    double bpmDelta;
    /// True if the tempo difference makes the offset change noticeably.
    bool drifting;
    /// How fast offsetMs grows per second because of bpmDelta.
    double driftMsPerSecond;
    /// Offset within the 4/4 bar in beats, [-2, 2): the whole beats this
    /// player is off in the bar plus offsetBeats. Empty without beat indices.
    std::optional<double> barOffsetBeats{};
    /// Whole beats off in the bar (-2..1), 0 if unknown.
    int beatsOffInBar = 0;
};

/// Below this tempo difference the offset is treated as stable.
/// 0.01 BPM at 174 BPM drifts ~0.06 ms per second.
constexpr double kDriftBpmThreshold = 0.01;

/// Offsets up to this are shown as "in sync": well below an audible flam
/// (~10 ms), with margin for the display accuracy.
constexpr double kInSyncMs = 5.0;

/// Beat within its 4/4 bar, 1..4.
int beatInBar(qint64 beatIndex);

/// Beats into the 4/4 bar at `now`, [0, 4); empty without beat index.
std::optional<double> barPosition(const PhaseSample& sample, Duration now);

/// Wraps a phase difference in beats into [-0.5, 0.5).
double wrapHalfBeat(double beats);

/// Phase of `sample` at time `now`, in beats (not wrapped).
double projectPhase(const PhaseSample& sample, Duration now);

/// Offset of `local` against `reference`, both projected to `now`.
/// Empty if either tempo is unknown.
std::optional<BeatOffset> beatOffset(
        const PhaseSample& local, const PhaseSample& reference, Duration now);

/// Within kInSyncMs, on the same beat of the bar (if known) and not
/// drifting apart. `ignoreDrift` when the tempo difference is a deliberate
/// phase trim (BEAT mode).
bool inSync(const BeatOffset& offset, bool ignoreDrift = false);

} // namespace mixxx::xdjsync
