#include "VideoLibrary.h"
#include "VideoFormats.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QCollator>
#include <algorithm>

namespace {
bool hasTransportStreamHeader(const QFileInfo &entry)
{
    // .ts and .mts also name TypeScript sources. Check packet sync bytes before
    // adding these ambiguous extensions from the opened file's folder.
    const QString suffix = entry.suffix().toLower();
    if (suffix != "ts" && suffix != "mts")
        return true;
    QFile file(entry.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(4096);
    // MPEG-TS, Blu-ray/DVHS, and FEC packet lengths used by FFmpeg's demuxer:
    // https://ffmpeg.org/doxygen/trunk/mpegts_8h.html
    for (int packetSize : {188, 192, 204}) {
        for (int offset = 0; offset + 3 * packetSize < header.size(); ++offset) {
            if (header.at(offset) == char(0x47)
                && header.at(offset + packetSize) == char(0x47)
                && header.at(offset + 2 * packetSize) == char(0x47)
                && header.at(offset + 3 * packetSize) == char(0x47))
                return true;
        }
    }
    return false;
}
}

int VideoLibrary::addSiblingVideos(PlaylistModel *playlist, const QList<QUrl> &files)
{
    if (!playlist)
        return 0;
    QSet<QString> visited;
    QList<QUrl> siblings;
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    for (const QUrl &url : files) {
        const QFileInfo file(url.toLocalFile());
        if (!url.isLocalFile() || !file.isFile()
            || !VideoFormats::extensions().contains(file.suffix().toLower()))
            continue;
        const QDir folder = file.absoluteDir();
        QString key = folder.canonicalPath();
#ifdef Q_OS_WIN
        key = key.toCaseFolded();
#endif
        if (visited.contains(key))
            continue;
        visited.insert(key);
        auto entries = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
        std::stable_sort(entries.begin(), entries.end(), [&](const auto &a, const auto &b) {
            return collator.compare(a.fileName(), b.fileName()) < 0;
        });
        for (const QFileInfo &entry : entries) {
            if (VideoFormats::extensions().contains(entry.suffix().toLower())
                && hasTransportStreamHeader(entry))
                siblings.append(QUrl::fromLocalFile(entry.absoluteFilePath()));
        }
    }
    const int added = playlist->appendUniqueUrls(siblings);
    // The opened file is already in the queue; include it in the natural order
    // instead of leaving it ahead of earlier episodes. Sorting preserves playback.
    if (added > 0)
        playlist->sortByTitle();
    return added;
}
