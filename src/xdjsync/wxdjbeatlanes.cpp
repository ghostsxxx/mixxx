#include "xdjsync/wxdjbeatlanes.h"

#include <QFontDatabase>
#include <QPainter>

#include "moc_wxdjbeatlanes.cpp"
#include "util/time.h"

namespace mixxx::xdjsync {

namespace {

// Pioneer-like palette, as in the XDJ100SX skin.
const QColor kBackground(0x00, 0x00, 0x00);
const QColor kText(0xe5, 0xe6, 0xea);
const QColor kTextDim(0x8c, 0x8c, 0x99);
const QColor kInactive(0x5c, 0x5c, 0x68);
const QColor kPanel(0x0d, 0x0d, 0x10);
const QColor kMaster(0xeb, 0x87, 0x0f);
const QColor kOwn(0x2d, 0x85, 0xcd);
const QColor kOwnLabel(0x4e, 0xa3, 0xe8);
const QColor kCue(0xe0, 0x41, 0x3a);
const QColor kPlayhead(0xff, 0xff, 0xff);

constexpr int kRefreshIntervalMs = 33; // ~30 fps
constexpr int kLabelColumnWidth = 132;
constexpr int kColumnGap = 12;
constexpr int kLaneGap = 8;
constexpr double kPxPerBeat = 30.0;
constexpr double kDefaultBpm = 128.0;

QFont numberFont(int pixelSize) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::DemiBold);
    return font;
}

QFont labelFont(const QFont& base, int pixelSize) {
    QFont font = base;
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::DemiBold);
    return font;
}

/// Label column for one lane: title and a fixed-width bars.beat countdown,
/// right-aligned so it never jumps while counting.
void paintLabel(QPainter* pPainter,
        const QRect& rect,
        const QString& title,
        const QColor& titleColor,
        const QString& countdown,
        const QColor& countdownColor) {
    const QFont base = pPainter->font();
    pPainter->setFont(labelFont(base, 11));
    pPainter->setPen(titleColor);
    const QRect titleRect(rect.left(), rect.top(), rect.width(), rect.height() / 2);
    pPainter->drawText(titleRect, Qt::AlignLeft | Qt::AlignBottom, title);

    const QFont number = numberFont(19);
    const QFontMetrics numberMetrics(number);
    const int numberWidth = numberMetrics.horizontalAdvance(QStringLiteral("00.0"));
    const QRect numberRect(rect.left(), rect.top() + rect.height() / 2, numberWidth, rect.height() / 2);
    pPainter->setFont(number);
    pPainter->setPen(countdownColor);
    pPainter->drawText(numberRect, Qt::AlignRight | Qt::AlignTop, countdown);

    pPainter->setFont(labelFont(base, 11));
    pPainter->setPen(kTextDim);
    const QRect unitRect(numberRect.right() + 6,
            numberRect.top(),
            rect.width() - numberWidth - 6,
            numberMetrics.ascent() + 1);
    pPainter->drawText(unitRect, Qt::AlignLeft | Qt::AlignBottom, QStringLiteral("BARS"));
    pPainter->setFont(base);
}

void paintLane(QPainter* pPainter,
        const QRect& rect,
        const LaneState& state,
        const QColor& lineColor,
        Duration now,
        double pxPerSecond) {
    const double baselineY = rect.top() + rect.height() * 0.68;
    pPainter->fillRect(QRectF(rect.left(), baselineY, rect.width(), 2.0), lineColor);

    const LaneView view = laneView(state, now, rect.width() / 2.0, rect.width(), pxPerSecond);
    const QFont base = pPainter->font();
    pPainter->setFont(labelFont(base, 10));
    for (const LaneTick& tick : view.ticks) {
        const double x = rect.left() + tick.x;
        if (tick.bar) {
            pPainter->fillRect(QRectF(x - 1.0, baselineY - 16.0, 2.0, 18.0), kText);
            pPainter->setPen(kTextDim);
            pPainter->drawText(QPointF(x + 4.0, rect.top() + 10.0), QString::number(tick.barNumber));
        } else {
            pPainter->fillRect(QRectF(x - 0.5, baselineY - 8.0, 1.0, 10.0), kInactive);
        }
    }
    pPainter->setFont(labelFont(base, 10));
    for (const LaneCueMark& cue : view.cues) {
        const double x = rect.left() + cue.x;
        pPainter->fillRect(QRectF(x - 1.5, rect.top() + 4.0, 3.0, rect.height() - 8.0), kCue);
        const QFontMetrics metrics(pPainter->font());
        const QRectF tag(x + 3.0, rect.top(), metrics.horizontalAdvance(cue.label) + 6.0, 13.0);
        pPainter->fillRect(tag, kCue);
        pPainter->setPen(kPlayhead);
        pPainter->drawText(tag, Qt::AlignCenter, cue.label);
    }
    pPainter->setFont(base);
}

