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
/// mode, Mixxx-style (D23): one beat jump when sync engages (SYNC on, play
/// start, a different master), afterwards only tempo
/// corrections (also when the same master is back after a network drop).
/// A DJ nudge on this player becomes an intentional offset that
/// is kept. Phase is only corrected while both play and the clock is aligned
/// well enough.
class PhaseFollower {
  public:
    /// Phase errors are removed by a tempo trim with this time constant.
    static constexpr double kSettleSeconds = 1.0;
    /// Largest tempo trim (5 %, like Mixxx's own sync).
    static constexpr double kMaxTrim = 0.05;
    /// Further off than this (in beats) the full trim is used ("train wreck").
    static constexpr double kTrainWreckBeats = 0.2;
    /// Offsets below this are not corrected.
    static constexpr double kDeadbandMs = 0.5;
    /// Don't chase phase with a worse clock alignment than this.
    static constexpr double kMaxClockUncertaintyMs = 5.0;

    /// `alignBars`: BEAT mode also puts beat 1 on beat 1 (4/4), when both
    /// players know their bar position; else only the beat is aligned.
    explicit PhaseFollower(bool alignBars = true)
            : m_alignBars(alignBars) {
    }

    /// `userNudging`: the DJ is nudging this player (jog, pitch bend) now.
    SyncAction update(PlayerSyncMode ownMode,
            bool localPlaying,
            double localBpm,
            const std::optional<ReferenceStatus>& reference,
            Duration now,
            bool userNudging);

  private:
    const bool m_alignBars;
    bool m_engagePending = true;
    bool m_wasBeatMode = false;
    bool m_wasPlaying = false;
    /// Last master seen, kept across network drops.
    std::optional<QString> m_lastReferenceId;
    bool m_wasNudging = false;
    /// Kept offset from a DJ nudge, in beats.
    double m_userOffsetBeats = 0.0;
};

} // namespace mixxx::xdjsync
