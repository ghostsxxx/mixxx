#include <gtest/gtest.h>

#include "xdjsync/enginephase.h"

namespace {

using mixxx::Duration;
using mixxx::xdjsync::callbackToDacSeconds;
using mixxx::xdjsync::EnginePhaseBuffer;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

TEST(XdjSyncEnginePhaseTest, EmptyBeforePublish) {
    EnginePhaseBuffer buffer;
    EXPECT_FALSE(buffer.latest(ms(0)).has_value());
}

TEST(XdjSyncEnginePhaseTest, IgnoresPublishWithoutDacTime) {
    EnginePhaseBuffer buffer;
    buffer.publish(true, 128.0, 0.25, ms(10));
    EXPECT_FALSE(buffer.latest(ms(0)).has_value());
}

TEST(XdjSyncEnginePhaseTest, PhaseIsHeardAtEndOfBuffer) {
    // beat_distance is updated after the buffer was rendered, so it belongs
    // to the sample right after the buffer: heard one buffer after the
    // buffer's first sample.
    EnginePhaseBuffer buffer;
    buffer.setFirstSampleAtDac(ms(1020));
    buffer.publish(true, 128.0, 0.25, ms(10));
    const auto sample = buffer.latest(ms(1000));
    ASSERT_TRUE(sample.has_value());
    EXPECT_TRUE(sample->playing);
    EXPECT_DOUBLE_EQ(128.0, sample->bpm);
    EXPECT_DOUBLE_EQ(0.25, sample->beatDistance);
    EXPECT_EQ(ms(1030), sample->atDac);
}

TEST(XdjSyncEnginePhaseTest, LatestWins) {
    EnginePhaseBuffer buffer;
    buffer.setFirstSampleAtDac(ms(1000));
    buffer.publish(true, 128.0, 0.1, ms(10));
    buffer.setFirstSampleAtDac(ms(1010));
    buffer.publish(true, 128.0, 0.12, ms(10));
    const auto sample = buffer.latest(ms(1000));
    ASSERT_TRUE(sample.has_value());
    EXPECT_DOUBLE_EQ(0.12, sample->beatDistance);
    EXPECT_EQ(ms(1020), sample->atDac);
}

TEST(XdjSyncEnginePhaseTest, StaleSampleIsDropped) {
    // Audio stopped (device lost, xrun storm): don't keep projecting an old
    // phase as if the deck were still playing.
    EnginePhaseBuffer buffer;
    buffer.setFirstSampleAtDac(ms(1000));
    buffer.publish(true, 128.0, 0.1, ms(10));
    EXPECT_TRUE(buffer.latest(ms(1010) + EnginePhaseBuffer::kMaxAge).has_value());
    EXPECT_FALSE(buffer.latest(ms(1011) + EnginePhaseBuffer::kMaxAge).has_value());
}

TEST(XdjSyncEnginePhaseTest, PrefersRawDriverDacDelay) {
    // Mixxx's estimate (CPU-timer fallback) can be off by up to two buffers
    // per process; the driver's own timestamps are used when plausible.
    EXPECT_NEAR(0.0213, callbackToDacSeconds(10.5, 10.5213, 0.004), 1e-12);
}

TEST(XdjSyncEnginePhaseTest, FallsBackOnImplausibleDriverDacDelay) {
    // Missing (0), in the past, or absurdly far in the future.
    EXPECT_DOUBLE_EQ(0.004, callbackToDacSeconds(10.5, 0.0, 0.004));
    EXPECT_DOUBLE_EQ(0.004, callbackToDacSeconds(10.5, 10.4, 0.004));
    EXPECT_DOUBLE_EQ(0.004, callbackToDacSeconds(10.5, 12.0, 0.004));
}

} // namespace
