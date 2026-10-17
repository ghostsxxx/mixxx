#include "xdjsync/xdjsyncservice.h"

#include <QHostInfo>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include "control/controlobject.h"
#include "control/pollingcontrolproxy.h"
#include "moc_xdjsyncservice.cpp"
#include "util/logger.h"
#include "util/time.h"
#include "xdjsync/enginephase.h"
#include "xdjsync/peertable.h"

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

PlayerSyncMode toSyncMode(double value) {
    const int mode = static_cast<int>(value);
    if (mode < 0 || mode > static_cast<int>(PlayerSyncMode::Master)) {
        return PlayerSyncMode::Free;
    }
    return static_cast<PlayerSyncMode>(mode);
}

} // namespace

/// Lives on XdjSyncService's thread. Not a Q_OBJECT: only lambdas connect.
class XdjSyncWorker : public QObject {
  public:
    XdjSyncWorker(XdjSyncService::Controls* pControls,
            QString playerId,
            QHostAddress broadcastAddress,
            quint16 port)
            : m_pControls(pControls),
              m_playerId(std::move(playerId)),
              m_broadcastAddress(std::move(broadcastAddress)),
              m_port(port),
              m_peers(m_playerId),
              m_play(ConfigKey(kDeckGroup, QStringLiteral("play"))),
              m_bpm(ConfigKey(kDeckGroup, QStringLiteral("bpm"))),
              m_beatDistance(ConfigKey(kDeckGroup, QStringLiteral("beat_distance"))) {
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

    PlayerSyncMode syncMode() const {
        return toSyncMode(m_pControls->pSyncMode->get());
    }

    void tick() {
        const Duration now = Time::elapsed();
        // Phase timed to the audio output (D5). Without audio (no clock
        // reference device yet), fall back to the controls sampled now.
        const auto enginePhaseSample = enginePhase().latest(now);
        m_dacTimed = enginePhaseSample.has_value();
        const PhaseSample local = enginePhaseSample.value_or(PhaseSample{
                m_play.toBool(),
                m_bpm.get(),
                m_beatDistance.get(),
                now,
        });
        send(StatePacket{
                m_playerId,
                m_stateSeq++,
                local.playing,
                syncMode(),
                local.bpm,
                local.beatDistance,
                0, // beatIndex: not used before bar alignment
                local.atDac,
        });

        m_peers.expire(now);
        updateControls(m_peers.reference(local, syncMode(), now));
    }

    void updateControls(const std::optional<ReferenceStatus>& reference) {
        Controls& c = *m_pControls;
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
        c.pRefInSync->forceSet(valid && inSync(*reference->offset) ? 1.0 : 0.0);
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
                                << (m_dacTimed ? "dac_timed" : "not_dac_timed");
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
    const QHostAddress m_broadcastAddress;
    const quint16 m_port;
    PeerTable m_peers;
    PollingControlProxy m_play;
    PollingControlProxy m_bpm;
    PollingControlProxy m_beatDistance;
    QUdpSocket* m_pSocket = nullptr;
    quint32 m_stateSeq = 0;
    quint32 m_pingSeq = 0;
    int m_logTicks = 0;
    bool m_dacTimed = false;
};

XdjSyncService::XdjSyncService(UserSettingsPointer pConfig) {
    m_controls.pEnabled = makeReadOnly("enabled");
    m_controls.pSyncMode = std::make_unique<ControlObject>(key("sync_mode"));
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

    if (!pConfig->getValue<bool>(key("Enabled"), false)) {
        return;
    }

    m_pDeckPhaseSource = std::make_unique<EngineDeckPhaseSource>();
    installDeckPhaseSource(m_pDeckPhaseSource.get());

    const QString playerId = pConfig->getValue(key("PlayerId"), QHostInfo::localHostName());
    const auto port = static_cast<quint16>(pConfig->getValue(key("Port"), static_cast<int>(kDefaultPort)));
    const QHostAddress broadcastAddress(pConfig->getValue(
            key("BroadcastAddress"), QStringLiteral("255.255.255.255")));

    m_pWorker = new XdjSyncWorker(&m_controls, playerId, broadcastAddress, port);
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
