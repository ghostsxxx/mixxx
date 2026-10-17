#include "xdjsync/tempofollow.h"

#include <cmath>

namespace mixxx::xdjsync {

std::optional<double> followTempo(
        PlayerSyncMode ownMode, double localBpm, std::optional<double> referenceBpm) {
    if (ownMode != PlayerSyncMode::Bpm && ownMode != PlayerSyncMode::Beat) {
        return std::nullopt;
    }
    if (!referenceBpm || *referenceBpm <= 0.0 || localBpm <= 0.0) {
        return std::nullopt;
    }
    if (std::abs(*referenceBpm - localBpm) < kTempoTolerance) {
        return std::nullopt;
    }
    return referenceBpm;
}

} // namespace mixxx::xdjsync
