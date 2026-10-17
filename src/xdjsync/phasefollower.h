#pragma once

#include <optional>

#include "util/duration.h"
#include "xdjsync/packet.h"
#include "xdjsync/peertable.h"

namespace mixxx::xdjsync {

/// What to do to this player's deck in one tick.
struct SyncAction {
    /// Set the deck tempo to this.
    std::optional<double> targetBpm;
    /// Jump by this many beats (fractional, negative = back).
    std::optional<double> beatJump;
};

/// Follows the reference player: tempo in BPM mode, tempo and phase in BEAT
/// mode (D17). Phase is only corrected while both play and the clock is
/// aligned well enough.
class PhaseFollower {
  public:
    /// Larger offsets are removed at once with a beat jump.
    static constexpr double kSnapMs = 20.0;
    /// Measured offset still shows the old phase right after a jump.
    static constexpr Duration kSnapCooldown = Duration::fromMillis(500);
    /// Smaller offsets are removed by a tempo trim with this time constant.
    static constexpr double kSettleSeconds = 2.0;
    /// Largest tempo trim (0.5 %).
    static constexpr double kMaxTrim = 0.005;
    /// Offsets below this are not corrected.
    static constexpr double kDeadbandMs = 0.5;
    /// Don't chase phase with a worse clock alignment than this.
    static constexpr double kMaxClockUncertaintyMs = 5.0;

    /// `alignBars`: BEAT mode also puts beat 1 on beat 1 (4/4), when both
    /// players know their bar position; else only the beat is aligned.
    explicit PhaseFollower(bool alignBars = true)
            : m_alignBars(alignBars) {
    }

    SyncAction update(PlayerSyncMode ownMode,
            bool localPlaying,
            double localBpm,
            const std::optional<ReferenceStatus>& reference,
            Duration now);

  private:
    const bool m_alignBars;
    std::optional<Duration> m_lastSnap;
};

} // namespace mixxx::xdjsync
