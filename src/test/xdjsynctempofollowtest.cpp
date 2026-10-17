#include <gtest/gtest.h>

#include "xdjsync/tempofollow.h"

namespace {

using mixxx::xdjsync::followTempo;
using mixxx::xdjsync::kTempoTolerance;
using mixxx::xdjsync::PlayerSyncMode;

TEST(XdjSyncTempoFollowTest, FreeAndMasterDoNotFollow) {
    EXPECT_FALSE(followTempo(PlayerSyncMode::Free, 128.0, 130.0).has_value());
    EXPECT_FALSE(followTempo(PlayerSyncMode::Master, 128.0, 130.0).has_value());
}

TEST(XdjSyncTempoFollowTest, BpmModeFollowsReference) {
    EXPECT_EQ(130.25, followTempo(PlayerSyncMode::Bpm, 128.0, 130.25));
    EXPECT_EQ(125.0, followTempo(PlayerSyncMode::Bpm, 128.0, 125.0));
}

TEST(XdjSyncTempoFollowTest, BeatModeFollowsTempoToo) {
    EXPECT_EQ(130.25, followTempo(PlayerSyncMode::Beat, 128.0, 130.25));
}

TEST(XdjSyncTempoFollowTest, NothingToDoWhenMatched) {
    EXPECT_FALSE(followTempo(PlayerSyncMode::Bpm, 128.0, 128.0).has_value());
    EXPECT_FALSE(followTempo(PlayerSyncMode::Bpm, 128.0, 128.0 + kTempoTolerance / 2)
                    .has_value());
    EXPECT_TRUE(followTempo(PlayerSyncMode::Bpm, 128.0, 128.0 + kTempoTolerance * 2)
                    .has_value());
}

TEST(XdjSyncTempoFollowTest, NeedsReferenceAndTrackTempo) {
    EXPECT_FALSE(followTempo(PlayerSyncMode::Bpm, 128.0, std::nullopt).has_value());
    // Reference has no track or no beatgrid.
    EXPECT_FALSE(followTempo(PlayerSyncMode::Bpm, 128.0, 0.0).has_value());
    // This player has no track or no beatgrid: nothing to scale.
    EXPECT_FALSE(followTempo(PlayerSyncMode::Bpm, 0.0, 128.0).has_value());
}

} // namespace
