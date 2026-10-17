#pragma once

#include <QHostAddress>
#include <QObject>
#include <QThread>
#include <memory>

#include "preferences/usersettings.h"

class ControlObject;

namespace mixxx::xdjsync {

class XdjSyncWorker;
class EngineDeckPhaseSource;

/// Network sync between XDJ100SX players (see XDJ100SX sync/DESIGN.md).
///
/// Owns the [XdjSync] controls and runs all networking on its own thread, so
/// neither the GUI nor the audio thread ever wait on the network. The
/// controls always exist (so skins can bind to them); the network thread
/// only runs if enabled.
///
/// Must be deleted only after audio processing has stopped: the audio thread
/// reads the deck phase source it installs.
///
/// Config ([XdjSync] in mixxx.cfg):
///   Enabled          0/1, default 0
///   PlayerId         default: host name
///   Port             default 51760
///   BroadcastAddress default 255.255.255.255
///   SyncType         beat (tempo + phase, default) or bpm (tempo only)
class XdjSyncService : public QObject {
    Q_OBJECT
  public:
    static constexpr const char* kConfigGroup = "[XdjSync]";

    explicit XdjSyncService(UserSettingsPointer pConfig);
    ~XdjSyncService() override;

    /// Controls shared with the worker thread. ControlObject::set/get are
    /// thread-safe; the objects themselves stay owned by the main thread.
    struct Controls {
        std::unique_ptr<ControlObject> pEnabled;
        /// SYNC on/off (toggle, persistent).
        std::unique_ptr<ControlObject> pSync;
        /// This player is the automatic master (read-only, D18).
        std::unique_ptr<ControlObject> pMaster;
        std::unique_ptr<ControlObject> pPeerCount;
        std::unique_ptr<ControlObject> pRefPresent;
        std::unique_ptr<ControlObject> pRefOffsetValid;
        std::unique_ptr<ControlObject> pRefOffsetMs;
        /// 0.5 + offset in beats: 0..1 with 0.5 = aligned, for a phase meter.
        std::unique_ptr<ControlObject> pRefPhase;
        std::unique_ptr<ControlObject> pRefInSync;
        std::unique_ptr<ControlObject> pRefBpm;
        std::unique_ptr<ControlObject> pRefBpmDelta;
        std::unique_ptr<ControlObject> pRefDrifting;
        std::unique_ptr<ControlObject> pRefClockUncertaintyMs;
    };

  private:
    Controls m_controls;
    std::unique_ptr<EngineDeckPhaseSource> m_pDeckPhaseSource;
    QThread m_thread;
    XdjSyncWorker* m_pWorker = nullptr;
};

} // namespace mixxx::xdjsync
