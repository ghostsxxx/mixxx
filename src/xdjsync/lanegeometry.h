#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "util/duration.h"
#include "xdjsync/packet.h"

namespace mixxx::xdjsync {

/// A hot cue on a lane, in that deck's beats from its first downbeat.
struct LaneCue {
    double beat;
    /// Mixxx hot cue index, 0-based (0 = "A").
    int number;
};

/// One deck as drawn on a beat lane.
struct LaneState {
    bool playing = false;
    double bpm = 0.0;
    /// Beats from the deck's first downbeat at `at` (local clock).
    double beatPosition = 0.0;
    Duration at;
    QList<LaneCue> cues;
};

struct LaneTick {
    double x;
    /// Beat 1 of a 4/4 bar.
    bool bar;
    /// 1-based bar number (bar ticks only).
    int barNumber;
};

struct LaneCueMark {
    double x;
    QString label;
};

struct LaneView {
    QList<LaneTick> ticks;
    QList<LaneCueMark> cues;
};

/// Beat position of `state` at `now`.
double beatPositionAt(const LaneState& state, Duration now);

/// Ticks and cue marks for a lane of `width` px with the playhead at
/// `center`. Lanes share a time axis (`pxPerSecond`), so a tempo or phase
/// difference between two lanes shows as their ticks not lining up.
LaneView laneView(const LaneState& state,
        Duration now,
        double center,
        double width,
        double pxPerSecond);

std::optional<double> beatsToNextCue(const LaneState& state, Duration now);

/// "15.2" (bars.beat, bars padded to two digits) or "--.-".
QString formatCountdown(std::optional<double> beats);

/// "A" for hot cue 0, "B" for 1, ...
QString hotcueLabel(int number);

/// Cues to send to peers for their lanes: from 16 beats behind `position`
/// on, nearest first, at most kMaxUpcomingCues.
QList<UpcomingCue> upcomingCues(const QList<LaneCue>& cues, double position);

/// What the beat lanes widget draws; written by the network thread, read by
/// the GUI.
struct LanesSnapshot {
    std::optional<LaneState> own;
    std::optional<LaneState> reference;
    bool isMaster = false;
};

void publishLanes(const LanesSnapshot& snapshot);
LanesSnapshot latestLanes();

} // namespace mixxx::xdjsync
