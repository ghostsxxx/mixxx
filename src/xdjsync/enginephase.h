#pragma once

#include <optional>

#include "control/controlvalue.h"
#include "control/pollingcontrolproxy.h"
#include "util/duration.h"
#include "xdjsync/beatphase.h"

namespace mixxx::xdjsync {

/// This deck as heard at the DAC.
struct EngineSample {
    PhaseSample phase;
    /// Track position in frames, at the same instant as phase.
    double trackFrame;
};

/// Hands this player's beat phase from the audio thread to the network
/// thread, timed to when it is heard (D5). One writer (audio thread), any
/// number of readers; lock-free and allocation-free.
class EnginePhaseBuffer {
  public:
    /// A sample older than this (audio stopped) is not returned.
    static constexpr Duration kMaxAge = Duration::fromMillis(500);

    /// Audio thread, at callback entry: when the first sample of the buffer
    /// about to be rendered reaches the DAC.
    void setFirstSampleAtDac(Duration atDac);

    /// Audio thread, after the engine updated beat_distance for the
    /// rendered buffer. Ignored until setFirstSampleAtDac was called.
    void publish(bool playing,
            double bpm,
            double beatDistance,
            double trackFrame,
            Duration bufferDuration);

    /// Any thread.
    std::optional<EngineSample> latest(Duration now) const;

  private:
    struct Slot {
        bool valid = false;
        EngineSample sample{};
    };

    // Only touched by the audio thread.
    std::optional<Duration> m_firstSampleAtDac;
    ControlValueAtomic<Slot> m_latest;
};

/// Reads Channel1's controls on the audio thread. Must be created on the
/// main thread (control lookup allocates and locks).
class EngineDeckPhaseSource {
  public:
    EngineDeckPhaseSource();

    void publish(EnginePhaseBuffer* pBuffer, Duration bufferDuration) const;

  private:
    PollingControlProxy m_play;
    PollingControlProxy m_bpm;
    PollingControlProxy m_beatDistance;
    PollingControlProxy m_playPosition;
    PollingControlProxy m_trackSamples;
};

/// Delay from audio callback entry to the DAC, in seconds: the driver's own
/// timestamps (PortAudio outputBufferDacTime - currentTime) if plausible,
/// else Mixxx's estimate. Mixxx's estimate rejects driver timestamps on
/// CPU-timer jitter and then guesses within two buffers, differently per
/// process, which breaks comparing players.
double callbackToDacSeconds(double driverCurrentTime,
        double driverOutputBufferDacTime,
        double mixxxEstimateSeconds);

/// Hooks for Mixxx's audio code. No-ops until a source is installed.
/// Sound device clock reference callback, at entry.
void onAudioCallbackEntry(Duration firstSampleAtDac);
/// EngineMixer, after all channels were post-processed.
void onEngineCallbackEnd(Duration bufferDuration);

/// Installed by XdjSyncService while it runs; nullptr to remove. The caller
/// must make sure the audio thread is not running onEngineCallbackEnd when
/// the source is deleted (remove only after audio has stopped).
void installDeckPhaseSource(const EngineDeckPhaseSource* pSource);

/// The process-wide buffer the hooks write to.
EnginePhaseBuffer& enginePhase();

} // namespace mixxx::xdjsync
