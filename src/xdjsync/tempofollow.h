#pragma once

#include <optional>

#include "xdjsync/packet.h"

namespace mixxx::xdjsync {

/// Smaller tempo differences are left alone, so the deck's rate is not set
/// every tick. 0.0005 BPM drifts ~0.002 ms per second at 128 BPM.
constexpr double kTempoTolerance = 0.0005;

/// Tempo this player should be set to (v2: BPM and BEAT mode follow the
/// reference tempo), or empty if there is nothing to do.
std::optional<double> followTempo(
        PlayerSyncMode ownMode, double localBpm, std::optional<double> referenceBpm);

} // namespace mixxx::xdjsync
