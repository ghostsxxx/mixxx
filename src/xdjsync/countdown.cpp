#include "xdjsync/countdown.h"

#include <cmath>

namespace mixxx::xdjsync {

namespace {

// Float noise must not turn "exactly 64 beats" into 65.
constexpr double kEpsilonBeats = 1e-6;

} // namespace

CueCountdown CueCountdown::fromBeats(double remainingBeats) {
    const int wholeBeats = static_cast<int>(std::ceil(remainingBeats - kEpsilonBeats));
    return CueCountdown{remainingBeats, wholeBeats / 4, wholeBeats % 4};
}

std::optional<CueCountdown> nextCueCountdown(
        double beatPosition, const QList<double>& cueBeatPositions) {
    std::optional<double> next;
    for (const double cue : cueBeatPositions) {
        if (cue > beatPosition + kEpsilonBeats && (!next || cue < *next)) {
            next = cue;
        }
    }
    if (!next) {
        return std::nullopt;
    }
    return CueCountdown::fromBeats(*next - beatPosition);
}

} // namespace mixxx::xdjsync
