#pragma once

#include <QHostAddress>
#include <QObject>
#include <QThread>
#include <memory>

#include "preferences/usersettings.h"

class ControlObject;

namespace mixxx::xdjsync {

class XdjSyncWorker;

/// Network sync between XDJ100SX players (see XDJ100SX sync/DESIGN.md).
///
/// Owns the [XdjSync] controls and runs all networking on its own thread, so
/// neither the GUI nor the audio thread ever wait on the network.
///
/// Config ([XdjSync] in mixxx.cfg):
///   Enabled          0/1, default 0
///   PlayerId         default: host name
///   Port             default 51760
///   BroadcastAddress default 255.255.255.255
class XdjSyncService : public QObject {
    Q_OBJECT
  public:
    static constexpr const char* kConfigGroup = "[XdjSync]";

    explicit XdjSyncService(UserSettingsPointer pConfig);
    ~XdjSyncService() override;

    static bool isEnabled(const UserSettingsPointer& pConfig);

    /// Controls shared with the worker thread. ControlObject::set/get are
    /// thread-safe; the objects themselves stay owned by the main thread.
    struct Controls {
        std::unique_ptr<ControlObject> pEnabled;
        std::unique_ptr<ControlObject> pSyncMode;
        std::unique_ptr<ControlObject> pPeerCount;
        std::unique_ptr<ControlObject> pRefPresent;
        std::unique_ptr<ControlObject> pRefOffsetValid;
        std::unique_ptr<ControlObject> pRefOffsetMs;
        std::unique_ptr<ControlObject> pRefBpm;
        std::unique_ptr<ControlObject> pRefBpmDelta;
        std::unique_ptr<ControlObject> pRefDrifting;
        std::unique_ptr<ControlObject> pRefClockUncertaintyMs;
    };

  private:
    Controls m_controls;
    QThread m_thread;
    XdjSyncWorker* m_pWorker;
};

} // namespace mixxx::xdjsync
