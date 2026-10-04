#include "VideoLibrary.h"
#include "VideoFormats.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QCollator>
#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
bool hasTransportStreamHeader(const QFileInfo &entry)
{
    // .ts and .mts also name TypeScript sources. Check packet sync bytes before
    // adding these ambiguous extensions during a whole-drive search.
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

VideoLibrary::~VideoLibrary()
{
    if (m_worker) {
        m_worker->requestInterruption();
        m_worker->wait();
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

QList<QUrl> VideoLibrary::defaultRoots()
{
    QList<QUrl> roots;
#ifdef Q_OS_WIN
    for (const QFileInfo &drive : QDir::drives()) {
        const QString path = QDir::toNativeSeparators(drive.absoluteFilePath());
        const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(path.utf16()));
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE)
            roots.append(QUrl::fromLocalFile(drive.absoluteFilePath()));
    }
#else
    for (const QString &path : QStandardPaths::standardLocations(QStandardPaths::MoviesLocation))
        roots.append(QUrl::fromLocalFile(path));
#endif
    return roots;
}

QString VideoLibrary::status() const
{
    if (m_scanning)
        return m_canceled ? tr("Stopping video search…")
            : tr("Finding videos… %1 added · %2 folders searched")
                  .arg(m_addedCount).arg(m_foldersVisited);
    return {};
}

void VideoLibrary::cancel()
{
    if (!m_worker || !m_scanning)
        return;
    m_canceled = true;
    m_worker->requestInterruption();
    emit progressChanged();
}

bool VideoLibrary::scan(PlaylistModel *playlist, const QList<QUrl> &requestedRoots)
{
    if (!playlist || m_scanning)
        return false;
    const QList<QUrl> roots = requestedRoots.isEmpty() ? defaultRoots() : requestedRoots;
    const QPointer<PlaylistModel> target(playlist);
    m_scanning = true;
    m_canceled = false;
    m_addedCount = 0;
    m_foldersVisited = 0;
    emit scanningChanged();
    emit progressChanged();

    m_worker = QThread::create([this, target, roots] {
        QStringList pending;
        for (const QUrl &root : roots) {
            if (root.isLocalFile() && QFileInfo(root.toLocalFile()).isDir())
                pending.append(root.toLocalFile());
        }
        QSet<QString> visited;
        const QSet<QString> extensions(VideoFormats::extensions().begin(),
                                       VideoFormats::extensions().end());
        QList<QUrl> batch;
        int folders = 0;
        auto publish = [&] {
            const QList<QUrl> urls = batch;
            batch.clear();
            QMetaObject::invokeMethod(this, [this, target, urls, folders] {
                if (m_canceled)
                    return;
                if (target)
                    m_addedCount += target->appendUniqueUrls(urls);
                else
                    cancel();
                m_foldersVisited = folders;
                emit progressChanged();
            }, Qt::QueuedConnection);
        };
        while (!pending.isEmpty() && !QThread::currentThread()->isInterruptionRequested()) {
            const QDir folder(pending.takeLast());
            QString key = folder.canonicalPath();
#ifdef Q_OS_WIN
            key = key.toCaseFolded();
#endif
            if (key.isEmpty() || visited.contains(key))
                continue;
            visited.insert(key);
            ++folders;
            const auto entries = folder.entryInfoList(
                QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot
                    | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo &entry : entries) {
                if (QThread::currentThread()->isInterruptionRequested())
                    break;
                // Junctions can lead out of a requested root or back into an ancestor.
                if (entry.isSymLink() || entry.isJunction())
                    continue;
                if (entry.isDir()) {
                    const QString name = entry.fileName();
                    if (name.compare("$Recycle.Bin", Qt::CaseInsensitive) != 0
                        && name.compare("System Volume Information", Qt::CaseInsensitive) != 0)
                        pending.append(entry.absoluteFilePath());
                } else if (entry.isFile() && extensions.contains(entry.suffix().toLower())
                           && hasTransportStreamHeader(entry)) {
                    batch.append(QUrl::fromLocalFile(entry.absoluteFilePath()));
                    if (batch.size() >= 64)
                        publish();
                }
            }
            if (folders % 100 == 0 || !batch.isEmpty())
                publish();
        }
        publish();
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this] {
        m_worker->deleteLater();
        m_worker = nullptr;
        m_scanning = false;
        emit scanningChanged();
        emit progressChanged();
        emit finished(m_canceled);
    });
    m_worker->start();
    return true;
}
