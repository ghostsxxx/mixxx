#include <QDir>
#include <QImage>
#include <QPainter>

#include "test/mixxxtest.h"
#include "xdjsync/wxdjbeatlanes.h"

namespace {

using mixxx::Duration;
using namespace mixxx::xdjsync;

constexpr int kWidth = 620;
constexpr int kHeight = 84;

Duration ms(double millis) {
    return Duration::fromNanos(static_cast<qint64>(millis * 1e6));
}

class XdjSyncBeatLanesTest : public MixxxTest {
  protected:
    /// Renders and, if XDJSYNC_RENDER_DIR is set, saves a PNG for review.
    QImage render(const LanesSnapshot& snapshot, const QString& name) {
        QImage image(kWidth, kHeight, QImage::Format_RGB32);
        QPainter painter(&image);
        WXdjBeatLanes::paintLanes(&painter, image.rect(), snapshot, ms(1000));
        painter.end();
        const QString dir = qEnvironmentVariable("XDJSYNC_RENDER_DIR");
        if (!dir.isEmpty()) {
            image.save(QDir(dir).filePath(name + QStringLiteral(".png")));
        }
        return image;
    }

    static LaneState lane(double beatPosition, QList<LaneCue> cues = {}) {
        return LaneState{true, 128.0, beatPosition, ms(1000), std::move(cues)};
    }

    static int playheadX() {
        // Label column 132 + gap 12, then the middle of the lane area.
        return 144 + (kWidth - 144) / 2;
    }

    static bool isColor(const QImage& image, int x, int y, QRgb rgb) {
        return image.pixel(x, y) == (rgb | 0xff000000);
    }

    static bool columnHasColor(const QImage& image, int x, int fromY, int toY, QRgb rgb) {
        for (int y = fromY; y < toY; ++y) {
            if (isColor(image, x, y, rgb)) {
                return true;
            }
        }
        return false;
    }
};

TEST_F(XdjSyncBeatLanesTest, OneBeatOff) {
    LanesSnapshot snapshot;
    snapshot.reference = lane(37.6, {LaneCue{64.0, 0}, LaneCue{128.0, 1}});
    snapshot.own = lane(38.612, {LaneCue{72.0, 0}, LaneCue{136.0, 1}});
    const QImage image = render(snapshot, QStringLiteral("lanes_one_beat_off"));
    // Playhead, orange master baseline on top, blue own baseline below.
    EXPECT_TRUE(isColor(image, playheadX(), kHeight / 2, 0xffffff));
    EXPECT_TRUE(columnHasColor(image, 150, 0, kHeight / 2, 0xeb870f));
    EXPECT_TRUE(columnHasColor(image, 150, kHeight / 2, kHeight, 0x2d85cd));
}

TEST_F(XdjSyncBeatLanesTest, InSync) {
    LanesSnapshot snapshot;
    snapshot.reference = lane(37.6, {LaneCue{40.0, 1}});
    snapshot.own = lane(37.6, {LaneCue{36.0, 0}, LaneCue{72.0, 2}});
    const QImage image = render(snapshot, QStringLiteral("lanes_in_sync"));
    // Red cue flag for the master's hot cue B, 2.4 beats ahead.
    const int cueX = playheadX() + static_cast<int>(2.4 * 30.0);
    EXPECT_TRUE(columnHasColor(image, cueX, 0, kHeight / 2, 0xe0413a));
}

TEST_F(XdjSyncBeatLanesTest, Master) {
    LanesSnapshot snapshot;
    snapshot.own = lane(37.6, {LaneCue{72.0, 0}});
    snapshot.isMaster = true;
    const QImage image = render(snapshot, QStringLiteral("lanes_master"));
    // Message panel instead of a master lane.
    EXPECT_TRUE(isColor(image, 150, 10, 0x0d0d10));
}

TEST_F(XdjSyncBeatLanesTest, NoReference) {
    LanesSnapshot snapshot;
    snapshot.own = lane(37.6);
    render(snapshot, QStringLiteral("lanes_no_reference"));
}

} // namespace
