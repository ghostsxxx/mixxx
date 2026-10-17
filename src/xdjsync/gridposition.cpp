#include "xdjsync/gridposition.h"

#include <cmath>

namespace mixxx::xdjsync {

double beatPosition(const Beats& beats, audio::FramePos position) {
    const audio::FramePos downbeat = *beats.cfirstmarker();
    if (beats.hasConstantTempo()) {
        const double framesPerBeat =
                beats.getSampleRate().toDouble() * 60.0 / beats.getLastMarkerBpm().value();
        return (position - downbeat) / framesPerBeat;
    }
    // Variable tempo: count whole beats, interpolate within the beat.
    audio::FramePos prevBeat;
    audio::FramePos nextBeat;
    if (!beats.findPrevNextBeats(position, &prevBeat, &nextBeat, false)) {
        return 0.0;
    }
    const int wholeBeats = prevBeat >= downbeat
            ? beats.numBeatsInRange(downbeat, prevBeat)
            : -beats.numBeatsInRange(prevBeat, downbeat);
    return wholeBeats + (position - prevBeat) / (nextBeat - prevBeat);
}

std::optional<BeatsPointer> withDownbeatAt(const Beats& beats, audio::FramePos position) {
    const qint64 nearestBeat = std::llround(beatPosition(beats, position));
    // Moving the anchor by whole beats keeps every beat where it is.
    const qint64 shift = ((nearestBeat % 4) + 4) % 4;
    return beats.tryTranslateBeats(static_cast<double>(shift));
}

} // namespace mixxx::xdjsync
