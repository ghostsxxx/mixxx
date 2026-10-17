#include "xdjsync/lanegeometry.h"

#include <QMutex>
#include <algorithm>
#include <cmath>

#include "xdjsync/countdown.h"

namespace mixxx::xdjsync {

namespace {

constexpr double kSecondsPerMinute = 60.0;
constexpr int kBeatsPerBar = 4;
/// Cues this far behind stay on peers' lanes while they pass the playhead.
constexpr double kUpcomingCuesBehindBeats = 16.0;

} // namespace

double beatPositionAt(const LaneState& state, Duration now) {
    if (!state.playing) {
        return state.beatPosition;
    }
    return state.beatPosition + (now - state.at).toDoubleSeconds() * state.bpm / kSecondsPerMinute;
}

LaneView laneView(const LaneState& state,
        Duration now,
        double center,
        double width,
        double pxPerSecond) {
    LaneView view;
    if (state.bpm <= 0.0 || pxPerSecond <= 0.0) {
        return view;
    }
    const double position = beatPositionAt(state, now);
    const double pxPerBeat = pxPerSecond * kSecondsPerMinute / state.bpm;
    const auto xOf = [&](double beat) {
        return center + (beat - position) * pxPerBeat;
    };
    const int first = static_cast<int>(std::floor(position - center / pxPerBeat));
    const int last = static_cast<int>(std::ceil(position + (width - center) / pxPerBeat));
    for (int beat = first; beat <= last; ++beat) {
        const double x = xOf(beat);
        if (x < 0.0 || x > width) {
            continue;
        }
        const bool bar = ((beat % kBeatsPerBar) + kBeatsPerBar) % kBeatsPerBar == 0;
        const int barNumber = static_cast<int>(
                                      std::floor(static_cast<double>(beat) / kBeatsPerBar)) +
                1;
        view.ticks.append(LaneTick{x, bar, bar ? barNumber : 0});
    }
    for (const LaneCue& cue : state.cues) {
        const double x = xOf(cue.beat);
        if (x >= 0.0 && x <= width) {
            view.cues.append(LaneCueMark{x, hotcueLabel(cue.number)});
        }
    }
    return view;
}

std::optional<double> beatsToNextCue(const LaneState& state, Duration now) {
    QList<double> cueBeats;
    for (const LaneCue& cue : state.cues) {
        cueBeats.append(cue.beat);
    }
    const auto countdown = nextCueCountdown(beatPositionAt(state, now), cueBeats);
    return countdown ? std::optional(countdown->beats) : std::nullopt;
}

QString formatCountdown(std::optional<double> beats) {
    if (!beats) {
        return QStringLiteral("--.-");
    }
    const CueCountdown countdown = CueCountdown::fromBeats(*beats);
    return QStringLiteral("%1.%2")
            .arg(countdown.bars, 2, 10, QLatin1Char('0'))
            .arg(countdown.beat);
}

QString hotcueLabel(int number) {
    if (number < 0 || number >= 26) {
        return QString::number(number + 1);
    }
    return QString(QChar('A' + number));
}

QList<UpcomingCue> upcomingCues(const QList<LaneCue>& cues, double position) {
    QList<LaneCue> nearby;
    for (const LaneCue& cue : cues) {
        if (cue.beat >= position - kUpcomingCuesBehindBeats) {
            nearby.append(cue);
        }
    }
    std::sort(nearby.begin(), nearby.end(), [](const LaneCue& a, const LaneCue& b) {
        return a.beat < b.beat;
    });
    QList<UpcomingCue> upcoming;
    for (const LaneCue& cue : nearby.mid(0, kMaxUpcomingCues)) {
        upcoming.append(UpcomingCue{static_cast<quint8>(cue.number), cue.beat - position});
    }
    return upcoming;
}

namespace {

QMutex s_lanesMutex;
LanesSnapshot s_lanes;

} // namespace

void publishLanes(const LanesSnapshot& snapshot) {
    const QMutexLocker locker(&s_lanesMutex);
    s_lanes = snapshot;
}

LanesSnapshot latestLanes() {
    const QMutexLocker locker(&s_lanesMutex);
    return s_lanes;
}

} // namespace mixxx::xdjsync
