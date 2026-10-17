#include "xdjsync/xdjsyncservice.h"

#include <QHostInfo>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "control/pollingcontrolproxy.h"
#include "mixer/playerinfo.h"
#include "track/track.h"
#include "moc_xdjsyncservice.cpp"
#include "util/logger.h"
#include "util/time.h"
#include "xdjsync/countdown.h"
#include "xdjsync/enginephase.h"
#include "xdjsync/gridposition.h"
#include "xdjsync/lanegeometry.h"
#include "xdjsync/peertable.h"
#include "xdjsync/phasefollower.h"

namespace mixxx::xdjsync {

namespace {

const Logger kLogger("XdjSync");

constexpr quint16 kDefaultPort = 51760;
constexpr int kStateIntervalMs = 20;  // 50 Hz
constexpr int kPingIntervalMs = 250;  // see ClockEstimator
constexpr int kLogIntervalTicks = 50; // once per second

const QString kGroup = QString::fromLatin1(XdjSyncService::kConfigGroup);
const QString kDeckGroup = QStringLiteral("[Channel1]");

ConfigKey key(const char* item) {
    return ConfigKey(kGroup, QString::fromLatin1(item));
}

std::unique_ptr<ControlObject> makeReadOnly(const char* item) {
    auto pControl = std::make_unique<ControlObject>(key(item));
    pControl->setReadOnly();
    return pControl;
}

} // namespace

/// Lives on XdjSyncService's thread. Not a Q_OBJECT: only lambdas connect.
class XdjSyncWorker : public QObject {
  public:
    XdjSyncWorker(XdjSyncService::Controls* pControls,
            QString playerId,
            QHostAddress broadcastAddress,
            quint16 port,
            PlayerSyncMode syncType,
            bool alignBars)
            : m_pControls(pControls),
              m_playerId(std::move(playerId)),
              m_syncType(syncType),
              m_broadcastAddress(std::move(broadcastAddress)),
              m_port(port),
              m_peers(m_playerId),
              m_play(ConfigKey(kDeckGroup, QStringLiteral("play"))),
              m_bpm(ConfigKey(kDeckGroup, QStringLiteral("bpm"))),
              m_beatDistance(ConfigKey(kDeckGroup, QStringLiteral("beat_distance"))),
              m_beatJump(ConfigKey(kDeckGroup, QStringLiteral("beatjump"))),
              m_playPosition(ConfigKey(kDeckGroup, QStringLiteral("playposition"))),
              m_trackSamples(ConfigKey(kDeckGroup, QStringLiteral("track_samples"))),
              m_alignBars(alignBars),
              m_follower(alignBars) {
    }

    /// Runs on the worker thread.
    void start() {
        m_pSocket = new QUdpSocket(this);
        // ShareAddress + ReuseAddressHint: SO_REUSEPORT on macOS, so several
        // players on one machine can all receive the broadcasts.
        if (!m_pSocket->bind(QHostAddress::AnyIPv4,
                    m_port,
                    QAbstractSocket::ShareAddress |
                            QAbstractSocket::ReuseAddressHint)) {
            kLogger.warning() << "Cannot bind UDP port" << m_port << m_pSocket->errorString();
            return;
        }
        connect(m_pSocket, &QUdpSocket::readyRead, this, [this] { receive(); });

        auto* pStateTimer = new QTimer(this);
        pStateTimer->setTimerType(Qt::PreciseTimer);
        connect(pStateTimer, &QTimer::timeout, this, [this] { tick(); });
        pStateTimer->start(kStateIntervalMs);

        auto* pPingTimer = new QTimer(this);
        pPingTimer->setTimerType(Qt::PreciseTimer);
        connect(pPingTimer, &QTimer::timeout, this, [this] { ping(); });
        pPingTimer->start(kPingIntervalMs);

        m_pControls->pEnabled->forceSet(1.0);
        kLogger.info() << "Started as" << m_playerId << "on port" << m_port
                       << "broadcasting to" << m_broadcastAddress.toString();
    }

