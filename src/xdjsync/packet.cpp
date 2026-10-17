#include "xdjsync/packet.h"

#include <QDataStream>
#include <algorithm>
#include "util/fpclassify.h"

namespace mixxx::xdjsync {

namespace {

constexpr char kMagic[] = {'X', 'D', 'J', 'S'};
constexpr int kMagicSize = sizeof(kMagic);

enum class PacketType : quint8 {
    State = 1,
    Ping = 2,
    Pong = 3,
};

constexpr quint8 kFlagPlaying = 1 << 0;

void prepare(QDataStream* pStream) {
    pStream->setByteOrder(QDataStream::LittleEndian);
    pStream->setFloatingPointPrecision(QDataStream::DoublePrecision);
}

void writeId(QDataStream* pStream, const QString& id) {
    // Oversized ids are written as-is and rejected by the receiver;
    // truncating here would silently create a different id.
    const QByteArray utf8 = id.toUtf8();
    const auto size = static_cast<quint8>(std::min<qsizetype>(utf8.size(), 255));
    *pStream << size;
    pStream->writeRawData(utf8.constData(), size);
}

void writeTime(QDataStream* pStream, Duration time) {
    *pStream << static_cast<qint64>(time.toIntegerNanos());
}

std::optional<QString> readId(QDataStream* pStream) {
    quint8 size = 0;
    *pStream >> size;
    if (size == 0 || size > kMaxPlayerIdBytes) {
        return std::nullopt;
    }
    QByteArray utf8(size, Qt::Uninitialized);
    if (pStream->readRawData(utf8.data(), size) != size) {
        return std::nullopt;
    }
    return QString::fromUtf8(utf8);
}

Duration readTime(QDataStream* pStream) {
    qint64 nanos = 0;
    *pStream >> nanos;
    return Duration::fromNanos(nanos);
}

void writeHeader(QDataStream* pStream, PacketType type) {
    pStream->writeRawData(kMagic, kMagicSize);
    *pStream << kProtocolVersion << static_cast<quint8>(type);
}

void writePayload(QDataStream* pStream, const StatePacket& state) {
    writeHeader(pStream, PacketType::State);
    writeId(pStream, state.playerId);
    *pStream << state.seq
             << static_cast<quint8>(state.playing ? kFlagPlaying : 0)
             << static_cast<quint8>(state.syncMode)
             << state.bpm
             << state.beatDistance
             << state.beatIndex;
    writeTime(pStream, state.atDac);
    writeTime(pStream, state.playingFor);
}

void writePayload(QDataStream* pStream, const PingPacket& ping) {
    writeHeader(pStream, PacketType::Ping);
    writeId(pStream, ping.playerId);
    *pStream << ping.seq;
    writeTime(pStream, ping.sent);
}

void writePayload(QDataStream* pStream, const PongPacket& pong) {
    writeHeader(pStream, PacketType::Pong);
    writeId(pStream, pong.playerId);
    writeId(pStream, pong.toPlayerId);
    *pStream << pong.seq;
    writeTime(pStream, pong.pingSent);
    writeTime(pStream, pong.pingReceived);
    writeTime(pStream, pong.pongSent);
}

std::optional<Packet> readState(QDataStream* pStream) {
    StatePacket state;
    const auto id = readId(pStream);
    if (!id) {
        return std::nullopt;
    }
    state.playerId = *id;
    quint8 flags = 0;
    quint8 syncMode = 0;
    *pStream >> state.seq >> flags >> syncMode >> state.bpm >>
            state.beatDistance >> state.beatIndex;
    state.atDac = readTime(pStream);
    if (pStream->status() != QDataStream::Ok) {
        return std::nullopt;
    }
    // Appended field, absent in older packets.
    if (!pStream->atEnd()) {
        state.playingFor = readTime(pStream);
        if (pStream->status() != QDataStream::Ok) {
            return std::nullopt;
        }
    }
    if (syncMode > static_cast<quint8>(PlayerSyncMode::Master)) {
        return std::nullopt;
    }
    if (!util_isfinite(state.bpm) || state.bpm < 0.0 ||
            !util_isfinite(state.beatDistance)) {
        return std::nullopt;
    }
    state.playing = (flags & kFlagPlaying) != 0;
    state.syncMode = static_cast<PlayerSyncMode>(syncMode);
    return state;
}

std::optional<Packet> readPing(QDataStream* pStream) {
    PingPacket ping;
    const auto id = readId(pStream);
    if (!id) {
        return std::nullopt;
    }
    ping.playerId = *id;
    *pStream >> ping.seq;
    ping.sent = readTime(pStream);
    if (pStream->status() != QDataStream::Ok) {
        return std::nullopt;
    }
    return ping;
}

std::optional<Packet> readPong(QDataStream* pStream) {
    PongPacket pong;
    const auto id = readId(pStream);
    if (!id) {
        return std::nullopt;
    }
    const auto toId = readId(pStream);
    if (!toId) {
        return std::nullopt;
    }
    pong.playerId = *id;
    pong.toPlayerId = *toId;
    *pStream >> pong.seq;
    pong.pingSent = readTime(pStream);
    pong.pingReceived = readTime(pStream);
    pong.pongSent = readTime(pStream);
    if (pStream->status() != QDataStream::Ok) {
        return std::nullopt;
    }
    return pong;
}

} // namespace

QByteArray encodePacket(const Packet& packet) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    prepare(&stream);
    std::visit([&stream](const auto& payload) { writePayload(&stream, payload); },
            packet);
    return bytes;
}

std::optional<Packet> decodePacket(const QByteArray& bytes) {
    if (bytes.size() < kMagicSize + 2 ||
            !bytes.startsWith(QByteArray::fromRawData(kMagic, kMagicSize))) {
        return std::nullopt;
    }
    QDataStream stream(bytes);
    prepare(&stream);
    stream.skipRawData(kMagicSize);
    quint8 version = 0;
    quint8 type = 0;
    stream >> version >> type;
    if (version != kProtocolVersion) {
        return std::nullopt;
    }
    switch (static_cast<PacketType>(type)) {
    case PacketType::State:
        return readState(&stream);
    case PacketType::Ping:
        return readPing(&stream);
    case PacketType::Pong:
        return readPong(&stream);
    }
    return std::nullopt;
}

} // namespace mixxx::xdjsync
