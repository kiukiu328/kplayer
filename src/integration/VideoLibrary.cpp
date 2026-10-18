#include "VideoLibrary.h"
#include "VideoFormats.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QCollator>
#include <QDesktopServices>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#endif
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

QList<QUrl> folderVideos(const QDir &folder)
{
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    auto entries = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    std::stable_sort(entries.begin(), entries.end(), [&](const auto &a, const auto &b) {
        return collator.compare(a.fileName(), b.fileName()) < 0;
    });
    QList<QUrl> videos;
    for (const QFileInfo &entry : entries) {
        if (VideoFormats::extensions().contains(entry.suffix().toLower())
            && hasTransportStreamHeader(entry))
            videos.append(QUrl::fromLocalFile(entry.absoluteFilePath()));
    }
    return videos;
}
}

int VideoLibrary::addSiblingVideos(PlaylistModel *playlist, const QList<QUrl> &files)
{
    if (!playlist)
        return 0;
    QSet<QString> visited;
    QList<QUrl> siblings;
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
        siblings.append(folderVideos(folder));
    }
    const int added = playlist->appendUniqueUrls(siblings);
    // The opened file is already in the queue; include it in the natural order
    // instead of leaving it ahead of earlier episodes. Sorting preserves playback.
    if (added > 0)
        playlist->sortByTitle();
    return added;
}

bool VideoLibrary::canOpenFileLocation(const QUrl &source) const
{
    return source.isLocalFile() && !source.toLocalFile().isEmpty()
        && QFileInfo(QFileInfo(source.toLocalFile()).absolutePath()).isDir();
}

bool VideoLibrary::openFileLocation(const QUrl &source)
{
    if (!canOpenFileLocation(source))
        return false;
    const QFileInfo file(source.toLocalFile());
#ifdef Q_OS_WIN
    // Use the shell API so Unicode, spaces, and commas are not command-line arguments.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const QString path = QDir::toNativeSeparators(file.absoluteFilePath());
    auto *item = ILCreateFromPathW(reinterpret_cast<LPCWSTR>(path.utf16()));
    const bool selected = item && SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    if (item)
        ILFree(item);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (selected)
        return true;
#endif
    // Also open the containing folder when a file has been moved or deleted.
    return QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath()));
}

int VideoLibrary::addFolderPlaylist(PlaylistCollection *playlists, const QUrl &url)
{
    if (!playlists || !url.isLocalFile() || !QFileInfo(url.toLocalFile()).isDir())
        return -1;
    const QDir folder(url.toLocalFile());
    const QList<QUrl> videos = folderVideos(folder);
    if (videos.isEmpty())
        return -1;
    QString baseName = folder.dirName();
    if (baseName.isEmpty())
        baseName = QDir::toNativeSeparators(folder.absolutePath());
    QString name = baseName;
    for (int number = 2; !playlists->nameAvailable(name); ++number)
        name = QStringLiteral("%1 (%2)").arg(baseName).arg(number);
    const int index = playlists->addPlaylist(name);
    playlists->currentPlaylist()->appendUrls(videos);
    return index;
}

int VideoLibrary::addDroppedUrls(PlaylistCollection *playlists, const QList<QUrl> &urls)
{
    if (!playlists)
        return 0;
    // Files retain their explicit order in the original queue; each folder gets its own tab.
    int added = playlists->currentPlaylist()->appendUrls(urls);
    QSet<QString> visited;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isDir())
            continue;
        const QString key = PlaylistModel::sourceKey(url);
        if (visited.contains(key))
            continue;
        visited.insert(key);
        if (addFolderPlaylist(playlists, url) >= 0)
            added += playlists->currentPlaylist()->count();
    }
    return added;
}

int VideoLibrary::addDroppedUrlsToPlaylist(PlaylistModel *playlist, const QList<QUrl> &urls)
{
    if (!playlist)
        return 0;
    QList<QUrl> tracks;
    QSet<QString> visited;
    const auto append = [&](const QUrl &url) {
        const QString key = PlaylistModel::sourceKey(url);
        if (!visited.contains(key)) {
            visited.insert(key);
            tracks.append(url);
        }
    };
    for (const QUrl &url : urls) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
            for (const QUrl &video : folderVideos(QDir(url.toLocalFile())))
                append(video);
        } else {
            append(url);
        }
    }
    return playlist->appendUrls(tracks);
}
