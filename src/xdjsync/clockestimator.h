#pragma once

#include <QList>
#include <optional>

#include "util/duration.h"

namespace mixxx::xdjsync {

/// One ping/pong exchange (NTP-style timestamps).
struct ClockSample {
    /// t1, local clock.
    Duration pingSent;
    /// t2, remote clock.
    Duration pingReceived;
    /// t3, remote clock.
    Duration pongSent;
    /// t4, local clock.
    Duration pongReceived;
};

struct ClockEstimate {
    /// remote clock - local clock.
    Duration offset;
    /// Network round trip of the sample the offset is taken from.
    Duration roundTrip;
    /// Worst-case error of offset (half the round trip).
    Duration uncertainty;

    Duration toLocal(Duration remoteTime) const {
        return remoteTime - offset;
    }
};

/// Estimates the offset of a remote player's clock from ping/pong exchanges.
///
/// Keeps the last kDefaultWindow samples and uses the one with the lowest
/// round trip: it is the least affected by queueing delay and asymmetry.
/// With pings every 250 ms the window spans 2 s, short enough that crystal
/// drift (< 100 ppm, i.e. < 0.2 ms over 2 s) can be ignored.
class ClockEstimator {
  public:
    static constexpr int kDefaultWindow = 8;
    static constexpr int kMinSamples = 3;

    explicit ClockEstimator(int window = kDefaultWindow);

    /// Returns false and ignores the sample if its timestamps are impossible.
    bool addSample(const ClockSample& sample);

    std::optional<ClockEstimate> estimate() const;

    void reset();

  private:
    int m_window;
    QList<ClockSample> m_samples;
};

} // namespace mixxx::xdjsync