void paintLaneMessage(QPainter* pPainter, const QRect& rect, const QString& message) {
    pPainter->fillRect(rect, kPanel);
    const QFont base = pPainter->font();
    pPainter->setFont(labelFont(base, 12));
    pPainter->setPen(kTextDim);
    pPainter->drawText(rect, Qt::AlignCenter, message);
    pPainter->setFont(base);
}

} // namespace

WXdjBeatLanes::WXdjBeatLanes(QWidget* pParent)
        : WWidget(pParent) {
    m_refreshTimer.setInterval(kRefreshIntervalMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { update(); });
}

void WXdjBeatLanes::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);
}

void WXdjBeatLanes::showEvent(QShowEvent* pEvent) {
    WWidget::showEvent(pEvent);
    m_refreshTimer.start();
}

void WXdjBeatLanes::hideEvent(QHideEvent* pEvent) {
    WWidget::hideEvent(pEvent);
    m_refreshTimer.stop();
}

void WXdjBeatLanes::paintEvent(QPaintEvent* pEvent) {
    Q_UNUSED(pEvent);
    QPainter painter(this);
    paintLanes(&painter, rect(), latestLanes(), Time::elapsed());
}

// static
void WXdjBeatLanes::paintLanes(QPainter* pPainter,
        const QRect& rect,
        const LanesSnapshot& snapshot,
        Duration now) {
    pPainter->save();
    pPainter->setRenderHint(QPainter::Antialiasing, false);
    pPainter->fillRect(rect, kBackground);

    const int laneHeight = (rect.height() - kLaneGap) / 2;
    const QRect labelColumn(rect.left(), rect.top(), kLabelColumnWidth, rect.height());
    const QRect laneArea(rect.left() + kLabelColumnWidth + kColumnGap,
            rect.top(),
            rect.width() - kLabelColumnWidth - kColumnGap,
            rect.height());
    const QRect topLane(laneArea.left(), laneArea.top(), laneArea.width(), laneHeight);
    const QRect bottomLane(laneArea.left(),
            laneArea.bottom() - laneHeight + 1,
            laneArea.width(),
            laneHeight);

    // One time axis for both lanes, scaled to this deck's tempo.
    const double bpm = snapshot.own ? snapshot.own->bpm
            : snapshot.reference    ? snapshot.reference->bpm
                                    : kDefaultBpm;
    const double pxPerSecond = kPxPerBeat * (bpm > 0.0 ? bpm : kDefaultBpm) / 60.0;

    const QRect topLabel(labelColumn.left(), topLane.top(), labelColumn.width(), laneHeight);
    const QRect bottomLabel(labelColumn.left(), bottomLane.top(), labelColumn.width(), laneHeight);

    if (snapshot.isMaster) {
        paintLabel(pPainter, topLabel, QStringLiteral("MASTER"), kMaster, QStringLiteral("--.-"), kInactive);
        paintLaneMessage(pPainter, topLane, QStringLiteral("THIS PLAYER IS MASTER"));
    } else if (snapshot.reference) {
        paintLabel(pPainter,
                topLabel,
                QStringLiteral("MASTER"),
                kMaster,
                formatCountdown(beatsToNextCue(*snapshot.reference, now)),
                kText);
        paintLane(pPainter, topLane, *snapshot.reference, kMaster, now, pxPerSecond);
    } else {
        paintLabel(pPainter, topLabel, QStringLiteral("MASTER"), kInactive, QStringLiteral("--.-"), kInactive);
        paintLaneMessage(pPainter, topLane, QStringLiteral("NO REFERENCE"));
    }

    if (snapshot.own) {
        paintLabel(pPainter,
                bottomLabel,
                QStringLiteral("NEXT CUE"),
                kOwnLabel,
                formatCountdown(beatsToNextCue(*snapshot.own, now)),
                kText);
        paintLane(pPainter, bottomLane, *snapshot.own, kOwn, now, pxPerSecond);
    } else {
        paintLabel(pPainter, bottomLabel, QStringLiteral("NEXT CUE"), kInactive, QStringLiteral("--.-"), kInactive);
        paintLaneMessage(pPainter, bottomLane, QStringLiteral("NO BEATGRID"));
    }

    const int playheadX = laneArea.left() + laneArea.width() / 2;
    pPainter->fillRect(QRect(playheadX - 1, rect.top(), 2, rect.height()), kPlayhead);
    pPainter->restore();
}

} // namespace mixxx::xdjsync
