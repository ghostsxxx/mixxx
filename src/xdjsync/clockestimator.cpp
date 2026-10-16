#include "xdjsync/clockestimator.h"

#include <algorithm>

namespace mixxx::xdjsync {

namespace {

Duration half(Duration duration) {
    return Duration::fromNanos(duration.toIntegerNanos() / 2);
}

Duration roundTrip(const ClockSample& sample) {
    return (sample.pongReceived - sample.pingSent) -
            (sample.pongSent - sample.pingReceived);
}

} // namespace

ClockEstimator::ClockEstimator(int window)
        : m_window(window) {
}

bool ClockEstimator::addSample(const ClockSample& sample) {
    const Duration zero;
    if (sample.pongReceived < sample.pingSent ||
            sample.pongSent < sample.pingReceived ||
            roundTrip(sample) < zero) {
        return false;
    }
    m_samples.append(sample);
    while (m_samples.size() > m_window) {
        m_samples.removeFirst();
    }
    return true;
}

std::optional<ClockEstimate> ClockEstimator::estimate() const {
    if (m_samples.size() < kMinSamples) {
        return std::nullopt;
    }
    const auto best = std::min_element(m_samples.cbegin(),
            m_samples.cend(),
            [](const ClockSample& a, const ClockSample& b) {
                return roundTrip(a) < roundTrip(b);
            });
    const Duration rtt = roundTrip(*best);
    const Duration offset = half((best->pingReceived - best->pingSent) +
            (best->pongSent - best->pongReceived));
    return ClockEstimate{offset, rtt, half(rtt)};
}

void ClockEstimator::reset() {
    m_samples.clear();
}

} // namespace mixxx::xdjsync
