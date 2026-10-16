#include <gtest/gtest.h>

#include "xdjsync/clockestimator.h"

namespace {

using mixxx::Duration;
using mixxx::xdjsync::ClockEstimator;
using mixxx::xdjsync::ClockSample;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

/// One ping/pong exchange against a remote clock that is `remoteAhead`
/// ahead of the local one.
ClockSample exchange(Duration localSend,
        Duration remoteAhead,
        Duration delayThere,
        Duration delayBack,
        Duration remoteProcessing = ms(0)) {
    const Duration remoteReceive = localSend + delayThere + remoteAhead;
    const Duration remoteSend = remoteReceive + remoteProcessing;
    const Duration localReceive = remoteSend - remoteAhead + delayBack;
    return ClockSample{localSend, remoteReceive, remoteSend, localReceive};
}

void addSamples(ClockEstimator* pEstimator, int count, Duration remoteAhead) {
    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(pEstimator->addSample(
                exchange(ms(1000 + 250 * i), remoteAhead, ms(1), ms(1))));
    }
}

TEST(XdjSyncClockTest, NoEstimateUntilEnoughSamples) {
    ClockEstimator estimator;
    EXPECT_FALSE(estimator.estimate().has_value());
    addSamples(&estimator, ClockEstimator::kMinSamples - 1, ms(0));
    EXPECT_FALSE(estimator.estimate().has_value());
    addSamples(&estimator, 1, ms(0));
    EXPECT_TRUE(estimator.estimate().has_value());
}

TEST(XdjSyncClockTest, SymmetricDelayGivesExactOffset) {
    ClockEstimator estimator;
    addSamples(&estimator, 3, ms(5000));
    const auto estimate = estimator.estimate();
    ASSERT_TRUE(estimate.has_value());
    EXPECT_EQ(ms(5000), estimate->offset);
    EXPECT_EQ(ms(2), estimate->roundTrip);
    EXPECT_EQ(ms(1), estimate->uncertainty);
}

TEST(XdjSyncClockTest, RemoteBehindGivesNegativeOffset) {
    ClockEstimator estimator;
    addSamples(&estimator, 3, ms(-1234));
    ASSERT_TRUE(estimator.estimate().has_value());
    EXPECT_EQ(ms(-1234), estimator.estimate()->offset);
}

TEST(XdjSyncClockTest, RemoteProcessingTimeIsNotRoundTrip) {
    ClockEstimator estimator;
    for (int i = 0; i < 3; ++i) {
        estimator.addSample(exchange(ms(1000 + 250 * i), ms(70), ms(1), ms(1), ms(10)));
    }
    ASSERT_TRUE(estimator.estimate().has_value());
    EXPECT_EQ(ms(70), estimator.estimate()->offset);
    EXPECT_EQ(ms(2), estimator.estimate()->roundTrip);
}

TEST(XdjSyncClockTest, AsymmetricDelayErrorWithinUncertainty) {
    ClockEstimator estimator;
    for (int i = 0; i < 3; ++i) {
        estimator.addSample(exchange(ms(1000 + 250 * i), ms(300), ms(1), ms(3)));
    }
    const auto estimate = estimator.estimate();
    ASSERT_TRUE(estimate.has_value());
    const double errorMs = (estimate->offset - ms(300)).toDoubleMillis();
    EXPECT_NE(0.0, errorMs);
    EXPECT_LE(std::abs(errorMs), estimate->uncertainty.toDoubleMillis());
}

TEST(XdjSyncClockTest, UsesSampleWithLowestRoundTrip) {
    // WiFi-like jitter: only the fastest exchange is nearly symmetric.
    ClockEstimator estimator;
    estimator.addSample(exchange(ms(1000), ms(40), ms(2), ms(9)));
    estimator.addSample(exchange(ms(1250), ms(40), ms(0.5), ms(0.5)));
    estimator.addSample(exchange(ms(1500), ms(40), ms(12), ms(3)));
    estimator.addSample(exchange(ms(1750), ms(40), ms(6), ms(1)));
    const auto estimate = estimator.estimate();
    ASSERT_TRUE(estimate.has_value());
    EXPECT_EQ(ms(40), estimate->offset);
    EXPECT_EQ(ms(1), estimate->roundTrip);
}

TEST(XdjSyncClockTest, OldSamplesLeaveTheWindow) {
    // The remote clock was stepped (e.g. NTP on the Pi): old samples,
    // even with a lower round trip, must not win forever.
    ClockEstimator estimator;
    for (int i = 0; i < 3; ++i) {
        estimator.addSample(exchange(ms(1000 + 250 * i), ms(0), ms(0.1), ms(0.1)));
    }
    for (int i = 0; i < ClockEstimator::kDefaultWindow; ++i) {
        estimator.addSample(exchange(ms(5000 + 250 * i), ms(800), ms(1), ms(1)));
    }
    ASSERT_TRUE(estimator.estimate().has_value());
    EXPECT_EQ(ms(800), estimator.estimate()->offset);
}

TEST(XdjSyncClockTest, RejectsImpossibleSamples) {
    ClockEstimator estimator;
    // Pong received before ping sent.
    EXPECT_FALSE(estimator.addSample(ClockSample{ms(1000), ms(0), ms(0), ms(999)}));
    // Remote claims to have spent longer than the whole round trip.
    EXPECT_FALSE(estimator.addSample(ClockSample{ms(1000), ms(0), ms(50), ms(1010)}));
    // Remote sent before it received.
    EXPECT_FALSE(estimator.addSample(ClockSample{ms(1000), ms(10), ms(5), ms(1020)}));
    EXPECT_FALSE(estimator.estimate().has_value());
}

TEST(XdjSyncClockTest, ToLocalConvertsRemoteTime) {
    ClockEstimator estimator;
    addSamples(&estimator, 3, ms(5000));
    const auto estimate = estimator.estimate();
    ASSERT_TRUE(estimate.has_value());
    EXPECT_EQ(ms(1234), estimate->toLocal(ms(6234)));
}

TEST(XdjSyncClockTest, ResetForgetsSamples) {
    ClockEstimator estimator;
    addSamples(&estimator, 3, ms(5000));
    estimator.reset();
    EXPECT_FALSE(estimator.estimate().has_value());
}

} // namespace
