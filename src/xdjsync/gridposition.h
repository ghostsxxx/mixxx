#pragma once

#include <optional>

#include "audio/frame.h"
#include "track/beats.h"

namespace mixxx::xdjsync {

/// Position in beats counted from the grid's first marker, which Mixxx puts
/// on the first downbeat: integer part = beat index (negative before it),
/// fraction = position within the beat.
double beatPosition(const Beats& beats, audio::FramePos position);

/// The same beats, re-anchored so the beat nearest to `position` is beat 1
/// of a 4/4 bar ("set downbeat"). Mixxx's analyzer doesn't find downbeats,
/// so the DJ marks one. Empty for variable-tempo grids.
std::optional<BeatsPointer> withDownbeatAt(const Beats& beats, audio::FramePos position);

} // namespace mixxx::xdjsync
