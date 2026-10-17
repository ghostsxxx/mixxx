#include "xdjsync/enginephase.h"

#include <atomic>

namespace mixxx::xdjsync {

namespace {

const QString kDeckGroup = QStringLiteral("[Channel1]");

std::atomic<const EngineDeckPhaseSource*> s_pSource{nullptr};

} // namespace

EngineDeckPhaseSource::EngineDeckPhaseSource()
        : m_play(kDeckGroup, QStringLiteral("play")),
          m_bpm(kDeckGroup, QStringLiteral("bpm")),
          m_beatDistance(kDeckGroup, QStringLiteral("beat_distance")),
          m_playPosition(kDeckGroup, QStringLiteral("playposition")),
          m_trackSamples(kDeckGroup, QStringLiteral("track_samples")) {
}

void EngineDeckPhaseSource::publish(EnginePhaseBuffer* pBuffer, Duration bufferDuration) const {
    // playposition is the fraction of the track, track_samples counts
    // interleaved stereo samples.
    const double trackFrame = m_playPosition.get() * m_trackSamples.get() / 2.0;
    pBuffer->publish(m_play.toBool(),
            m_bpm.get(),
            m_beatDistance.get(),
            trackFrame,
            bufferDuration);
}

void EnginePhaseBuffer::setFirstSampleAtDac(Duration atDac) {
    m_firstSampleAtDac = atDac;
}

void EnginePhaseBuffer::publish(bool playing,
        double bpm,
        double beatDistance,
        double trackFrame,
        Duration bufferDuration) {
    if (!m_firstSampleAtDac) {
        return;
    }
    // beat_distance belongs to the play position after the rendered buffer.
    m_latest.setValue(Slot{true,
            EngineSample{
                    PhaseSample{playing, bpm, beatDistance, *m_firstSampleAtDac + bufferDuration},
                    trackFrame}});
}

std::optional<EngineSample> EnginePhaseBuffer::latest(Duration now) const {
    const Slot slot = m_latest.getValue();
    if (!slot.valid || now - slot.sample.phase.atDac > kMaxAge) {
        return std::nullopt;
    }
    return slot.sample;
}

EnginePhaseBuffer& enginePhase() {
    static EnginePhaseBuffer s_buffer;
    return s_buffer;
}

double callbackToDacSeconds(double driverCurrentTime,
        double driverOutputBufferDacTime,
        double mixxxEstimateSeconds) {
    const double driverSeconds = driverOutputBufferDacTime - driverCurrentTime;
    if (driverOutputBufferDacTime > 0.0 && driverSeconds > 0.0 && driverSeconds < 1.0) {
        return driverSeconds;
    }
    return mixxxEstimateSeconds;
}

void onAudioCallbackEntry(Duration firstSampleAtDac) {
    enginePhase().setFirstSampleAtDac(firstSampleAtDac);
}

void onEngineCallbackEnd(Duration bufferDuration) {
    if (const EngineDeckPhaseSource* pSource = s_pSource.load(std::memory_order_acquire)) {
        pSource->publish(&enginePhase(), bufferDuration);
    }
}

void installDeckPhaseSource(const EngineDeckPhaseSource* pSource) {
    s_pSource.store(pSource, std::memory_order_release);
}

} // namespace mixxx::xdjsync