  private:
    void send(const Packet& packet) {
        m_pSocket->writeDatagram(encodePacket(packet), m_broadcastAddress, m_port);
    }

    void receive() {
        while (m_pSocket->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = m_pSocket->receiveDatagram();
            const Duration receivedAt = Time::elapsed();
            const auto packet = decodePacket(datagram.data());
            if (!packet) {
                continue;
            }
            auto pong = m_peers.handlePacket(*packet, receivedAt);
            if (pong) {
                pong->pongSent = Time::elapsed();
                send(*pong);
            }
        }
    }

    void ping() {
        send(PingPacket{m_playerId, m_pingSeq++, Time::elapsed()});
    }

    /// Free with SYNC off, else the configured sync type.
    PlayerSyncMode syncMode() const {
        return m_pControls->pSync->toBool() ? m_syncType : PlayerSyncMode::Free;
    }

    Duration playingFor(bool playing, Duration now) {
        if (!playing) {
            m_playingSince.reset();
            return Duration();
        }
        if (!m_playingSince) {
            m_playingSince = now;
        }
        return now - *m_playingSince;
    }

    /// Bar position and cue countdown of the loaded track at `trackFrame`.
    struct DeckGrid {
        std::optional<qint64> beatIndex;
        std::optional<double> cueCountdownBeats;
        /// Hot cues in beats from the first downbeat.
        QList<LaneCue> cues;
    };

    DeckGrid deckGrid(double trackFrame, double beatDistance) const {
        // Track and its beats/cues are thread-safe to read (Track locks).
        const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(kDeckGroup);
        if (!pTrack) {
            return {};
        }
        const BeatsPointer pBeats = pTrack->getBeats();
        if (!pBeats) {
            return {};
        }
        const double position = beatPosition(*pBeats, audio::FramePos(trackFrame));
        QList<double> cueBeats;
        QList<LaneCue> laneCues;
        const QList<CuePointer> cues = pTrack->getCuePoints();
        for (const CuePointer& pCue : cues) {
            const auto type = pCue->getType();
            const auto cuePosition = pCue->getPosition();
            // Hot cues only: Mixxx's main cue, intro/outro and sound markers
            // usually sit at the track start and would only add noise.
            if (type == CueType::HotCue && cuePosition.isValid()) {
                const double cueBeat = beatPosition(*pBeats, cuePosition);
                cueBeats.append(cueBeat);
                laneCues.append(LaneCue{cueBeat, pCue->getHotCue()});
            }
        }
        const auto countdown = nextCueCountdown(position, cueBeats);
        // The engine's beat_distance is authoritative within the beat; the
        // grid position only decides which beat it is.
        return DeckGrid{std::llround(position - beatDistance),
                countdown ? std::optional(countdown->beats) : std::nullopt,
                laneCues};
    }

