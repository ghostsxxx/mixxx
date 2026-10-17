#pragma once

#include <QDomNode>
#include <QTimer>

#include "util/duration.h"
#include "widget/wwidget.h"
#include "xdjsync/lanegeometry.h"

class SkinContext;

namespace mixxx::xdjsync {

/// CDJ-style beat lanes (skin tag <XdjBeatLanes>): the reference (master)
/// deck on top, this deck below, on one time axis around a shared playhead,
/// with beats, numbered bars, hot cues and a bars.beat countdown to the next
/// hot cue per lane. Phase, beat-in-bar offset and tempo difference all show
/// as the two lanes' ticks not lining up.
class WXdjBeatLanes : public WWidget {
    Q_OBJECT
  public:
    explicit WXdjBeatLanes(QWidget* pParent = nullptr);

    void setup(const QDomNode& node, const SkinContext& context);

    /// Draws `snapshot` at `now` into `rect`; static so it can be rendered
    /// without a running player (tests).
    static void paintLanes(QPainter* pPainter,
            const QRect& rect,
            const LanesSnapshot& snapshot,
            Duration now);

  protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void showEvent(QShowEvent* pEvent) override;
    void hideEvent(QHideEvent* pEvent) override;

  private:
    QTimer m_refreshTimer;
};

} // namespace mixxx::xdjsync
