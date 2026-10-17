#pragma once

#include <QList>
#include <optional>

namespace mixxx::xdjsync {

/// Beats left until a cue, shown CDJ-style as bars.beat.
struct CueCountdown {
    double beats;
    /// Remaining whole beats (rounded up) = bars * 4 + beat, beat 1..4
    /// counting down within the bar (CDJ-style; 64 beats = 15.4).
    int bars;
    int beat;

    static CueCountdown fromBeats(double remainingBeats);
};

/// Countdown from `beatPosition` to the next cue strictly ahead; cue and play
/// positions in beats from the grid's first downbeat. Empty if none ahead.
std::optional<CueCountdown> nextCueCountdown(
        double beatPosition, const QList<double>& cueBeatPositions);

} // namespace mixxx::xdjsync