    void tick() {
        const Duration now = Time::elapsed();
        // Phase timed to the audio output (D5). Without audio (no clock
        // reference device yet), fall back to the controls sampled now.
        const auto engineSample = enginePhase().latest(now);
        m_dacTimed = engineSample.has_value();
        PhaseSample local = engineSample ? engineSample->phase
                                         : PhaseSample{
                                                   m_play.toBool(),
                                                   m_bpm.get(),
                                                   m_beatDistance.get(),
                                                   now,
                                           };
        const double trackFrame = engineSample
                ? engineSample->trackFrame
                : m_playPosition.get() * m_trackSamples.get() / 2.0;
        const DeckGrid grid = deckGrid(trackFrame, local.beatDistance);
        local.beatIndex = grid.beatIndex;
        std::optional<LaneState> ownLane;
        if (local.beatIndex && local.bpm > 0.0) {
            ownLane = LaneState{local.playing,
                    local.bpm,
                    static_cast<double>(*local.beatIndex) + local.beatDistance,
                    local.atDac,
                    grid.cues};
        }
        const Duration localPlayingFor = playingFor(local.playing, now);
        send(StatePacket{
                m_playerId,
                m_stateSeq++,
                local.playing,
                syncMode(),
                local.bpm,
                local.beatDistance,
                local.beatIndex.value_or(0),
                local.atDac,
                localPlayingFor,
                local.beatIndex.has_value(),
                grid.cueCountdownBeats,
                ownLane ? upcomingCues(ownLane->cues, ownLane->beatPosition)
                        : QList<UpcomingCue>(),
        });
        updateOwnControls(local, grid, now);

        m_peers.expire(now);
        const auto master = m_peers.masterPlayerId(local, localPlayingFor, now);
        m_pControls->pMaster->forceSet(master == m_playerId ? 1.0 : 0.0);
        // The master follows nobody (reference() is empty for it).
        const auto reference = m_peers.reference(local, localPlayingFor, now);
        publishLanes(LanesSnapshot{
                ownLane,
                reference ? reference->lane : std::nullopt,
                master == m_playerId,
        });
        const SyncAction action =
                m_follower.update(syncMode(), local.playing, m_bpm.get(), reference, now);
        // Setting bpm makes Mixxx adjust the rate (on the main thread), as if
        // the BPM were typed in. Overrides the pitch fader while following.
        if (action.targetBpm) {
            m_bpm.set(*action.targetBpm);
        }
        // Fractional beat jump; like a controller script setting it.
        if (action.beatJump) {
            m_beatJump.set(*action.beatJump);
        }
        updateControls(reference);
    }

    static void setCountdown(ControlObject* pValid,
            ControlObject* pBars,
            ControlObject* pBeat,
            std::optional<double> beats) {
        const auto countdown = beats ? std::optional(CueCountdown::fromBeats(*beats)) : std::nullopt;
        pValid->forceSet(countdown ? 1.0 : 0.0);
        pBars->forceSet(countdown ? countdown->bars : 0.0);
        pBeat->forceSet(countdown ? countdown->beat : 0.0);
    }

    static double barBeatControl(std::optional<double> barPosition) {
        return barPosition ? std::floor(*barPosition) + 1.0 : 0.0;
    }

    void updateOwnControls(const PhaseSample& local, const DeckGrid& grid, Duration now) {
        Controls& c = *m_pControls;
        c.pBarBeat->forceSet(barBeatControl(barPosition(local, now)));
        std::optional<double> countdownNow;
        if (grid.cueCountdownBeats) {
            countdownNow = std::max(0.0,
                    *grid.cueCountdownBeats - (projectPhase(local, now) - local.beatDistance));
        }
        setCountdown(c.pCueCountdownValid.get(),
                c.pCueCountdownBars.get(),
                c.pCueCountdownBeat.get(),
                countdownNow);
    }

    void updateControls(const std::optional<ReferenceStatus>& reference) {
        Controls& c = *m_pControls;
        c.pRefBarBeat->forceSet(barBeatControl(reference ? reference->barPosition : std::nullopt));
        setCountdown(c.pRefCueCountdownValid.get(),
                c.pRefCueCountdownBars.get(),
                c.pRefCueCountdownBeat.get(),
                reference ? reference->cueCountdownBeats : std::nullopt);
        c.pPeerCount->forceSet(m_peers.peerCount());
        c.pRefPresent->forceSet(reference ? 1.0 : 0.0);
        c.pRefBpm->forceSet(reference ? reference->bpm : 0.0);
        c.pRefClockUncertaintyMs->forceSet(reference && reference->clock
                        ? reference->clock->uncertainty.toDoubleMillis()
                        : 0.0);
        const bool valid = reference && reference->offset;
        c.pRefOffsetValid->forceSet(valid ? 1.0 : 0.0);
        c.pRefOffsetMs->forceSet(valid ? reference->offset->offsetMs : 0.0);
        c.pRefPhase->forceSet(valid ? 0.5 + reference->offset->offsetBeats : 0.5);
        c.pRefBeatsOff->forceSet(valid ? reference->offset->beatsOffInBar : 0.0);
        bool synced = false;
        if (valid) {
            BeatOffset offset = *reference->offset;
            if (!m_alignBars) {
                offset.beatsOffInBar = 0; // only the beat matters
            }
            // In BEAT mode the tempo difference is a deliberate phase trim.
            synced = inSync(offset, syncMode() == PlayerSyncMode::Beat);
        }
        c.pRefInSync->forceSet(synced ? 1.0 : 0.0);
        c.pRefBpmDelta->forceSet(valid ? reference->offset->bpmDelta : 0.0);
        c.pRefDrifting->forceSet(valid && reference->offset->drifting ? 1.0 : 0.0);

        if (++m_logTicks >= kLogIntervalTicks) {
            m_logTicks = 0;
            if (valid) {
                kLogger.debug() << "peers" << m_peers.peerCount() << "ref"
                                << reference->playerId << "offset_ms"
                                << reference->offset->offsetMs << "bpm_delta"
                                << reference->offset->bpmDelta << "clock_uncertainty_ms"
                                << reference->clock->uncertainty.toDoubleMillis()
                                << (m_dacTimed ? "dac_timed" : "not_dac_timed")
                                << "bar_beat" << c.pBarBeat->get() << "ref_bar_beat"
                                << c.pRefBarBeat->get() << "beats_off"
                                << reference->offset->beatsOffInBar << "cue"
                                << c.pCueCountdownBars->get() << c.pCueCountdownBeat->get()
                                << "ref_cue" << c.pRefCueCountdownBars->get()
                                << c.pRefCueCountdownBeat->get();
            } else {
                kLogger.debug() << "peers" << m_peers.peerCount() << "ref"
                                << (reference ? reference->playerId : QStringLiteral("-"))
                                << "no offset";
            }
        }
    }

    using Controls = XdjSyncService::Controls;

    Controls* const m_pControls;
    const QString m_playerId;
    const PlayerSyncMode m_syncType;
    std::optional<Duration> m_playingSince;
    const QHostAddress m_broadcastAddress;
    const quint16 m_port;
    PeerTable m_peers;
    PollingControlProxy m_play;
    PollingControlProxy m_bpm;
    PollingControlProxy m_beatDistance;
    PollingControlProxy m_beatJump;
    PollingControlProxy m_playPosition;
    PollingControlProxy m_trackSamples;
    const bool m_alignBars;
    PhaseFollower m_follower;
    QUdpSocket* m_pSocket = nullptr;
    quint32 m_stateSeq = 0;
    quint32 m_pingSeq = 0;
    int m_logTicks = 0;
    bool m_dacTimed = false;
};

namespace {

/// Re-anchors the loaded track's grid so the beat at the play position is
/// beat 1 of the bar; saved with the track like any grid edit.
void setDownbeatAtPlayPosition() {
    const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(kDeckGroup);
    if (!pTrack) {
        return;
    }
    const BeatsPointer pBeats = pTrack->getBeats();
    if (!pBeats) {
        return;
    }
    const double trackFrame =
            ControlObject::get(ConfigKey(kDeckGroup, QStringLiteral("playposition"))) *
            ControlObject::get(ConfigKey(kDeckGroup, QStringLiteral("track_samples"))) / 2.0;
    const auto pNewBeats = withDownbeatAt(*pBeats, audio::FramePos(trackFrame));
    if (!pNewBeats || !pTrack->trySetBeats(*pNewBeats)) {
        kLogger.warning() << "Cannot set downbeat (variable tempo or locked grid)";
    }
}

} // namespace

XdjSyncService::XdjSyncService(UserSettingsPointer pConfig) {
    m_controls.pEnabled = makeReadOnly("enabled");
    // Persistent: a player restarts with SYNC as it was left. A toggle, so
    // a skin button latches (a plain control would act as a push button).
    auto pSync = std::make_unique<ControlPushButton>(key("sync"), true);
    pSync->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_controls.pSync = std::move(pSync);
    m_controls.pMaster = makeReadOnly("master");
    m_controls.pPeerCount = makeReadOnly("peer_count");
    m_controls.pRefPresent = makeReadOnly("ref_present");
    m_controls.pRefOffsetValid = makeReadOnly("ref_offset_valid");
    m_controls.pRefOffsetMs = makeReadOnly("ref_offset_ms");
    m_controls.pRefPhase = makeReadOnly("ref_phase");
    m_controls.pRefPhase->forceSet(0.5);
    m_controls.pRefInSync = makeReadOnly("ref_in_sync");
    m_controls.pRefBpm = makeReadOnly("ref_bpm");
    m_controls.pRefBpmDelta = makeReadOnly("ref_bpm_delta");
    m_controls.pRefDrifting = makeReadOnly("ref_drifting");
    m_controls.pRefClockUncertaintyMs = makeReadOnly("ref_clock_uncertainty_ms");
    m_controls.pBarBeat = makeReadOnly("bar_beat");
    m_controls.pCueCountdownValid = makeReadOnly("cue_countdown_valid");
    m_controls.pCueCountdownBars = makeReadOnly("cue_countdown_bars");
    m_controls.pCueCountdownBeat = makeReadOnly("cue_countdown_beat");
    m_controls.pRefBarBeat = makeReadOnly("ref_bar_beat");
    m_controls.pRefBeatsOff = makeReadOnly("ref_beats_off");
    m_controls.pRefCueCountdownValid = makeReadOnly("ref_cue_countdown_valid");
    m_controls.pRefCueCountdownBars = makeReadOnly("ref_cue_countdown_bars");
    m_controls.pRefCueCountdownBeat = makeReadOnly("ref_cue_countdown_beat");
    m_controls.pSetDownbeat = std::make_unique<ControlPushButton>(key("set_downbeat"));
    connect(m_controls.pSetDownbeat.get(),
            &ControlObject::valueChanged,
            this,
            [](double value) {
                if (value > 0) {
                    setDownbeatAtPlayPosition();
                }
            });

    if (!pConfig->getValue<bool>(key("Enabled"), false)) {
        return;
    }

    m_pDeckPhaseSource = std::make_unique<EngineDeckPhaseSource>();
    installDeckPhaseSource(m_pDeckPhaseSource.get());

    const QString playerId = pConfig->getValue(key("PlayerId"), QHostInfo::localHostName());
    const auto port = static_cast<quint16>(pConfig->getValue(key("Port"), static_cast<int>(kDefaultPort)));
    const QHostAddress broadcastAddress(pConfig->getValue(
            key("BroadcastAddress"), QStringLiteral("255.255.255.255")));

    const PlayerSyncMode syncType =
            pConfig->getValue(key("SyncType"), QStringLiteral("beat")) == QStringLiteral("bpm")
            ? PlayerSyncMode::Bpm
            : PlayerSyncMode::Beat;

    const bool alignBars =
            pConfig->getValue(key("SyncAlign"), QStringLiteral("bar")) != QStringLiteral("beat");

    m_pWorker = new XdjSyncWorker(
            &m_controls, playerId, broadcastAddress, port, syncType, alignBars);
    m_pWorker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_pWorker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("XdjSync"));
    m_thread.start();
    QMetaObject::invokeMethod(m_pWorker, [pWorker = m_pWorker] { pWorker->start(); });
}

XdjSyncService::~XdjSyncService() {
    installDeckPhaseSource(nullptr);
    // No-op if the thread was never started.
    m_thread.quit();
    m_thread.wait();
}

} // namespace mixxx::xdjsync
