#include "MpvVideo.h"
#include "MpvPreview.h"
#include "MpvErrors.h"
#include "PlaylistModel.h"
#include "Favorites.h"
#include "VideoLibrary.h"
#include "FileAssociations.h"
#include "VideoFormats.h"
#include "AppLanguage.h"
#include "FullscreenCursor.h"
#include <QDesktopServices>

#include <QAbstractItemModelTester>
#include <QDataStream>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMimeData>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QSettings>
#include <QScreen>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QWheelEvent>
#include <QScopeGuard>
#include <QSaveFile>
#include <QTimer>
#include <QQuickItemGrabResult>
#include <QPointer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <clocale>
#include <atomic>
#include <utility>

class FileLocationReceiver : public QObject
{
    Q_OBJECT
public:
    QUrl opened;
public slots:
    void capture(const QUrl &url) { opened = url; }
};

class PlayerTests : public QObject
{
    Q_OBJECT

    static QUrl makeAudio(const QString &path)
    {
        // A self-contained, seekable 30-second PCM WAV fixture.
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        const QByteArray samples(8000 * 30 * 2, '\0');
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + samples.size());
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000)
               << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(samples.size());
        stream.writeRawData(samples.constData(), samples.size());
        return QUrl::fromLocalFile(path);
    }

    static QUrl makeVideo(const QString &path, bool reversed = false)
    {
        // Raw Y4M needs no external encoder and provides seekable, distinguishable frames.
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write("YUV4MPEG2 W64 H36 F1:1 Ip A1:1 C420jpeg\n");
        const QByteArray chroma(64 * 36 / 4, char(128));
        for (int frame = 0; frame < 30; ++frame) {
            const bool bright = (frame >= 15) != reversed;
            file.write("FRAME\n");
            file.write(QByteArray(64 * 36, bright ? char(220) : char(32)));
            file.write(chroma);
            file.write(chroma);
        }
        return QUrl::fromLocalFile(path);
    }

    static double previewBrightness(QQuickWindow *window, QQuickItem *preview)
    {
        const QImage screenshot = window->grabWindow();
        if (screenshot.isNull())
            return -1;
        const QString capturePath = qEnvironmentVariable("KPLAYER_TEST_CAPTURE");
        if (!capturePath.isEmpty())
            screenshot.save(capturePath);
        // Inspect the center of the video, away from chrome and letterboxing.
        const QPointF center = preview->mapToScene(
                {preview->width() / 2, preview->height() / 2});
        const qreal scaleX = qreal(screenshot.width()) / window->width();
        const qreal scaleY = qreal(screenshot.height()) / window->height();
        const QRect sample(qRound(center.x() * scaleX) - 10,
                           qRound(center.y() * scaleY) - 10, 20, 20);
        if (!screenshot.rect().contains(sample))
            return -1;
        double total = 0;
        for (int y = sample.top(); y <= sample.bottom(); ++y)
            for (int x = sample.left(); x <= sample.right(); ++x)
                total += qGray(screenshot.pixel(x, y));
        return total / (sample.width() * sample.height());
    }

    static void click(QQuickWindow *window, QQuickItem *item,
                      Qt::MouseButton button = Qt::LeftButton)
    {
        // Model changes and popup opening can precede Qt Quick's layout pass.
        QSignalSpy renderedFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!renderedFrames.isEmpty(), 5000);
        QTest::mouseClick(window, button, Qt::NoModifier,
                         item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    }

    static bool exposeWindow(QQuickWindow *window)
    {
        if (!window || !QTest::qWaitForWindowExposed(window))
            return false;
        // Shortcuts require an active native window, not just an exposed one.
        window->requestActivate();
        return QTest::qWaitForWindowActive(window);
    }

    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
    {
        if (parent->objectName() == name)
            return parent;
        for (auto *child : parent->childItems()) {
            if (auto *item = findItem(child, name))
                return item;
        }
        return nullptr;
    }

    static QQuickItem *menuItemAt(QObject *menu, int index)
    {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item),
                                  Q_ARG(int, index));
        return item;
    }

private slots:
    void init()
    {
        QSettings().clear();
    }

    void savedPlaylistsPreserveQueuesAcrossRestarts()
    {
        QTemporaryDir directory;
        const QUrl local = makeVideo(directory.filePath("episode.y4m"));
        const QUrl remote("https://example.com/episode2.mp4");
        {
            PlaylistCollection playlists;
            playlists.setPersistent(true);
            auto *original = playlists.currentPlaylist();
            original->appendUrls({remote, local, remote});
            original->setCurrentIndex(2);
            QCOMPARE(playlists.saveCurrentPlaylistAs("  Saved  "), 1);
            auto *saved = playlists.currentPlaylist();
            QVERIFY(saved != original);
            QCOMPARE(saved->count(), 3);
            QCOMPARE(saved->currentIndex(), 2);
            QCOMPARE(playlists.saveCurrentPlaylistAs("saved"), -1);
            QVERIFY(saved->moveTrack(1, 0));
            QCOMPARE(original->data(original->index(0), PlaylistModel::SourceRole).toUrl(), remote);
            QVERIFY(playlists.renamePlaylist(1, "Episodes"));
            original->clear();
            QCOMPARE(saved->count(), 3);
            QCOMPARE(playlists.addPlaylist("Remove me"), 2);
            QVERIFY(playlists.removePlaylist(2));
        }
        // An unavailable local file remains in the saved queue for later use.
        QVERIFY(QFile::remove(local.toLocalFile()));
        {
            PlaylistCollection restored;
            restored.setPersistent(true);
            QCOMPARE(restored.count(), 2);
            QCOMPARE(restored.currentIndex(), 1);
            QCOMPARE(restored.defaultPlaylist()->count(), 0);
            restored.setCurrentIndex(1);
            QCOMPARE(restored.currentName(), QStringLiteral("Episodes"));
            auto *saved = restored.currentPlaylist();
            QCOMPARE(saved->count(), 3);
            QCOMPARE(saved->data(saved->index(0), PlaylistModel::SourceRole).toUrl(), local);
            QCOMPARE(saved->data(saved->index(1), PlaylistModel::SourceRole).toUrl(), remote);
            QCOMPARE(saved->data(saved->index(2), PlaylistModel::SourceRole).toUrl(), remote);
            QCOMPARE(saved->currentIndex(), 2);
            QCOMPARE(saved->appendUniqueUrls({remote}), 0);
            saved->clear();
        }
        PlaylistCollection empty;
        empty.setPersistent(true);
        empty.setCurrentIndex(1);
        QCOMPARE(empty.currentPlaylist()->count(), 0);
    }

    void startupRestoresLastPlaylistUnlessOpeningVideo()
    {
        QTemporaryDir previousDirectory;
        QTemporaryDir openedDirectory;
        const QUrl first = makeVideo(previousDirectory.filePath("first.y4m"));
        const QUrl second = makeVideo(previousDirectory.filePath("second.y4m"));
        const QUrl opened = makeVideo(openedDirectory.filePath("episode10.y4m"));
        const QUrl sibling = makeVideo(openedDirectory.filePath("episode2.y4m"));
        {
            PlaylistCollection playlists;
            playlists.setPersistent(true);
            playlists.defaultPlaylist()->appendUrls({first, second, first});
            playlists.defaultPlaylist()->setCurrentIndex(2);
            playlists.addPlaylist("Saved");
            playlists.currentPlaylist()->appendUrls({second});
            playlists.setCurrentIndex(0);
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *playlists = window->findChild<PlaylistCollection *>();
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(playlists && video);
            QCOMPARE(playlists->count(), 2);
            QCOMPARE(playlists->currentIndex(), 0);
            QCOMPARE(playlists->defaultPlaylist()->count(), 3);
            QCOMPARE(playlists->defaultPlaylist()->currentIndex(), 2);
            QCOMPARE(playlists->defaultPlaylist()->data(playlists->defaultPlaylist()->index(1),
                                                       PlaylistModel::SourceRole).toUrl(), second);
            QCOMPARE(video->source(), first);
            QVERIFY(video->stopped());
            playlists->setCurrentIndex(1);
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *playlists = window->findChild<PlaylistCollection *>();
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(playlists && video);
            QCOMPARE(playlists->currentIndex(), 1);
            QCOMPARE(playlists->currentName(), QStringLiteral("Saved"));
            QCOMPARE(playlists->defaultPlaylist()->count(), 3);
            QCOMPARE(video->source(), second);
            QVERIFY(video->stopped());
        }
        {
            QQmlApplicationEngine engine;
            QSignalSpy warnings(&engine, &QQmlEngine::warnings);
            engine.setInitialProperties({{"startupFiles", QVariant::fromValue(QList<QUrl>{opened})}});
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *playlists = window->findChild<PlaylistCollection *>();
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(playlists && video);
            QTRY_VERIFY(video->playing() && video->mediaLoaded());
            QCOMPARE(playlists->currentIndex(), 0);
            auto *queue = playlists->defaultPlaylist();
            QCOMPARE(queue->count(), 2);
            QCOMPARE(queue->indexOfSource(sibling), 0);
            QCOMPARE(queue->currentSource(), opened);
            QCOMPARE(queue->indexOfSource(first), -1);
            QCOMPARE(queue->indexOfSource(second), -1);
            QCOMPARE(video->source(), opened);
            QCOMPARE(video->loadRevision(), quint64(1));
            QCOMPARE(playlists->count(), 2);
            playlists->setCurrentIndex(1);
            QCOMPARE(playlists->currentPlaylist()->count(), 1);
            QCOMPARE(playlists->currentPlaylist()->currentSource(), second);
            playlists->setCurrentIndex(0);
            QVERIFY2(warnings.isEmpty(), "Startup playlist replacement generated QML warnings");
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *playlists = window->findChild<PlaylistCollection *>();
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(playlists && video);
            QCOMPARE(playlists->currentIndex(), 0);
            QCOMPARE(playlists->defaultPlaylist()->count(), 2);
            QCOMPARE(video->source(), opened);
            QVERIFY(video->stopped());
        }
    }

    void startupScrollsToLastPlayedTrack_data()
    {
        QTest::addColumn<int>("playlistIndex");
        QTest::addColumn<bool>("initiallyHidden");
        QTest::newRow("default-visible") << 0 << false;
        QTest::newRow("default-hidden") << 0 << true;
        QTest::newRow("saved-visible") << 1 << false;
        QTest::newRow("saved-hidden") << 1 << true;
    }

    void startupScrollsToLastPlayedTrack()
    {
        QFETCH(int, playlistIndex);
        QFETCH(bool, initiallyHidden);
        QTemporaryDir directory;
        const QUrl lastPlayed = makeVideo(directory.filePath("last-played.y4m"));
        QVERIFY(!lastPlayed.isEmpty());
        QList<QUrl> tracks;
        for (int i = 0; i < 100; ++i)
            tracks.append(QUrl(QStringLiteral("https://example.com/episode%1.mkv").arg(i)));
        const int lastIndex = 75;
        tracks[lastIndex] = lastPlayed;
        {
            PlaylistCollection playlists;
            playlists.setPersistent(true);
            playlists.defaultPlaylist()->appendUrls(tracks);
            playlists.defaultPlaylist()->setCurrentIndex(lastIndex);
            playlists.addPlaylist("Saved");
            playlists.currentPlaylist()->appendUrls(tracks);
            playlists.currentPlaylist()->setCurrentIndex(lastIndex);
            playlists.setCurrentIndex(playlistIndex);
        }
        QSettings().setValue("window/playlistShown", !initiallyHidden);

        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *playlists = window->findChild<PlaylistCollection *>();
        auto *video = window->findChild<MpvVideo *>();
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        QVERIFY(playlists && video && panel);
        auto *list = findItem(panel, "trackList");
        QVERIFY(list);
        QCOMPARE(playlists->currentIndex(), playlistIndex);
        QCOMPARE(playlists->currentPlaylist()->currentIndex(), lastIndex);
        QCOMPARE(video->source(), lastPlayed);
        QVERIFY(video->stopped());
        QCOMPARE(panel->isVisible(), !initiallyHidden);
        if (initiallyHidden)
            window->setProperty("playlistVisible", true);
        QTRY_VERIFY(panel->isVisible());
        QTRY_COMPARE(list->property("currentIndex").toInt(), lastIndex);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(list, QStringLiteral("track%1").arg(lastIndex))));
        QTRY_VERIFY(row->mapToItem(list, QPointF()).y() >= -0.5);
        QTRY_VERIFY(row->mapToItem(list, QPointF(0, row->height())).y() <= list->height() + 0.5);
        QCOMPARE(panel->property("selectedRows").toList(), QVariantList{lastIndex});
        QVERIFY(video->stopped());
        QCOMPARE(video->source(), lastPlayed);
        QVERIFY2(warnings.isEmpty(), "Startup playlist positioning generated QML warnings");
    }

    void folderImportsCreateNaturallySortedIndependentPlaylists()
    {
        QTemporaryDir directory;
        QDir folder(directory.path());
        QVERIFY(folder.mkpath("Series/nested"));
        QVERIFY(folder.mkdir("empty"));
        const QUrl ten = makeVideo(directory.filePath("Series/episode10.y4m"));
        const QUrl two = makeVideo(directory.filePath("Series/episode2.y4m"));
        makeVideo(directory.filePath("Series/nested/episode1.y4m"));
        QFile source(directory.filePath("Series/source.ts"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("export const x = 1;");
        source.close();
        PlaylistCollection playlists;
        QAbstractItemModelTester tester(&playlists,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QUrl original("https://example.com/original.mp4");
        playlists.currentPlaylist()->appendUrls({original});
        VideoLibrary library;
        const QUrl series = QUrl::fromLocalFile(directory.filePath("Series"));
        QCOMPARE(library.addFolderPlaylist(&playlists, series), 1);
        QCOMPARE(playlists.currentName(), QStringLiteral("Series"));
        QCOMPARE(playlists.currentPlaylist()->count(), 2);
        QCOMPARE(playlists.currentPlaylist()->currentSource(), two);
        QCOMPARE(playlists.currentPlaylist()->data(playlists.currentPlaylist()->index(1),
                                                  PlaylistModel::SourceRole).toUrl(), ten);
        QCOMPARE(playlists.defaultPlaylist()->currentSource(), original);
        QCOMPARE(library.addFolderPlaylist(&playlists, QUrl::fromLocalFile(directory.filePath("empty"))), -1);
        QCOMPARE(library.addFolderPlaylist(&playlists, QUrl("https://example.com/folder")), -1);
        QCOMPARE(playlists.count(), 2);
        playlists.setCurrentIndex(0);
        QCOMPARE(library.addDroppedUrls(&playlists, {two, series, series}), 3);
        QCOMPARE(playlists.defaultPlaylist()->count(), 2);
        QCOMPARE(playlists.currentName(), QStringLiteral("Series (2)"));
        QCOMPARE(playlists.count(), 3);
        QCOMPARE(playlists.currentPlaylist()->count(), 2);
    }

    void playlistFolderAndSaveActions()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("episode2.y4m"));
        const QUrl second = makeVideo(directory.filePath("episode10.y4m"));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        QVERIFY(!item("addMediaTab"));
        auto *playlists = window->findChild<PlaylistCollection *>();
        auto *menu = window->findChild<QObject *>("playlistOptionsMenu");
        auto *dialog = window->findChild<QObject *>("playlistNameDialog");
        auto *folderDialog = window->findChild<QObject *>("playlistFolderDialog");
        QVERIFY(playlists && menu && dialog && folderDialog);
        auto *original = playlists->currentPlaylist();
        original->appendUrls({second, first});
        original->setCurrentIndex(1);
        click(window, item("playlistOptionsButton"));
        QTRY_VERIFY(menu->property("opened").toBool());
        click(window, item("saveCurrentPlaylistMenuItem"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        item("playlistNameField")->setProperty("text", "Saved queue");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(playlists->count(), 2);
        QCOMPARE(playlists->currentPlaylist()->count(), 3);
        QCOMPARE(playlists->currentPlaylist()->currentIndex(), 1);
        QCOMPARE(original->count(), 3);
        click(window, item("playlistOptionsButton"));
        QTRY_VERIFY(menu->property("opened").toBool());
        click(window, item("addFolderPlaylistMenuItem"));
        QTRY_VERIFY(folderDialog->property("visible").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(folderDialog, "close"));
        QVERIFY(folderDialog->setProperty("selectedFolder", QUrl::fromLocalFile(directory.path())));
        QVERIFY(QMetaObject::invokeMethod(folderDialog, "accepted"));
        QTRY_COMPARE(playlists->count(), 3);
        QCOMPARE(playlists->currentPlaylist()->count(), 2);
        QCOMPARE(playlists->currentPlaylist()->currentSource(), first);
        QVERIFY(exposeWindow(window));
        auto *tabs = item("playlistTabs");
        const QPoint point = tabs->mapToScene({tabs->width() / 2, tabs->height() / 2}).toPoint();
        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(directory.path())});
        QDragEnterEvent enter(point, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(point, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(playlists->count(), 4);
        QCOMPARE(playlists->currentPlaylist()->count(), 2);
        click(window, item("playlistOptionsButton"));
        QTRY_VERIFY(menu->property("opened").toBool());
        const QString capturePath = qEnvironmentVariable("KPLAYER_PLAYLIST_CAPTURE");
        if (!capturePath.isEmpty())
            QVERIFY(window->grabWindow().save(capturePath));
        QVERIFY2(warnings.isEmpty(), "Playlist folder/save actions generated QML warnings");
    }

    void playlistFolderDropTargetsAndMiddleClick()
    {
        QTemporaryDir directory;
        QDir folder(directory.path());
        QVERIFY(folder.mkdir("Series"));
        QVERIFY(folder.mkdir("Extra"));
        QVERIFY(folder.mkdir("empty"));
        const QUrl originalSource = makeVideo(directory.filePath("original.y4m"));
        const QUrl ten = makeVideo(directory.filePath("Series/episode10.y4m"));
        const QUrl two = makeVideo(directory.filePath("Series/episode2.y4m"));
        const QUrl extra = makeVideo(directory.filePath("Extra/extra.y4m"));
        const QUrl seriesFolder = QUrl::fromLocalFile(directory.filePath("Series"));
        const QUrl extraFolder = QUrl::fromLocalFile(directory.filePath("Extra"));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.setInitialProperties({{"mediaSource", originalSource}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *playlists = window->findChild<PlaylistCollection *>();
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(playlists && video && item("trackList") && item("playlistTabs"));
        auto *original = playlists->currentPlaylist();
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        const quint64 revision = video->loadRevision();
        const auto center = [](QQuickItem *target) {
            return target->mapToScene({target->width() / 2, target->height() / 2}).toPoint();
        };
        const auto dropUrls = [&](const QList<QUrl> &urls, const QPoint &start, const QPoint &end) {
            QMimeData files;
            files.setUrls(urls);
            QDragEnterEvent enter(start, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            QDragMoveEvent move(end, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &move);
            QDropEvent drop(end, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &drop);
            return drop.isAccepted();
        };

        // A drop over an existing tab creates a separate, naturally sorted queue.
        QVERIFY(dropUrls({seriesFolder}, center(item("trackList")), center(item("playlistTab0"))));
        QCOMPARE(playlists->count(), 2);
        auto *series = playlists->currentPlaylist();
        QCOMPARE(playlists->currentName(), QStringLiteral("Series"));
        QCOMPARE(series->count(), 2);
        QCOMPARE(series->currentSource(), two);
        QCOMPARE(series->data(series->index(1), PlaylistModel::SourceRole).toUrl(), ten);
        QCOMPARE(original->count(), 1);

        // Moving from the tab bar into the content appends to the selected queue.
        QVERIFY(dropUrls({extraFolder}, center(item("playlistTabs")), center(item("trackList"))));
        QCOMPARE(playlists->count(), 2);
        QCOMPARE(playlists->currentPlaylist(), series);
        QCOMPARE(series->count(), 3);
        QCOMPARE(series->data(series->index(2), PlaylistModel::SourceRole).toUrl(), extra);
        QCOMPARE(series->currentSource(), two);

        // Mixed file/folder drops expand each file once and preserve explicit order.
        const QPoint content = center(item("trackList"));
        QVERIFY(dropUrls({extra, seriesFolder, seriesFolder}, content, content));
        QCOMPARE(series->count(), 6);
        QCOMPARE(series->data(series->index(3), PlaylistModel::SourceRole).toUrl(), extra);
        QCOMPARE(series->data(series->index(4), PlaylistModel::SourceRole).toUrl(), two);
        QCOMPARE(series->data(series->index(5), PlaylistModel::SourceRole).toUrl(), ten);
        QVERIFY(!dropUrls({QUrl::fromLocalFile(directory.filePath("empty"))}, content, content));
        QCOMPARE(series->count(), 6);

        // The unused part of the tab strip is also a tab drop target.
        auto *tabs = item("playlistTabs");
        const QPoint blank = tabs->mapToScene({tabs->width() - 2, tabs->height() / 2}).toPoint();
        QVERIFY(dropUrls({extraFolder}, content, blank));
        QCOMPARE(playlists->count(), 3);
        auto *extraQueue = playlists->currentPlaylist();
        QCOMPARE(extraQueue->count(), 1);
        QCOMPARE(video->source(), originalSource);
        QCOMPARE(video->loadRevision(), revision);
        QVERIFY(video->playing());

        click(window, item("playlistTab1"), Qt::MiddleButton);
        QTRY_COMPARE(playlists->count(), 2);
        QCOMPARE(playlists->currentPlaylist(), extraQueue);
        QCOMPARE(playlists->currentIndex(), 1);
        click(window, item("playlistTab1"), Qt::MiddleButton);
        QTRY_COMPARE(playlists->count(), 1);
        QCOMPARE(playlists->currentPlaylist(), original);
        click(window, item("playlistTab0"), Qt::MiddleButton);
        QCOMPARE(playlists->count(), 1);
        QCOMPARE(video->source(), originalSource);
        QCOMPARE(video->loadRevision(), revision);
        QVERIFY(video->playing());
        QVERIFY2(warnings.isEmpty(), "Playlist drop/middle-click generated QML warnings");
    }

    void responsiveControlsFitInBothLanguages()
    {
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &errors) { warnings.append(errors); });
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) { return findItem(window->contentItem(), name); };
        auto *preferences = window->findChild<QObject *>("playbackPreferences");
        auto *language = engine.singletonInstance<AppLanguage *>("KPlayer", "AppLanguage");
        QVERIFY(preferences && language);
        const QVariant savedWidth = preferences->property("playlistWidth");
        const QString savedLanguage = language->language();
        const auto restore = qScopeGuard([&] {
            preferences->setProperty("playlistWidth", savedWidth);
            language->setLanguage(savedLanguage);
        });
        auto bounds = [](QQuickItem *control) {
            return QRectF(control->mapToScene(QPointF()), control->size());
        };
        auto *panel = item("playlistPanel");
        auto *footer = item("playlistFooter");
        auto *search = item("playlistSearch");
        auto *list = item("trackList");
        auto *bar = item("playbackBar");
        auto *timestamps = item("playbackTimestamps");
        auto *transport = item("transportButtons");
        auto *tools = item("playbackTools");
        QVERIFY(panel && footer && search && list && bar && timestamps && transport && tools);
        for (const QString &locale : {QStringLiteral("en"), QStringLiteral("zh_TW")}) {
            language->setLanguage(locale);
            for (const QSize size : {QSize(1225, 824), QSize(1100, 500), QSize(1600, 900)}) {
                window->resize(size);
                QTRY_COMPARE(window->size(), size);
                for (const int width : {360, 449, 450, 472, 520, 1000}) {
                    preferences->setProperty("playlistWidth", width);
                    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 5000);
                    const QString context = QStringLiteral("%1 %2x%3 playlist %4")
                            .arg(locale).arg(size.width()).arg(size.height()).arg(panel->width());
                    QVERIFY2(search->width() >= 140, qPrintable(context));
                    QVERIFY2(bounds(footer).contains(bounds(search)), qPrintable(context));
                    QVERIFY2(!bounds(list).intersects(bounds(footer)), qPrintable(context));
                    QList<QRectF> buttons;
                    for (const char *name : {"moveTopButton", "moveUpButton", "moveDownButton",
                                             "moveBottomButton", "addPlaylistButton",
                                             "removePlaylistButton", "sortPlaylistButton"}) {
                        auto *button = item(name);
                        QVERIFY(button);
                        const QRectF rect = bounds(button);
                        QVERIFY2(bounds(footer).contains(rect), qPrintable(context));
                        QVERIFY2(!rect.intersects(bounds(search)), qPrintable(context));
                        for (const QRectF &other : buttons)
                            QVERIFY2(!rect.intersects(other), qPrintable(context));
                        buttons.append(rect);
                        if (auto *content = button->property("contentItem").value<QQuickItem *>())
                            QVERIFY2(content->width() >= content->implicitWidth(), qPrintable(context));
                    }
                    QVERIFY2(bounds(bar).contains(bounds(timestamps)), qPrintable(context));
                    QVERIFY2(bounds(bar).contains(bounds(transport)), qPrintable(context));
                    QVERIFY2(bounds(bar).contains(bounds(tools)), qPrintable(context));
                    QVERIFY2(!bounds(timestamps).intersects(bounds(transport)), qPrintable(context));
                    QVERIFY2(!bounds(timestamps).intersects(bounds(tools)), qPrintable(context));
                    QVERIFY2(!bounds(transport).intersects(bounds(tools)), qPrintable(context));
                    const QString capturePath = qEnvironmentVariable("KPLAYER_TEST_CAPTURE");
                    if (!capturePath.isEmpty() && size == QSize(1225, 824)
                            && (width == 360 || width == 520 || width == 1000))
                        QVERIFY(window->grabWindow().save(QStringLiteral("%1.%2.%3.png")
                                .arg(capturePath, locale).arg(width)));
                }
            }
            window->setProperty("playlistVisible", false);
            QTRY_VERIFY(!panel->isVisible());
            QTRY_VERIFY(!bar->property("compact").toBool());
            window->setProperty("playlistVisible", true);
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
    }

    void controlPanelControlsPlayback()
    {
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("panel.y4m"));
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &errors) { warnings.append(errors); });
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = window->findChild<QQuickWindow *>("controlPanelWindow");
        auto *advanced = window->findChild<QObject *>("advancedSettingsDialog");
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(panel && advanced && video);
        auto item = [panel](const char *name) { return findItem(panel->contentItem(), name); };
        QVERIFY(!panel->isVisible());
        click(window, findItem(window->contentItem(), "settingsButton"));
        QVERIFY(exposeWindow(panel));
        QVERIFY(!advanced->property("visible").toBool());
        QCOMPARE(panel->title(), QStringLiteral("控制面板"));
        auto *language = engine.singletonInstance<AppLanguage *>("KPlayer", "AppLanguage");
        language->setLanguage("en");
        QTRY_COMPARE(panel->title(), QStringLiteral("Control Panel"));

        // Real pointer input changes only the floating panel's native opacity.
        auto *opacity = item("controlPanelOpacitySlider");
        QVERIFY(opacity);
        QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
                         opacity->mapToScene({opacity->width() / 2, opacity->height() / 2}).toPoint());
        QTRY_VERIFY(panel->opacity() > 0.25 && panel->opacity() < 0.9);
        QCOMPARE(window->opacity(), 1.0);
        panel->setProperty("panelOpacity", 1.0);
        click(panel, item("controlPanelPinButton"));
        QVERIFY(panel->flags().testFlag(Qt::WindowStaysOnTopHint));
        click(panel, item("controlPanelPinButton"));
        QVERIFY(!panel->flags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(exposeWindow(panel));

        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        QTRY_VERIFY(!video->playing());
        click(panel, item("panelEqualizerCheck"));
        QVERIFY(video->equalizerEnabled());
        auto *band = item("panelEqualizerBand0");
        QTRY_VERIFY(band->isEnabled());
        band->forceActiveFocus();
        QTRY_VERIFY(band->hasActiveFocus());
        QTest::keyClick(panel, Qt::Key_Up);
        QTRY_COMPARE(video->equalizerBands().first().toDouble(), 0.5);
        click(panel, item("panelEqualizerCheck"));
        QVERIFY(!video->equalizerEnabled());

        click(panel, item("controlPanelTab1"));
        auto *brightness = item("panelbrightnessSlider");
        brightness->forceActiveFocus();
        QTest::keyClick(panel, Qt::Key_Left);
        QTRY_COMPARE(video->brightness(), -1);
        video->setSoften(true);
        video->setSharpen(true);
        video->setDeblock(true);
        QSignalSpy captured(video, &MpvVideo::frameCaptured);
        const QUrl capture = QUrl::fromLocalFile(directory.filePath("capture.png"));
        video->captureFrame(capture);
        QTRY_COMPARE(captured.size(), 1);
        QVERIFY(!QImage(capture.toLocalFile()).isNull());
        video->setSoften(false);
        video->setSharpen(false);
        video->setDeblock(false);

        click(panel, item("controlPanelTab2"));
        click(panel, item("panelSubtitleSlower"));
        QCOMPARE(video->subtitleDelay(), 0.1);
        click(panel, item("panelSubtitleFaster"));
        QCOMPARE(video->subtitleDelay(), 0.0);
        video->setSubtitleFontSize(24);
        QTRY_COMPARE(item("panelSubtitleSize")->property("value").toInt(), 24);

        click(panel, item("controlPanelTab3"));
        click(panel, item("panelSpeedFaster"));
        QTRY_COMPARE(video->speed(), 1.1);
        click(panel, item("panelSpeedDefault"));
        QTRY_COMPARE(video->speed(), 1.0);
        video->seek(2);
        QTRY_VERIFY(video->position() >= 2 && video->position() < 3);
        click(panel, item("panelLoopA"));
        QCOMPARE(video->loopA(), video->position());
        video->seek(5);
        QTRY_VERIFY(video->position() >= 5 && video->position() < 6);
        click(panel, item("panelLoopB"));
        QCOMPARE(video->loopB(), video->position());
        video->play();
        video->seek(4);
        QTRY_VERIFY_WITH_TIMEOUT(video->position() < 3, 5000);
        video->togglePause();
        click(panel, item("panelLoopUndo"));
        QCOMPARE(video->loopA(), -1.0);
        QCOMPARE(video->loopB(), -1.0);

        // Optional artifacts let visual review use the actual rendered QML.
        const QString screenshots = qEnvironmentVariable("KPLAYER_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            for (int tab = 0; tab < 4; ++tab) {
                click(panel, item(qPrintable(QString("controlPanelTab%1").arg(tab))));
                QVERIFY(panel->grabWindow().save(QDir(screenshots).filePath(QString("panel-%1.png").arg(tab))));
            }
        }
        click(panel, item("controlPanelCloseButton"));
        QTRY_VERIFY(!panel->isVisible());
        // Exercise the equalizer on a loaded audio stream, not only video-only media.
        video->setSource(makeAudio(directory.filePath("equalizer.wav")));
        video->setEqualizerEnabled(true);
        video->setPreamp(3);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->playing() && video->position() > 0.1);
        QCOMPARE(video->errorString(), QString{});
        video->setEqualizerEnabled(false);
        video->stop();
        QVERIFY(exposeWindow(window));
        QTest::keyClick(window, Qt::Key_F5);
        QTRY_VERIFY(advanced->property("opened").toBool());
        QVERIFY(!panel->isVisible());
        QCOMPARE(video->errorString(), QString{});
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
        language->setLanguage("zh_TW");
    }

    void interfaceLanguageSwitchesAndPersists()
    {
        QSettings().clear();
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
            QVERIFY(dialog);
            auto hasDialogText = [dialog](const QString &text) {
                for (auto *child : dialog->findChildren<QObject *>()) {
                    if (child->property("text").toString() == text)
                        return true;
                }
                return false;
            };
            QCOMPARE(window->property("interfaceLanguage").toString(), QStringLiteral("zh_TW"));
            QCOMPARE(dialog->property("pageTitles").value<QJSValue>().toVariant().toStringList(),
                     QStringList({"一般", "播放", "字幕"}));
            QCOMPARE(MpvErrors::description(MPV_ERROR_LOADING_FAILED), QStringLiteral("無法載入媒體"));
            QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
            QTRY_VERIFY(dialog->property("opened").toBool());
            QVERIFY(hasDialogText(QStringLiteral("儲存")));
            QVERIFY(hasDialogText(QStringLiteral("取消")));
            auto *choice = findItem(window->contentItem(), "interfaceLanguageChoice");
            QVERIFY(choice);
            choice->setProperty("currentIndex", 1);
            QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
            QCOMPARE(window->property("interfaceLanguage").toString(), QStringLiteral("zh_TW"));
            QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
            QTRY_VERIFY(dialog->property("opened").toBool());
            QCOMPARE(choice->property("currentIndex").toInt(), 0);
            choice->setProperty("currentIndex", 1);
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
            QTRY_COMPARE(window->property("interfaceLanguage").toString(), QStringLiteral("en"));
            QCOMPARE(dialog->property("title").toString(), QStringLiteral("Advanced settings"));
            QCOMPARE(dialog->property("pageTitles").value<QJSValue>().toVariant().toStringList(),
                     QStringList({"General", "Playback", "Subtitles"}));
            QCOMPARE(MpvErrors::description(MPV_ERROR_LOADING_FAILED), QStringLiteral("Could not load media"));
            QCOMPARE(MpvErrors::description(-999), QStringLiteral("Unknown player error (-999)"));
            QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
            QTRY_VERIFY(dialog->property("opened").toBool());
            QVERIFY(hasDialogText(QStringLiteral("Save")));
            QVERIFY(hasDialogText(QStringLiteral("Cancel")));
            QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
            QVERIFY(!findItem(window->contentItem(), "addMediaTab"));
            QCOMPARE(window->findChild<QObject *>("playlistFolderDialog")->property("title").toString(),
                     QStringLiteral("Add folder as playlist"));
            QCOMPARE(findItem(window->contentItem(), "playlistTab0")->property("label").toString(), QStringLiteral("Default"));
            QCOMPARE(QSettings().value("interface/language").toString(), QStringLiteral("en"));
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QCOMPARE(window->property("interfaceLanguage").toString(), QStringLiteral("en"));
            auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
            QCOMPARE(dialog->property("title").toString(), QStringLiteral("Advanced settings"));
            auto *language = engine.singletonInstance<AppLanguage *>("KPlayer", "AppLanguage");
            QVERIFY(language);
            language->setLanguage("zh_TW");
            QCOMPARE(dialog->property("title").toString(), QStringLiteral("進階設定"));
            QCOMPARE(findItem(window->contentItem(), "playButton")->property("label").toString(), QStringLiteral("播放"));
        }
        QSettings().clear();
    }

    void unusedWindowAreasAllowDragging()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        QSignalSpy moves(window, SIGNAL(windowMoveRequested()));
        const auto drag = [&](QQuickItem *item, QPointF local) {
            const QPoint start = item->mapToScene(local).toPoint();
            const QPoint end = start + QPoint(QGuiApplication::styleHints()->startDragDistance() + 12, 0);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, end, 30);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        };
        for (const QString &name : {QStringLiteral("titleBar"), QStringLiteral("playbackBar"),
                                    QStringLiteral("trackList")}) {
            auto *item = findItem(window->contentItem(), name);
            QVERIFY(item);
            const int before = moves.count();
            drag(item, {item->width() / 2, item->height() - 20});
            QCOMPARE(moves.count(), before + 1);
        }
        const int before = moves.count();
        for (const QString &name : {QStringLiteral("playButton"), QStringLiteral("volumeSlider"),
                                    QStringLiteral("playlistSearch"), QStringLiteral("playlistTab0")}) {
            auto *item = findItem(window->contentItem(), name);
            QVERIFY(item);
            drag(item, {item->width() / 2, item->height() / 2});
            QCOMPARE(moves.count(), before);
        }
        window->showFullScreen();
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QVERIFY(!findItem(window->contentItem(), "windowDragArea")->property("enabled").toBool());
    }

    void favoritesPersistAndKeepUnavailableFiles()
    {
        QSettings().remove("favorites");
        QTemporaryDir directory;
        const QUrl source = makeAudio(directory.filePath(QString::fromUtf8("我的最愛.wav")));
        const QUrl remote("https://example.com/video%20one.mp4");
        {
            Favorites favorites;
            QVERIFY(favorites.entries().isEmpty());
            QVERIFY(!favorites.toggle(QUrl("https:/missing-host.mp4")));
            QVERIFY(!favorites.toggle(QUrl::fromLocalFile(directory.path())));
            QVERIFY(favorites.toggle(source));
            QVERIFY(favorites.contains(source));
            QCOMPARE(favorites.entries().first().toMap().value("title").toString(),
                     QString::fromUtf8("我的最愛.wav"));
            QVERIFY(favorites.toggle(remote));
        }
        QVERIFY(QFile::remove(source.toLocalFile()));
        {
            Favorites favorites;
            QCOMPARE(favorites.entries().size(), 2);
            QVERIFY(favorites.contains(source));
            QVERIFY(favorites.toggle(source));
            QVERIFY(!favorites.contains(source));
            QVERIFY(favorites.contains(QUrl("https://example.com/folder/../video%20one.mp4")));
            favorites.clear();
        }
        QVERIFY(Favorites().entries().isEmpty());
    }

    void videoMenusApplyAdjustmentsAndFavorites()
    {
        QSettings().remove("favorites");
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("adjustments.y4m"), true);
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *favorites = window->findChild<Favorites *>();
        auto *menu = window->findChild<QObject *>("appMenu");
        auto *adjustments = window->findChild<QObject *>("videoAdjustmentsDialog");
        auto *aspectMenu = window->findChild<QObject *>("aspectRatioMenu");
        auto *favoritesMenu = window->findChild<QObject *>("favoritesMenu");
        QVERIFY(video && favorites && menu && adjustments && aspectMenu && favoritesMenu);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        QTRY_VERIFY(!video->playing());
        QTRY_VERIFY(previewBrightness(window, video) > 160);

        QVERIFY(QMetaObject::invokeMethod(menuItemAt(aspectMenu, 1), "triggered"));
        QTRY_COMPARE(video->aspectRatio(), QString("4:3"));
        video->setAspectRatio("invalid");
        QCOMPARE(video->aspectRatio(), QString("4:3"));
        QVERIFY(QMetaObject::invokeMethod(menuItemAt(aspectMenu, 0), "triggered"));
        QCOMPARE(video->aspectRatio(), QString("no"));
        QVERIFY(QMetaObject::invokeMethod(menu, "videoAdjustmentsRequested"));
        QTRY_VERIFY(adjustments->property("opened").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        auto *brightness = findItem(window->contentItem(), "brightnessSlider");
        QVERIFY(brightness);
        brightness->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(video->brightness(), -1);
        video->setBrightness(-200);
        QCOMPARE(video->brightness(), -100);
        video->setContrast(200);
        QCOMPARE(video->contrast(), 100);
        video->setSaturation(-200);
        QCOMPARE(video->saturation(), -100);
        click(window, findItem(window->contentItem(), "resetVideoAdjustmentsButton"));
        QCOMPARE(video->brightness(), 0);
        QCOMPARE(video->contrast(), 0);
        QCOMPARE(video->saturation(), 0);
        QVERIFY(QMetaObject::invokeMethod(adjustments, "close"));
        video->setBrightness(-100);
        QTRY_VERIFY(previewBrightness(window, video) < 10);
        video->resetVideoAdjustments();
        QTRY_VERIFY(previewBrightness(window, video) > 160);
        QCOMPARE(video->errorString(), QString{});

        QVERIFY(QMetaObject::invokeMethod(menuItemAt(favoritesMenu, 0), "triggered"));
        QTRY_VERIFY(favorites->contains(source));
        QTRY_COMPARE(favoritesMenu->property("count").toInt(), 4);
        video->stop();
        QCOMPARE(video->position(), 0.0);
        QCOMPARE(video->duration(), 0.0);
        QVERIFY(QMetaObject::invokeMethod(menuItemAt(favoritesMenu, 3), "triggered"));
        QTRY_VERIFY(video->playing() && video->seekable());
        QCOMPARE(video->source(), source);
        video->seek(29.5);
        QTRY_VERIFY_WITH_TIMEOUT(video->stopped(), 5000);
        video->stop();
        QCOMPARE(video->position(), 0.0);
        QCOMPARE(video->duration(), 0.0);
        favorites->clear();
    }

    void advancedSettingsCanBeDragged()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *handle = findItem(window->contentItem(), "settingsDragHandle");
        QVERIFY(handle);
        QSignalSpy moves(window, SIGNAL(windowMoveRequested()));
        const QPointF centered(dialog->property("x").toReal(), dialog->property("y").toReal());
        const auto drag = [&](QPoint delta) {
            const QPoint start = handle->mapToScene({handle->width() / 2, handle->height() / 2}).toPoint();
            const QPoint end(qBound(1, start.x() + delta.x(), window->width() - 1),
                             qBound(1, start.y() + delta.y(), window->height() - 1));
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, end, 30);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        };
        drag({60, 40});
        QTRY_COMPARE(dialog->property("x").toReal(), centered.x() + 60);
        QTRY_COMPARE(dialog->property("y").toReal(), centered.y() + 40);
        QCOMPARE(moves.count(), 0);
        click(window, findItem(window->contentItem(), "settingsPage2"));
        QCOMPARE(dialog->property("currentPage").toInt(), 2);
        drag({-1000, -1000});
        QTRY_COMPARE(dialog->property("x").toReal(), 0.0);
        QTRY_COMPARE(dialog->property("y").toReal(), 0.0);
        drag({2000, 2000});
        QTRY_COMPARE(dialog->property("x").toReal(), window->width() - dialog->property("width").toReal());
        QTRY_COMPARE(dialog->property("y").toReal(), window->height() - dialog->property("height").toReal());
        window->resize(1100, 500);
        QTRY_VERIFY(dialog->property("x").toReal() + dialog->property("width").toReal() <= window->width());
        QTRY_VERIFY(dialog->property("y").toReal() + dialog->property("height").toReal() <= window->height());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->property("x").toReal(), (window->width() - dialog->property("width").toReal()) / 2);
        QCOMPARE(dialog->property("y").toReal(), (window->height() - dialog->property("height").toReal()) / 2);
    }

    void advancedSettingsDropdownHoverIsReadable()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        for (const char *name : {"interfaceLanguageChoice"}) {
            auto *combo = findItem(window->contentItem(), QString::fromLatin1(name));
            QVERIFY(combo);
            click(window, combo);
            auto *option = findItem(window->contentItem(), QString::fromLatin1(name) + "Option1");
            QTRY_VERIFY(option && option->isVisible());
            QTest::mouseMove(window, option->mapToScene(
                    {option->width() / 2, option->height() / 2}).toPoint());
            QTRY_VERIFY(option->property("hovered").toBool());
            QTRY_VERIFY(option->property("highlighted").toBool());
            auto *text = option->property("contentItem").value<QQuickItem *>();
            auto *background = option->property("background").value<QQuickItem *>();
            QVERIFY(text && background);
            const QColor foregroundColor = text->property("color").value<QColor>();
            const QColor backgroundColor = background->property("color").value<QColor>();
            QVERIFY(qGray(foregroundColor.rgb()) - qGray(backgroundColor.rgb()) > 100);
            QVERIFY(!option->property("text").toString().isEmpty());
            const QString captureDirectory = qEnvironmentVariable("KPLAYER_SETTINGS_CAPTURE");
            if (!captureDirectory.isEmpty()) {
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                QVERIFY(window->grabWindow().save(QDir(captureDirectory).filePath(
                        QString::fromLatin1(name) + ".png")));
            }
            QTest::keyClick(window, Qt::Key_Escape);
        }
    }

    void subtitleRuleRowsReorderAndSave()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(video && dialog);
        video->setDefaultSubtitleRules("subtrackname:English\ntracknum:2\nsubtrackname:Japanese");
        dialog->setProperty("currentPage", 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!findItem(window->contentItem(), "subtitleLanguageChoice"));
        auto *first = findItem(window->contentItem(), "subtitleRuleDragHandle0");
        auto *last = findItem(window->contentItem(), "subtitleRuleDragHandle2");
        QVERIFY(first && last);
        QTest::qWait(250);
        const QPoint start = first->mapToScene({first->width() / 2, first->height() / 2}).toPoint();
        const QPoint end = last->mapToScene({last->width() / 2, last->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTRY_VERIFY(first->property("pressed").toBool());
        QTest::mouseMove(window, end, 100);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        auto *value = findItem(window->contentItem(), "subtitleRuleValue0");
        QVERIFY(value);
        QTRY_COMPARE(value->property("text").toString(), QStringLiteral("2"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QCOMPARE(video->defaultSubtitleRules(), QStringLiteral("tracknum:2\nsubtrackname:Japanese\nsubtrackname:English"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        value = findItem(window->contentItem(), "subtitleRuleValue0");
        QVERIFY(value);
        value->setProperty("text", "3");
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QCOMPARE(video->defaultSubtitleRules(), QStringLiteral("tracknum:2\nsubtrackname:Japanese\nsubtrackname:English"));
        video->setDefaultSubtitleRules("");
    }

    void subtitleRuleEditorValidatesAndCancelsDrafts()
    {
        QList<QQmlError> warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlEngine::warnings, this,
                [&](const QList<QQmlError> &items) { warnings.append(items); });
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(video && dialog);
        video->setDefaultSubtitleRules("tracknum:2");
        dialog->setProperty("currentPage", 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *settings = findItem(window->contentItem(), "subtitleSettings");
        auto *save = findItem(window->contentItem(), "advancedSettingsSaveButton");
        auto *value = findItem(window->contentItem(), "subtitleRuleValue0");
        QVERIFY(settings && save && value);
        QVERIFY(!settings->property("fallbackExpanded").toBool());
        value->setProperty("text", "0");
        QTRY_VERIFY(!save->isEnabled());
        value->setProperty("text", "1.5");
        QVERIFY(!settings->property("rulesValid").toBool());
        value->setProperty("text", "2");
        QTRY_VERIFY(save->isEnabled());

        auto *add = findItem(window->contentItem(), "addSubtitleRuleButton");
        QVERIFY(add);
        QTest::qWait(250);
        click(window, add);
        auto *newValue = findItem(window->contentItem(), "subtitleRuleValue1");
        QTRY_VERIFY(newValue && newValue->hasActiveFocus());
        QVERIFY(save->isEnabled()); // Empty draft rows are ignored.
        for (char letter : QByteArray("English"))
            QTest::keyClick(window, letter);
        auto *grip = findItem(window->contentItem(), "subtitleRuleGrip1");
        QVERIFY(grip);
        grip->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);
        QTRY_COMPARE(findItem(window->contentItem(), "subtitleRuleValue0")->property("text").toString(),
                     QStringLiteral("English"));
        QCOMPARE(video->defaultSubtitleRules(), QStringLiteral("tracknum:2"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!findItem(window->contentItem(), "subtitleRuleValue1"));
        auto *remove = findItem(window->contentItem(), "removeSubtitleRuleButton0");
        QVERIFY(remove);
        QTest::qWait(250);
        click(window, remove);
        auto *empty = findItem(window->contentItem(), "subtitleRulesEmptyState");
        QVERIFY(empty);
        QTRY_VERIFY(empty->isVisible());
        click(window, add);
        value = findItem(window->contentItem(), "subtitleRuleValue0");
        QTRY_VERIFY(value && value->hasActiveFocus());
        auto *type = findItem(window->contentItem(), "subtitleRuleType0");
        QVERIFY(type);
        click(window, type);
        auto *numberOption = findItem(window->contentItem(), "subtitleRuleType0Option1");
        QTRY_VERIFY(numberOption && numberOption->isVisible());
        click(window, numberOption);
        value->setProperty("text", "3");
        QTRY_VERIFY(save->isEnabled());
        click(window, save);
        QTRY_COMPARE(video->defaultSubtitleRules(), QStringLiteral("tracknum:3"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        type = findItem(window->contentItem(), "subtitleRuleType0");
        value = findItem(window->contentItem(), "subtitleRuleValue0");
        QVERIFY(type && value);
        QTest::qWait(250);
        click(window, type);
        auto *languageOption = findItem(window->contentItem(), "subtitleRuleType0Option2");
        QTRY_VERIFY(languageOption && languageOption->isVisible());
        click(window, languageOption);
        value->setProperty("text", "en,ja");
        QTRY_VERIFY(!save->isEnabled());
        value->setProperty("text", "und");
        QVERIFY(!save->isEnabled());
        value->setProperty("text", "EN");
        QTRY_VERIFY(save->isEnabled());
        click(window, save);
        QTRY_COMPARE(video->defaultSubtitleRules(), QStringLiteral("language:en"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString{} : warnings.first().toString()));
    }

    void subtitleStyleOverrideDefaultsOffAndPersists()
    {
        {
            MpvVideo video;
            QVERIFY(!video.subtitleStyleOverride());
            video.setPersistent(true);
            QVERIFY(!video.subtitleStyleOverride());
            video.setSubtitleStyleOverride(true);
            QVERIFY(video.subtitleStyleOverride());
            QCOMPARE(QSettings().value("subtitles/styleOverride").toBool(), true);
        }
        {
            MpvVideo video;
            video.setPersistent(true);
            QVERIFY(video.subtitleStyleOverride());
            video.setSubtitleStyleOverride(false);
        }
        MpvVideo video;
        video.setPersistent(true);
        QVERIFY(!video.subtitleStyleOverride());
    }

    void currentSubtitleCanBeAddedToPriorityRules()
    {
        QTemporaryDir directory;
        const QUrl media = makeVideo(directory.filePath("episode.y4m"));
        QFile subtitle(directory.filePath("Signs & Songs.srt"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly));
        subtitle.write("1\n00:00:00,000 --> 00:00:29,000\nSubtitle\n");
        subtitle.close();
        QList<QQmlError> warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlEngine::warnings, this,
                [&](const QList<QQmlError> &items) { warnings.append(items); });
        engine.setInitialProperties({{"mediaSource", media}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        auto *menu = window->findChild<QObject *>("subtitleSelectionMenu");
        QVERIFY(video && dialog && menu);
        auto *quickAdd = menu->findChild<QObject *>("addCurrentSubtitleRuleMenuItem");
        auto *useCurrent = qobject_cast<QQuickItem *>(dialog->findChild<QObject *>("useCurrentSubtitleButton"));
        QVERIFY(quickAdd);
        QVERIFY(useCurrent);
        QVERIFY(!quickAdd->property("enabled").toBool());
        QVERIFY(!useCurrent->isEnabled());
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        video->togglePause();
        QVERIFY(video->addSubtitle(QUrl::fromLocalFile(subtitle.fileName())));
        QTRY_VERIFY(video->subtitleTrack() > 0);
        const int track = video->subtitleTrack();
        video->setDefaultSubtitleRules("subtrackname:Existing");
        QTRY_VERIFY(quickAdd->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(quickAdd, "triggered"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->property("currentPage").toInt(), 2);
        auto *value = findItem(window->contentItem(), "subtitleRuleValue1");
        QTRY_VERIFY(value && value->hasActiveFocus());
        QCOMPARE(value->property("text").toString(), QStringLiteral("Signs & Songs.srt"));
        QCOMPARE(video->defaultSubtitleRules(), QStringLiteral("subtrackname:Existing"));
        auto *style = findItem(window->contentItem(), "subtitleStyleOverrideInput");
        QVERIFY(style && !style->property("checked").toBool());
        auto *languageChoice = dialog->findChild<QObject *>("useCurrentSubtitleLanguageMenuItem");
        QVERIFY(languageChoice && !languageChoice->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(quickAdd, "triggered"));
        QVERIFY(!findItem(window->contentItem(), "subtitleRuleValue2"));
        auto *settings = findItem(window->contentItem(), "subtitleSettings");
        QVERIFY(settings);
        QCOMPARE(settings->property("ruleActionResult").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(video->defaultSubtitleRules(), QStringLiteral("subtrackname:Existing"));

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(useCurrent->isEnabled());
        auto *scroll = findItem(window->contentItem(), "advancedSettingsScroll");
        QVERIFY(scroll);
        auto *view = scroll->property("contentItem").value<QQuickItem *>();
        QVERIFY(view);
        auto *content = view->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        const auto captureCurrent = [&](const char *objectName) {
            const qreal y = useCurrent->mapToItem(content, QPointF{}).y();
            view->setProperty("contentY", std::max(0.0, y - view->height() / 2));
            QTest::qWait(250);
            click(window, useCurrent);
            auto *item = qobject_cast<QQuickItem *>(settings->findChild<QObject *>(objectName));
            QTest::qWait(100);
            if (!item || !item->isVisible())
                return false;
            click(window, item);
            return true;
        };
        QVERIFY(captureCurrent("useCurrentSubtitleNameMenuItem"));
        value = findItem(window->contentItem(), "subtitleRuleValue1");
        QTRY_VERIFY(value && value->hasActiveFocus());
        QCOMPARE(value->property("text").toString(), QStringLiteral("Signs & Songs.srt"));
        QVERIFY(captureCurrent("useCurrentSubtitleNumberMenuItem"));
        value = findItem(window->contentItem(), "subtitleRuleValue2");
        QTRY_VERIFY(value && value->hasActiveFocus());
        QCOMPARE(value->property("text").toString(), QString::number(track));
        QVERIFY(captureCurrent("useCurrentSubtitleNumberMenuItem"));
        QVERIFY(!findItem(window->contentItem(), "subtitleRuleValue3"));
        QCOMPARE(settings->property("ruleActionResult").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        const QString saved = QStringLiteral("subtrackname:Existing\nsubtrackname:Signs & Songs.srt\ntracknum:%1").arg(track);
        QTRY_COMPARE(video->defaultSubtitleRules(), saved);
        QCOMPARE(QSettings().value("subtitles/defaultRules").toString(), saved);

        // Untitled embedded subtitles can still be captured by their number.
        QTRY_VERIFY(!dialog->property("visible").toBool());
        dialog->setProperty("currentSubtitleTrack", QVariantMap{{"id", 7}});
        QVERIFY(QMetaObject::invokeMethod(dialog, "openForCurrentSubtitleRule"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        value = findItem(window->contentItem(), "subtitleRuleValue3");
        QTRY_VERIFY(value && value->hasActiveFocus());
        QCOMPARE(value->property("text").toString(), QStringLiteral("7"));
        auto *nameChoice = settings->findChild<QObject *>("useCurrentSubtitleNameMenuItem");
        QVERIFY(nameChoice && !nameChoice->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QCOMPARE(video->defaultSubtitleRules(), saved);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString{} : warnings.first().toString()));
    }

    void subtitleSettingsFitInBothLanguages()
    {
        QList<QQmlError> warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlEngine::warnings, this,
                [&](const QList<QQmlError> &items) { warnings.append(items); });
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        auto *language = engine.singletonInstance<AppLanguage *>("KPlayer", "AppLanguage");
        QVERIFY(video && dialog && language);
        video->setDefaultSubtitleRules("subtrackname:Signs & Songs\ntracknum:2\nlanguage:ja");
        video->setDefaultSubtitleLanguage("zh,en");
        dialog->setProperty("currentPage", 2);
        const QString captureDirectory = qEnvironmentVariable("KPLAYER_SUBTITLE_CAPTURE");
        for (const QString &code : {QStringLiteral("en"), QStringLiteral("zh_TW")}) {
            language->setLanguage(code);
            QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
            QTRY_VERIFY(dialog->property("opened").toBool());
            for (int width : {1000, 640}) {
                window->resize(width, 740);
                QTRY_COMPARE(dialog->property("width").toInt(), std::min(900, width - 32));
                QTest::qWait(250);
                auto *type = findItem(window->contentItem(), "subtitleRuleType0");
                auto *value = findItem(window->contentItem(), "subtitleRuleValue0");
                QVERIFY(type && value);
                QTRY_COMPARE(type->property("displayText").toString(), code == "en"
                        ? QStringLiteral("Track name") : QStringLiteral("字幕軌名稱"));
                auto *languageType = findItem(window->contentItem(), "subtitleRuleType2");
                QVERIFY(languageType);
                QTRY_COMPARE(languageType->property("displayText").toString(), code == "en"
                        ? QStringLiteral("Language") : QStringLiteral("語言"));
                auto *save = findItem(window->contentItem(), "advancedSettingsSaveButton");
                QVERIFY(save);
                QTRY_COMPARE(save->property("text").toString(), code == "en"
                        ? QStringLiteral("Save") : QStringLiteral("儲存"));
                QVERIFY(value->width() >= 60);
                const QRectF valueRect(value->mapToScene({0, 0}), value->size());
                QVERIFY(valueRect.left() >= 0 && valueRect.right() <= window->width());
                QVERIFY(valueRect.top() >= 0 && valueRect.bottom() <= window->height());
                if (!captureDirectory.isEmpty()) {
                    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(!frames.isEmpty());
                    QVERIFY(window->grabWindow().save(QDir(captureDirectory).filePath(
                            QStringLiteral("subtitles-%1-%2.png").arg(code).arg(width))));
                }
            }
            QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString{} : warnings.first().toString()));
    }

    void advancedSettingsPersistAndCancel()
    {
        QSettings().clear();
        QList<QQmlError> warnings;
        {
            QQmlApplicationEngine engine;
            connect(&engine, &QQmlEngine::warnings, this,
                    [&](const QList<QQmlError> &items) { warnings.append(items); });
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
            QVERIFY(video && dialog);
            click(window, findItem(window->contentItem(), "videoStage"));
            QTest::keyClick(window, Qt::Key_F5);
            QTRY_VERIFY(dialog->property("opened").toBool());
            QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
            click(window, findItem(window->contentItem(), "settingsPage2"));
            auto *codes = findItem(window->contentItem(), "subtitleLanguageCodes");
            QVERIFY(codes);
            // The language field is below the fold on the subtitle settings page.
            auto *scroll = findItem(window->contentItem(), "advancedSettingsScroll");
            auto *flickable = scroll->property("contentItem").value<QQuickItem *>();
            QVERIFY(flickable);
            QTRY_VERIFY(flickable->property("contentHeight").toReal() > flickable->height());
            flickable->setProperty("contentY", std::max(0.0,
                flickable->property("contentHeight").toReal() - flickable->height()));
            click(window, codes);
            QTRY_VERIFY(codes->hasActiveFocus());
            QTest::keyClick(window, Qt::Key_X);
            QTest::keyClick(window, Qt::Key_C);
            QTest::keyClick(window, Qt::Key_Z);
            QCOMPARE(codes->property("text").toString(), QStringLiteral("xcz"));
            QCOMPARE(video->speed(), 1.0);
            auto *maximum = findItem(window->contentItem(), "maximumSpeedInput");
            auto *speed = findItem(window->contentItem(), "customSpeedInput");
            QVERIFY(maximum && speed);
            auto *previewToggle = findItem(window->contentItem(), "seekPreviewInput");
            auto *seek = findItem(window->contentItem(), "positionSlider");
            QVERIFY(previewToggle && seek);
            QVERIFY(previewToggle->property("checked").toBool());
            QVERIFY(seek->property("previewEnabled").toBool());
            previewToggle->setProperty("checked", false);
            QVERIFY(seek->property("previewEnabled").toBool());
            QCOMPARE(maximum->property("value").toInt(), 160);
            maximum->setProperty("value", 240);
            speed->setProperty("value", 125);
            codes->setProperty("text", " JA, JPN ");
            auto *track = findItem(window->contentItem(), "subtitleTrackNumberInput");
            auto *overrideStyle = findItem(window->contentItem(), "subtitleStyleOverrideInput");
            QVERIFY(track && overrideStyle);
            track->setProperty("value", 2);
            overrideStyle->setProperty("checked", false);
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
            QTRY_VERIFY(!seek->property("previewEnabled").toBool());
            QTRY_COMPARE(video->defaultSubtitleLanguage(), QStringLiteral("ja,jpn"));
            QCOMPARE(video->maximumSpeed(), 24.0);
            QTRY_COMPARE(video->speed(), 12.5);
            QCOMPARE(video->defaultSubtitleTrack(), 2);
            QVERIFY(!video->subtitleStyleOverride());
            QTest::keyClick(window, Qt::Key_F5);
            QTRY_VERIFY(dialog->property("opened").toBool());
            QCOMPARE(codes->property("text").toString(), QStringLiteral("ja,jpn"));
            QVERIFY(!previewToggle->property("checked").toBool());
            previewToggle->setProperty("checked", true);
            codes->setProperty("text", "en");
            track->setProperty("value", 3);
            overrideStyle->setProperty("checked", true);
            maximum->setProperty("value", 80);
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!dialog->property("visible").toBool());
            QVERIFY(!seek->property("previewEnabled").toBool());
            QCOMPARE(video->defaultSubtitleLanguage(), QStringLiteral("ja,jpn"));
            QCOMPARE(video->maximumSpeed(), 24.0);
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *video = engine.rootObjects().first()->findChild<MpvVideo *>();
            QVERIFY(video);
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            auto *seek = findItem(window->contentItem(), "positionSlider");
            QVERIFY(seek && !seek->property("previewEnabled").toBool());
            QCOMPARE(video->defaultSubtitleLanguage(), QStringLiteral("ja,jpn"));
            QCOMPARE(video->maximumSpeed(), 24.0);
            QCOMPARE(video->speed(), 1.0);
            video->toggleSpeed();
            QTRY_COMPARE(video->speed(), 12.5);
            QCOMPARE(video->defaultSubtitleTrack(), 2);
            QVERIFY(!video->subtitleStyleOverride());
        }
        QSettings().clear();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString{} : warnings.first().toString()));
    }

    void playerPreferencesSurviveRestart()
    {
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(video);
            video->setMaximumSpeed(32);
            video->setSpeed(24);
            video->setVolume(37);
            video->setMuted(true);
            video->setSubtitleFont("sans-serif");
            video->setSubtitleFontSize(32);
            video->setSubtitlePosition(80);
            video->setSubtitleAlignment(-1);
            video->setSubtitlesVisible(false);
            video->setSubtitleDelay(2);
            video->setBrightness(12);
            video->setContrast(-7);
            video->setAspectRatio("16:9");
            video->setEqualizerBands({1, 2, 3, 4, 5, 0, -1, -2, -3, -4});
            video->setPreamp(2);
            video->setEqualizerEnabled(true);
            video->setSharpen(true);
            QTRY_COMPARE(video->speed(), 24.0);
            QTRY_COMPARE(video->volume(), 37.0);
            QTRY_VERIFY(video->muted());
            window->setProperty("repeatMode", 2);
            window->setProperty("playlistVisible", false);
            window->resize(1150, 700);
            QTRY_COMPARE(window->size(), QSize(1150, 700));
            QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
            QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
            QTRY_COMPARE(window->size(), window->screen()->size());
            window->close();
        }
        // Old installations may still have an active-speed key. Ignore it on startup.
        QSettings().setValue("playback/speed", 24.0);
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(video);
            QTRY_COMPARE(video->speed(), 1.0);
            QCOMPARE(video->maximumSpeed(), 32.0);
            QTRY_COMPARE(video->volume(), 37.0);
            QTRY_VERIFY(video->muted());
            QCOMPARE(video->subtitleFont(), QStringLiteral("sans-serif"));
            QCOMPARE(video->subtitleFontSize(), 32);
            QCOMPARE(video->subtitlePosition(), 80);
            QCOMPARE(video->subtitleAlignment(), -1);
            QVERIFY(!video->subtitlesVisible());
            QCOMPARE(video->subtitleDelay(), 0.0);
            QCOMPARE(video->brightness(), 12);
            QCOMPARE(video->contrast(), -7);
            QCOMPARE(video->aspectRatio(), QStringLiteral("16:9"));
            QCOMPARE(video->equalizerBands(), QVariantList({1, 2, 3, 4, 5, 0, -1, -2, -3, -4}));
            QCOMPARE(video->preamp(), 2.0);
            QVERIFY(video->equalizerEnabled());
            QVERIFY(video->sharpen());
            QCOMPARE(window->property("repeatMode").toInt(), 2);
            QVERIFY(!window->property("playlistVisible").toBool());
            QVERIFY(window->property("controlsVisible").toBool());
            QCOMPARE(window->visibility(), QWindow::Windowed);
            QCOMPARE(window->size(), QSize(1150, 700));
            QTest::keyClick(window, Qt::Key_Z);
            QTRY_COMPARE(video->speed(), 24.0);
            QVERIFY(!QSettings().contains("playback/speed"));
            QTest::keyClick(window, Qt::Key_Z);
            QTRY_COMPARE(video->speed(), 1.0);
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(video);
            QCOMPARE(video->speed(), 1.0);
            video->toggleSpeed();
            QTRY_COMPARE(video->speed(), 24.0); // Z remembers the last non-normal speed too.
        }
    }

    void defaultSubtitleLanguageSelectsTrack()
    {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to create the multilingual Matroska fixture");
        QTemporaryDir directory;
        const auto rawVideo = makeVideo(directory.filePath("video.y4m"));
        QFile subtitles(directory.filePath("text.srt"));
        QVERIFY(subtitles.open(QIODevice::WriteOnly));
        subtitles.write("1\n00:00:00,000 --> 00:00:29,000\nSubtitle\n");
        subtitles.close();
        const QString media = directory.filePath("multilingual.mkv");
        QProcess encoder;
        encoder.start(ffmpeg, {"-v", "error", "-i", rawVideo.toLocalFile(),
            "-i", subtitles.fileName(), "-i", subtitles.fileName(),
            "-map", "0:v", "-map", "1:s", "-map", "2:s", "-c:v", "ffv1", "-c:s", "srt",
            "-metadata:s:s:0", "language=eng", "-metadata:s:s:1", "language=jpn",
            "-metadata:s:s:0", "title=English", "-metadata:s:s:1", "title=Japanese",
            "-disposition:s:0", "default", "-disposition:s:1", "0", media});
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", QUrl::fromLocalFile(media)}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(video);
        video->setDefaultSubtitleLanguage("ja,jpn");
        video->play();
        QTRY_COMPARE(video->subtitleTracks().size(), 2);
        const int english = video->subtitleTracks().first().toMap().value("id").toInt();
        const int japanese = video->subtitleTracks().last().toMap().value("id").toInt();
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->togglePause();

        // Capture language through both the settings chooser and subtitle menus.
        auto *settingsDialog = window->findChild<QObject *>("advancedSettingsDialog");
        auto *subtitleMenu = window->findChild<QObject *>("subtitleSelectionMenu");
        QVERIFY(settingsDialog && subtitleMenu);
        auto *quickLanguage = subtitleMenu->findChild<QObject *>("addCurrentSubtitleLanguageRuleMenuItem");
        QVERIFY(quickLanguage && quickLanguage->property("enabled").toBool());
        settingsDialog->setProperty("currentPage", 2);
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "open"));
        QTRY_VERIFY(settingsDialog->property("opened").toBool());
        auto *useCurrent = findItem(window->contentItem(), "useCurrentSubtitleButton");
        QVERIFY(useCurrent);
        QTest::qWait(250);
        click(window, useCurrent);
        auto *languageChoice = qobject_cast<QQuickItem *>(
            settingsDialog->findChild<QObject *>("useCurrentSubtitleLanguageMenuItem"));
        QVERIFY(languageChoice);
        QTRY_VERIFY(languageChoice->isVisible() && languageChoice->isEnabled());
        click(window, languageChoice);
        auto *ruleValue = findItem(window->contentItem(), "subtitleRuleValue0");
        QTRY_VERIFY(ruleValue && ruleValue->hasActiveFocus());
        QCOMPARE(ruleValue->property("text").toString(), QStringLiteral("jpn"));
        QCOMPARE(findItem(window->contentItem(), "subtitleRuleType0")->property("currentIndex").toInt(), 2);
        QCOMPARE(video->defaultSubtitleRules(), QString{}); // Capture is still a draft.
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "accept"));
        QTRY_COMPARE(video->defaultSubtitleRules(), QStringLiteral("language:jpn"));
        QTRY_VERIFY(!settingsDialog->property("visible").toBool());
        video->setDefaultSubtitleRules("language:ja");
        QVERIFY(QMetaObject::invokeMethod(quickLanguage, "triggered"));
        QTRY_VERIFY(settingsDialog->property("opened").toBool());
        QVERIFY(!findItem(window->contentItem(), "subtitleRuleValue1")); // ja and jpn are the same rule.
        QCOMPARE(findItem(window->contentItem(), "subtitleSettings")->property("ruleActionResult").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "reject"));
        QTRY_VERIFY(!settingsDialog->property("visible").toBool());
        auto *appMenu = window->findChild<QObject *>("appMenu");
        QVERIFY(appMenu);
        auto *appLanguage = appMenu->findChild<QObject *>("addCurrentSubtitleLanguageRuleMenuItem");
        QVERIFY(appLanguage && appLanguage->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(appLanguage, "triggered"));
        QTRY_VERIFY(settingsDialog->property("opened").toBool());
        QVERIFY(!findItem(window->contentItem(), "subtitleRuleValue1"));
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "reject"));
        QTRY_VERIFY(!settingsDialog->property("visible").toBool());

        // Language competes with names and numbers in the same ordered list.
        video->setDefaultSubtitleRules("");
        video->setDefaultSubtitleLanguage("ja_JP");
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleRules("language:en\nsubtrackname:Japanese");
        QTRY_COMPARE(video->subtitleTrack(), english);
        QCOMPARE(QSettings().value("subtitles/defaultRules").toString(), QStringLiteral("language:en\nsubtrackname:Japanese"));
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->setDefaultSubtitleRules("subtrackname:Japanese\nlanguage:eng");
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleRules(QStringLiteral("language:ENg\ntracknum:%1").arg(japanese));
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->setDefaultSubtitleRules(QStringLiteral("language:en_US\ntracknum:%1").arg(japanese));
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->setDefaultSubtitleRules(QStringLiteral("language:ja\ntracknum:%1").arg(english));
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleRules("language:xx\nlanguage:jpn\nsubtrackname:English");
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleRules(QStringLiteral("language:und\nlanguage:zxx\nlanguage:mul\ntracknum:%1").arg(english));
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->setDefaultSubtitleRules(QStringLiteral("subtrackname:missing\ntracknum:%1\nsubtrackname:Japanese").arg(english));
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->setDefaultSubtitleRules(QStringLiteral("subtrackname: japanese \ntracknum:%1").arg(english));
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleRules("unknown:English\ntracknum:invalid\nsubtrackname:missing");
        QTRY_COMPARE(video->subtitleTrack(), japanese); // Falls back to language.
        video->setDefaultSubtitleRules("");
        video->setSubtitleTrack(english);
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleLanguage("en,eng");
        QTRY_COMPARE(video->subtitleTrack(), english); // Applies without reopening.
        video->setDefaultSubtitleTrack(japanese);
        QTRY_COMPARE(video->subtitleTrack(), japanese); // Number takes priority over language.
        video->setDefaultSubtitleTrack(9999);
        QTRY_COMPARE(video->subtitleTrack(), english); // Missing number falls back to language.
        video->setDefaultSubtitleTrack(japanese);
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleTrack(0);
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->setDefaultSubtitleTrack(japanese);
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleLanguage("");
        video->setDefaultSubtitleTrack(9999);
        QTRY_COMPARE(video->subtitleTrack(), english); // No language: automatic default track.
        video->setDefaultSubtitleTrack(0);
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), english);
        QCOMPARE(video->errorString(), QString{});
    }

    void subtitleLanguageRulesRespectScriptVariants()
    {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to create the Chinese subtitle fixture");
        QTemporaryDir directory;
        const auto rawVideo = makeVideo(directory.filePath("video.y4m"));
        QFile subtitles(directory.filePath("text.srt"));
        QVERIFY(subtitles.open(QIODevice::WriteOnly));
        subtitles.write("1\n00:00:00,000 --> 00:00:29,000\nSubtitle\n");
        subtitles.close();
        const QString media = directory.filePath("chinese.mkv");
        QProcess encoder;
        encoder.start(ffmpeg, {"-v", "error", "-i", rawVideo.toLocalFile(),
            "-i", subtitles.fileName(), "-i", subtitles.fileName(),
            "-map", "0:v", "-map", "1:s", "-map", "2:s", "-map", "1:s", "-map", "2:s",
            "-c:v", "ffv1", "-c:s", "srt",
            "-metadata:s:s:0", "language=zh-Hans", "-metadata:s:s:0", "title=Simplified",
            "-metadata:s:s:1", "language=chi", "-metadata:s:s:1", "title=Unspecified Chinese",
            "-metadata:s:s:2", "language=zh-Hant", "-metadata:s:s:2", "title=Traditional",
            "-metadata:s:s:3", "language=zh-TW", "-metadata:s:s:3", "title=Taiwan",
            "-disposition:s:0", "default", "-disposition:s:1", "0",
            "-disposition:s:2", "0", "-disposition:s:3", "0", media});
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        QSettings().setValue("subtitles/defaultRules", "language:zh-hant");
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", QUrl::fromLocalFile(media)}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(video);
        video->play();
        QTRY_COMPARE(video->subtitleTracks().size(), 4);
        const auto tracks = video->subtitleTracks();
        const int simplified = tracks.at(0).toMap().value("id").toInt();
        const int traditional = tracks.at(2).toMap().value("id").toInt();
        const int taiwan = tracks.at(3).toMap().value("id").toInt();
        QCOMPARE(tracks.at(0).toMap().value("lang").toString().toLower(), QStringLiteral("zh-hans"));
        QCOMPARE(tracks.at(2).toMap().value("lang").toString().toLower(), QStringLiteral("zh-hant"));
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        video->togglePause();
        video->setDefaultSubtitleRules("language:ZHO_HANT");
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        video->setDefaultSubtitleRules("subtrackname:Simplified\nlanguage:zh-hant");
        QTRY_COMPARE(video->subtitleTrack(), simplified);
        video->setDefaultSubtitleRules("language:zh-hant\nsubtrackname:Simplified");
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        video->setDefaultSubtitleRules("language:zh-tw");
        QTRY_COMPARE(video->subtitleTrack(), taiwan);
        video->setDefaultSubtitleRules(QStringLiteral("language:zh-latn\ntracknum:%1").arg(traditional));
        QTRY_COMPARE(video->subtitleTrack(), traditional); // No Chinese track uses the requested script.
        video->setDefaultSubtitleRules("language:zh");
        QTRY_COMPARE(video->subtitleTrack(), simplified); // A broad language rule still matches any variant.

        // Current-language capture must keep Hans and Hant as distinct rules.
        video->setDefaultSubtitleRules("language:zh-hans");
        QTRY_COMPARE(video->subtitleTrack(), simplified);
        video->setSubtitleTrack(traditional);
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        auto *menu = window->findChild<QObject *>("subtitleSelectionMenu");
        auto *dialog = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(menu && dialog);
        auto *addLanguage = menu->findChild<QObject *>("addCurrentSubtitleLanguageRuleMenuItem");
        QVERIFY(addLanguage && addLanguage->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(addLanguage, "triggered"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *value = findItem(window->contentItem(), "subtitleRuleValue1");
        QTRY_VERIFY(value && value->hasActiveFocus());
        QCOMPARE(value->property("text").toString(), QStringLiteral("zh-hant"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_COMPARE(video->defaultSubtitleRules(), QStringLiteral("language:zh-hans\nlanguage:zh-hant"));
        QTRY_COMPARE(video->subtitleTrack(), simplified);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        video->setSubtitleTrack(traditional);
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        QVERIFY(QMetaObject::invokeMethod(addLanguage, "triggered"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!findItem(window->contentItem(), "subtitleRuleValue2"));
        QCOMPARE(findItem(window->contentItem(), "subtitleSettings")->property("ruleActionResult").toInt(), 2);
        value = findItem(window->contentItem(), "subtitleRuleValue1");
        QTRY_VERIFY(value && value->hasActiveFocus());
        auto *grip = findItem(window->contentItem(), "subtitleRuleGrip1");
        QVERIFY(grip);
        grip->forceActiveFocus();
        QTRY_VERIFY(grip->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);
        QTRY_COMPARE(findItem(window->contentItem(), "subtitleRuleValue0")->property("text").toString(),
                     QStringLiteral("zh-hant"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_COMPARE(video->defaultSubtitleRules(), QStringLiteral("language:zh-hant\nlanguage:zh-hans"));
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), traditional);
        QCOMPARE(video->errorString(), QString{});
    }

    void subtitlePriorityRulesApplyToAllFilesAndPersist()
    {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to create the multilingual Matroska fixture");
        QTemporaryDir directory;
        const auto rawVideo = makeVideo(directory.filePath("video.y4m"));
        QFile subtitles(directory.filePath("text.srt"));
        QVERIFY(subtitles.open(QIODevice::WriteOnly));
        subtitles.write("1\n00:00:00,000 --> 00:00:29,000\nSubtitle\n");
        subtitles.close();
        const QUrl first = QUrl::fromLocalFile(directory.filePath("first.mkv"));
        QProcess encoder;
        encoder.start(ffmpeg, {"-v", "error", "-i", rawVideo.toLocalFile(),
            "-i", subtitles.fileName(), "-i", subtitles.fileName(),
            "-map", "0:v", "-map", "1:s", "-map", "2:s", "-c:v", "ffv1", "-c:s", "srt",
            "-metadata:s:s:0", "language=eng", "-metadata:s:s:1", "language=jpn",
            "-metadata:s:s:0", "title=English", "-metadata:s:s:1", "title=Japanese",
            "-disposition:s:0", "default", "-disposition:s:1", "0", first.toLocalFile()});
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        QVERIFY(QDir(directory.path()).mkdir("other"));
        const QUrl second = QUrl::fromLocalFile(directory.filePath("other/second.mkv"));
        QVERIFY(QFile::copy(first.toLocalFile(), second.toLocalFile()));
        QString savedRules;
        {
            QQmlApplicationEngine engine;
            engine.setInitialProperties({{"mediaSource", first}});
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            auto *playlists = window->findChild<PlaylistCollection *>();
            QVERIFY(video && playlists);
            video->play();
            QTRY_COMPARE(video->subtitleTracks().size(), 2);
            const int english = video->subtitleTracks().first().toMap().value("id").toInt();
            const int japanese = video->subtitleTracks().last().toMap().value("id").toInt();
            savedRules = QStringLiteral("subtrackname:Missing\nlanguage:ja\ntracknum:%1").arg(english);
            video->setDefaultSubtitleRules(savedRules);
            QTRY_COMPARE(video->subtitleTrack(), japanese);
            video->setSubtitleTrack(english);
            QTRY_COMPARE(video->subtitleTrack(), english);
            QCOMPARE(video->defaultSubtitleRules(), savedRules);

            // A manual choice belongs to this file; the next file uses the global rules.
            QCOMPARE(playlists->currentPlaylist()->appendUrls({second}), 1);
            QVERIFY(QMetaObject::invokeMethod(window, "playTrack", Q_ARG(int, 1)));
            QTRY_COMPARE(video->source(), second);
            QTRY_COMPARE(video->subtitleTracks().size(), 2);
            QTRY_COMPARE(video->subtitleTrack(), japanese);
            QCOMPARE(video->defaultSubtitleRules(), savedRules);

            QCOMPARE(playlists->addPlaylist("Another playlist"), 1);
            QCOMPARE(playlists->currentPlaylist()->appendUrls({first}), 1);
            QVERIFY(QMetaObject::invokeMethod(window, "playTrack", Q_ARG(int, 0)));
            QTRY_COMPARE(video->source(), first);
            QTRY_COMPARE(video->subtitleTracks().size(), 2);
            QTRY_COMPARE(video->subtitleTrack(), japanese);
            QCOMPARE(video->defaultSubtitleRules(), savedRules);
            window->close();
        }
        {
            QQmlApplicationEngine engine;
            engine.setInitialProperties({{"startupFiles", QVariant::fromValue(QList<QUrl>{second})}});
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            QVERIFY(video);
            QCOMPARE(video->defaultSubtitleRules(), savedRules);
            QTRY_COMPARE(video->source(), second);
            QTRY_VERIFY(video->playing());
            QTRY_COMPARE(video->subtitleTracks().size(), 2);
            QTRY_COMPARE(video->subtitleTrack(), video->subtitleTracks().last().toMap().value("id").toInt());
            QCOMPARE(video->errorString(), QString{});
        }
    }

    void subtitleStyleChangesRenderedAss()
    {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg is needed to create the subtitle rendering fixture");
        QTemporaryDir directory;
        const QString media = directory.filePath("black.mkv");
        QProcess encoder;
        encoder.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=640x360:r=1:d=30", "-c:v", "ffv1", media});
        QVERIFY(encoder.waitForFinished(10000));
        QCOMPARE(encoder.exitCode(), 0);
        QFile subtitle(directory.filePath("styled.ass"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly));
        subtitle.write(
            "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
            "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
            "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,sans-serif,14,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,"
            "0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n"
            "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:00.00,0:00:29.00,Default,,0,0,0,,Subtitle rendering\n");
        subtitle.close();
        QQmlApplicationEngine engine;
        // Match the video aspect ratio: video-only captures exclude subtitles in window margins.
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import KPlayer
            Window {
                width: 640; height: 360; visible: true
                MpvVideo { anchors.fill: parent }
            }
        )", QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(video);
        video->setSource(QUrl::fromLocalFile(media));
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        video->togglePause();
        QVERIFY(video->addSubtitle(QUrl::fromLocalFile(subtitle.fileName())));
        QTRY_VERIFY(video->subtitleTrack() > 0);
        video->seek(2);
        QTRY_VERIFY(video->position() >= 1.9);
        QVERIFY(!video->subtitleStyleOverride());
        video->setSubtitleFontSize(48);
        QSignalSpy captures(video, &MpvVideo::frameCaptured);
        const auto capture = [&](const QString &name) {
            const QString path = directory.filePath(name + ".png");
            video->captureFrame(QUrl::fromLocalFile(path));
            return path;
        };
        const auto textBounds = [](const QString &path) {
            const QImage image(path);
            QRect bounds;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qGray(image.pixel(x, y)) > 180)
                        bounds = bounds.united(QRect(x, y, 1, 1));
                }
            }
            return bounds;
        };
        const QString original = capture("authored");
        QTRY_COMPARE(captures.count(), 1);
        const QRect authored = textBounds(original);
        QVERIFY(!authored.isEmpty());
        video->setSubtitleStyleOverride(true);
        QTest::qWait(200);
        const QString enlarged = capture("enlarged");
        QTRY_COMPARE(captures.count(), 2);
        const QRect styled = textBounds(enlarged);
        QVERIFY2(styled.height() > authored.height(), qPrintable(QString("ASS height: authored %1, styled %2").arg(authored.height()).arg(styled.height())));
        video->setSubtitleAlignment(-1);
        video->setSubtitlePosition(60);
        QTest::qWait(200);
        const QString moved = capture("moved");
        QTRY_COMPARE(captures.count(), 3);
        const QRect repositioned = textBounds(moved);
        QVERIFY(!repositioned.isEmpty());
        QVERIFY(repositioned.left() < styled.left());
        QVERIFY(repositioned.top() < styled.top());
        video->setSubtitleBottomInset(60);
        QCOMPARE(video->subtitlePosition(), 60); // UI clearance never changes the saved setting.
        QTest::qWait(200);
        const QString inset = capture("inset");
        QTRY_COMPARE(captures.count(), 4);
        const QRect raised = textBounds(inset);
        QVERIFY(!raised.isEmpty());
        QVERIFY(qAbs(repositioned.top() - raised.top() - 60) <= 3);
        video->setSubtitleBottomInset(0);
        QTest::qWait(200);
        const QString restored = capture("restored");
        QTRY_COMPARE(captures.count(), 5);
        QCOMPARE(textBounds(restored), repositioned);
        QCOMPARE(video->errorString(), QString{});
    }

    void siblingDiscoveryOnlyAddsVideosFromOpenedFolders()
    {
        QTemporaryDir directory;
        const QUrl selected = makeVideo(directory.filePath("episode1.y4m"));
        const QUrl sibling = makeVideo(directory.filePath("episode2.MP4"));
        const QUrl audio = makeAudio(directory.filePath("music.wav"));
        QVERIFY(QDir(directory.path()).mkdir("nested"));
        const QUrl nested = makeVideo(directory.filePath("nested/episode3.y4m"));
        QFile source(directory.filePath("source.ts"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("export const notVideo = true;");
        source.close();
        PlaylistModel playlist;
        VideoLibrary library;
        playlist.appendUrls({selected});
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);
        QCOMPARE(library.addSiblingVideos(&playlist, {selected, sibling}), 1);
        QCOMPARE(playlist.count(), 2);
        QCOMPARE(playlist.currentSource(), selected);
        QCOMPARE(sourceChanges.count(), 0);
        QVERIFY(playlist.indexOfSource(sibling) >= 0);
        QCOMPARE(playlist.indexOfSource(nested), -1);
        QCOMPARE(playlist.indexOfSource(audio), -1);
        QCOMPARE(library.addSiblingVideos(&playlist, {selected}), 0);
        PlaylistModel audioPlaylist;
        QCOMPARE(library.addSiblingVideos(&audioPlaylist, {audio}), 0);
        QCOMPARE(library.addSiblingVideos(&playlist, {QUrl("https://example.com/movie.mp4")}), 0);
    }

    void siblingDiscoverySortsNumberedVideosWithoutChangingPlayback()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("video-1.y4m"));
        const QUrl second = makeVideo(directory.filePath("video-2.y4m"));
        const QUrl twelfth = makeVideo(directory.filePath("video-12.y4m"));
        PlaylistModel playlist;
        QAbstractItemModelTester tester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        VideoLibrary library;
        playlist.appendUrls({twelfth});
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);

        QCOMPARE(library.addSiblingVideos(&playlist, {twelfth}), 2);
        QCOMPARE(playlist.indexOfSource(first), 0);
        QCOMPARE(playlist.indexOfSource(second), 1);
        QCOMPARE(playlist.indexOfSource(twelfth), 2);
        QCOMPARE(playlist.currentIndex(), 2);
        QCOMPARE(playlist.currentSource(), twelfth);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(library.addSiblingVideos(&playlist, {twelfth}), 0);
        QCOMPARE(playlist.count(), 3);
    }

    void openingMediaAddsSiblingsWithoutLibraryScan()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"startupFiles", QVariant::fromValue(QList<QUrl>{second})}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(playlist && video);
        QTRY_COMPARE(playlist->count(), 2);
        QTRY_VERIFY(video->mediaLoaded());
        QCOMPARE(video->source(), second);
        QCOMPARE(playlist->currentSource(), second);
        QCOMPARE(playlist->indexOfSource(first), 0);
        QCOMPARE(playlist->currentIndex(), 1);
        // Exercise the same handler used by the Open Media dialog on a named queue.
        auto *collection = window->findChild<PlaylistCollection *>();
        QVERIFY(collection);
        collection->addPlaylist("Opened files");
        QVERIFY(QMetaObject::invokeMethod(window, "addOpenedFiles",
                                          Q_ARG(QList<QUrl>, QList<QUrl>{first})));
        QTRY_COMPARE(collection->currentPlaylist()->count(), 2);
        QTRY_VERIFY(video->mediaLoaded());
        QCOMPARE(video->source(), first);
        QVERIFY(QMetaObject::invokeMethod(window, "addOpenedFiles",
                                          Q_ARG(QList<QUrl>, QList<QUrl>{first})));
        QCOMPARE(collection->currentPlaylist()->count(), 2);
        QCOMPARE(playlist->count(), 2);
        video->stop();
    }

    void speedChangesIgnoreStaleMpvNotifications()
    {
        MpvVideo video;
        QSignalSpy speedChanges(&video, &MpvVideo::speedChanged);
        // Queue changes before mpv's initial observation and replies reach Qt.
        video.setSpeed(2.0);
        video.toggleSpeed();
        video.toggleSpeed();
        QCOMPARE(video.speed(), 2.0);
        QCOMPARE(speedChanges.count(), 3);
        QTest::qWait(100);
        QCOMPARE(video.speed(), 2.0);
        QCOMPARE(speedChanges.count(), 3);
    }

    void speedToggleIgnoresKeyRepeat()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *stage = findItem(window->contentItem(), "videoStage");
        QVERIFY(video && stage);
        click(window, stage);
        QVERIFY(window->property("playbackShortcutsEnabled").toBool());
        video->setSpeed(2.0);
        QTest::qWait(100); // Let mpv acknowledge the starting speed before counting toggles.
        QCOMPARE(video->speed(), 2.0);
        QSignalSpy speedChanges(video, &MpvVideo::speedChanged);

        QTest::keyPress(window, Qt::Key_Z);
        QCOMPARE(video->speed(), 1.0);
        for (int repeat = 0; repeat < 3; ++repeat) {
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier,
                              QStringLiteral("z"), true);
            QCoreApplication::sendEvent(window, &release);
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier,
                            QStringLiteral("z"), true);
            QCoreApplication::sendEvent(window, &press);
        }
        QTest::keyRelease(window, Qt::Key_Z);
        QTest::qWait(100);
        QCOMPARE(video->speed(), 1.0);
        QCOMPARE(speedChanges.count(), 1);

        // A separate press still restores the remembered speed immediately.
        QTest::keyClick(window, Qt::Key_Z);
        QTest::qWait(100);
        QCOMPARE(video->speed(), 2.0);
        QCOMPARE(speedChanges.count(), 2);
    }

    void subtitleMenuAndPotPlayerShortcuts()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"));
        QList<QUrl> subtitles;
        for (const QString &name : {QStringLiteral("English.srt"), QStringLiteral("中文.srt")}) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("1\n00:00:00,000 --> 00:00:29,000\nTest subtitle\n");
            subtitles.append(QUrl::fromLocalFile(file.fileName()));
        }
        QList<QQmlError> warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlEngine::warnings, this,
                [&](const QList<QQmlError> &items) { warnings.append(items); });
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *playlist = window->findChild<PlaylistModel *>();
        QVERIFY(video && playlist);
        playlist->appendUrls({second});
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        click(window, findItem(window->contentItem(), "videoStage"));
        QTest::keyClick(window, Qt::Key_C);
        QTRY_COMPARE(video->speed(), 1.1);
        auto *hint = findItem(window->contentItem(), "playbackHint");
        auto *stage = findItem(window->contentItem(), "videoStage");
        QVERIFY(hint && stage);
        QTRY_VERIFY(hint->isVisible());
        QCOMPARE(hint->property("text").toString(), QStringLiteral("1.1x"));
        QCOMPARE(hint->property("color").value<QColor>(), QColor("#2489ee"));
        QCOMPARE(hint->mapToItem(stage, QPointF{}), QPointF(16, 16));
        QTest::keyClick(window, Qt::Key_X);
        QTRY_COMPARE(video->speed(), 1.0);
        QCOMPARE(hint->property("text").toString(), QStringLiteral("1.0x"));
        video->setSpeed(2.0);
        QTRY_COMPARE(video->speed(), 2.0);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(video->speed(), 1.0);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(video->speed(), 2.0);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(video->speed(), 1.0);
        video->setSpeed(0.2);
        QTest::keyClick(window, Qt::Key_X);
        QTRY_COMPARE(video->speed(), 0.2);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(video->speed(), 1.0);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(video->speed(), 0.2);
        video->setSpeed(16.0);
        QTest::keyClick(window, Qt::Key_C);
        QTRY_COMPARE(video->speed(), 16.0);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(video->speed(), 1.0);

        // Load through the dialog acceptance path, then observe mpv's actual track list.
        QTest::keyClick(window, Qt::Key_O, Qt::AltModifier);
        auto *dialog = window->findChild<QObject *>("subtitleDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        // Supply the chosen URL and dispatch the accepted signal; this avoids
        // depending on the platform file picker implementation in the test.
        QVERIFY(dialog->setProperty("selectedFile", subtitles.first()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        QTRY_COMPARE(video->subtitleTracks().size(), 1);
        QVERIFY(video->addSubtitle(subtitles.last()));
        QTRY_COMPARE(video->subtitleTracks().size(), 2);
        const int firstId = video->subtitleTracks().first().toMap().value("id").toInt();
        const int secondId = video->subtitleTracks().last().toMap().value("id").toInt();
        QTRY_COMPARE(video->subtitleTrack(), secondId);
        QVERIFY(video->subtitleTracks().last().toMap().value("label").toString().contains("中文.srt"));
        // QTest mouse input does not restore native activation after a Windows file picker.
        QVERIFY(exposeWindow(window));
        click(window, findItem(window->contentItem(), "videoStage"));
        QTest::keyClick(window, Qt::Key_H, Qt::AltModifier);
        QTRY_VERIFY(!video->subtitlesVisible());
        QCOMPARE(video->subtitleTrack(), secondId);
        QTest::keyClick(window, Qt::Key_H, Qt::AltModifier);
        QTRY_VERIFY(video->subtitlesVisible());
        QTest::keyClick(window, Qt::Key_L, Qt::AltModifier);
        QTRY_COMPARE(video->subtitleTrack(), firstId);
        QTest::keyClick(window, Qt::Key_L);
        auto *menu = window->findChild<QObject *>("subtitleSelectionMenu");
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("count").toInt(), 9 + video->subtitleTracks().size());
        QVERIFY(menuItemAt(menu, 5)->property("checked").toBool());
        click(window, menuItemAt(menu, 6));
        QTRY_COMPARE(video->subtitleTrack(), secondId);
        video->setSubtitleTrack(-1);
        QTRY_COMPARE(video->subtitleTrack(), -1);
        video->setSubtitlesVisible(false);
        video->setSubtitleTrack(firstId);
        QTRY_COMPARE(video->subtitleTrack(), firstId);
        QTRY_VERIFY(video->subtitlesVisible());

        video->setSubtitleTrack(secondId);
        QTRY_COMPARE(video->subtitleTrack(), secondId);
        auto *trackNamePreference = menu->findChild<QObject *>("subtitlePreferenceTrackNameMenuItem");
        auto *trackNumberPreference = menu->findChild<QObject *>("subtitlePreferenceTrackNumberMenuItem");
        auto *automaticPreference = menu->findChild<QObject *>("subtitlePreferenceAutomaticMenuItem");
        QVERIFY(trackNamePreference && trackNumberPreference && automaticPreference);
        QVERIFY(trackNamePreference->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(trackNamePreference, "triggered"));
        QTRY_COMPARE(video->defaultSubtitleTrackName(), QStringLiteral("中文.srt"));
        QCOMPARE(video->defaultSubtitleTrack(), 0);
        QCOMPARE(video->defaultSubtitleLanguage(), QString{});
        QVERIFY(QMetaObject::invokeMethod(trackNumberPreference, "triggered"));
        QTRY_COMPARE(video->defaultSubtitleTrack(), secondId);
        QCOMPARE(video->defaultSubtitleTrackName(), QString{});
        QCOMPARE(video->defaultSubtitleLanguage(), QString{});
        QVERIFY(QMetaObject::invokeMethod(automaticPreference, "triggered"));
        QTRY_COMPARE(video->defaultSubtitleTrack(), 0);
        QCOMPARE(video->defaultSubtitleTrackName(), QString{});
        QCOMPARE(video->defaultSubtitleLanguage(), QString{});

        click(window, findItem(window->contentItem(), "videoStage"));
        QTest::keyClick(window, Qt::Key_PageDown);
        QTRY_COMPARE(video->source(), second);
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_VERIFY(video->subtitleTracks().isEmpty());
        QTest::keyClick(window, Qt::Key_PageUp);
        QTRY_COMPARE(video->source(), first);
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        video->seek(12);
        QTRY_VERIFY(qAbs(video->position() - 12) < 0.2);
        QTest::keyClick(window, Qt::Key_Backspace);
        QTRY_VERIFY(video->position() < 0.2);
        QTest::keyClick(window, Qt::Key_F);
        QTRY_VERIFY(video->position() > 0.5 && !video->playing());
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        auto *search = findItem(window->contentItem(), "playlistSearch");
        QTRY_VERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_X);
        QTest::keyClick(window, Qt::Key_C);
        QTest::keyClick(window, Qt::Key_Z);
        QTest::keyClick(window, Qt::Key_L);
        QCOMPARE(search->property("text").toString(), QStringLiteral("xczl"));
        QCOMPARE(video->speed(), 1.0);
        QVERIFY(!menu->property("visible").toBool());
        video->stop();
        QVERIFY(!video->mediaLoaded());
        QVERIFY(video->subtitleTracks().isEmpty());
        QCOMPARE(video->subtitleTrack(), -1);
        QCOMPARE(video->errorString(), QString{});
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString{} : warnings.first().toString()));
    }

    void discoveryDeduplicatesWithoutChangingManualQueues()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"));
        PlaylistModel playlist;
        QAbstractItemModelTester tester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(playlist.appendUrls({first, first}), 2);
        playlist.setCurrentIndex(1);
        QSignalSpy changes(&playlist, &PlaylistModel::currentSourceChanged);
        QCOMPARE(playlist.appendUniqueUrls({first, second, second, QUrl{}}), 1);
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(playlist.currentSource(), first);
        QCOMPARE(changes.size(), 0);
        QCOMPARE(playlist.indexOfSource(second), 2);
        const QUrl alias = QUrl::fromLocalFile(directory.path() + "/./first.y4m");
        QCOMPARE(playlist.appendUniqueUrls({alias}), 0);
        QCOMPARE(playlist.indexOfSource(alias), 0);
        QVERIFY(playlist.removeAt(0));
        QCOMPARE(playlist.appendUniqueUrls({first}), 0);
        QVERIFY(playlist.removeAt(0));
        QCOMPARE(playlist.appendUniqueUrls({first}), 1);
        playlist.clear();
        QCOMPARE(playlist.appendUniqueUrls({first, second}), 2);
    }

    void siblingDiscoveryRejectsTypeScriptButKeepsTransportStreams()
    {
        QTemporaryDir directory;
        for (const QString &name : {QStringLiteral("source.ts"), QStringLiteral("source.mts")}) {
            QFile source(directory.filePath(name));
            QVERIFY(source.open(QIODevice::WriteOnly));
            source.write("export const greeting: string = 'Hello';\n");
        }
        for (int packetSize : {188, 192, 204}) {
            QFile stream(directory.filePath(QString::number(packetSize)
                         + (packetSize == 192 ? ".mts" : ".ts")));
            QVERIFY(stream.open(QIODevice::WriteOnly));
            QByteArray packet(packetSize, char(0xff));
            const int offset = packetSize == 192 ? 4 : 0;
            // Four valid transport-stream null-packet headers with stuffing.
            packet[offset] = char(0x47);
            packet[offset + 1] = char(0x1f);
            packet[offset + 2] = char(0xff);
            packet[offset + 3] = char(0x10);
            for (int count = 0; count < 4; ++count)
                stream.write(packet);
        }
        PlaylistModel playlist;
        VideoLibrary library;
        const QUrl selected = makeVideo(directory.filePath("selected.y4m"));
        playlist.appendUrls({selected});
        QCOMPARE(library.addSiblingVideos(&playlist, {selected}), 3);
        QCOMPARE(playlist.count(), 4);
        QCOMPARE(playlist.indexOfSource(QUrl::fromLocalFile(directory.filePath("source.ts"))), -1);
        QCOMPARE(playlist.indexOfSource(QUrl::fromLocalFile(directory.filePath("source.mts"))), -1);
    }

    void startupWithoutOpenedFilesKeepsPlaylistEmpty()
    {
        QTemporaryDir directory;
        makeVideo(directory.filePath("nearby.y4m"));
        const QString previousDirectory = QDir::currentPath();
        struct RestoreDirectory {
            QString path;
            ~RestoreDirectory() { QDir::setCurrent(path); }
        } restore{previousDirectory};
        QVERIFY(QDir::setCurrent(directory.path()));
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(playlist && video);
        QTest::qWait(200);
        QCOMPARE(playlist->count(), 0);
        QVERIFY(video->source().isEmpty());
        QVERIFY(!video->playing());
    }

    void explorerStartupOnlyDiscoversSameFolder()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("第一部 video.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"));
        QVERIFY(QDir(directory.path()).mkpath("nested/deep"));
        const QUrl nested = makeVideo(directory.filePath("nested/deep/nested.y4m"));
        QTemporaryDir otherDirectory;
        const QUrl unrelated = makeVideo(otherDirectory.filePath("unrelated.y4m"));
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&](const QList<QQmlError> &items) { warnings.append(items); });
        engine.setInitialProperties({
            {"startupFiles", QVariant::fromValue(QList<QUrl>{first})}
        });
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *library = window->findChild<VideoLibrary *>();
        auto *playlists = window->findChild<PlaylistCollection *>();
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(library && playlists && video);
        QTRY_VERIFY(video->playing());
        QTRY_VERIFY(video->seekable());
        QCOMPARE(playlists->defaultPlaylist()->count(), 2);
        QCOMPARE(playlists->defaultPlaylist()->currentSource(), first);
        QCOMPARE(video->source(), first);
        QCOMPARE(video->loadRevision(), quint64(1));
        QVERIFY(playlists->defaultPlaylist()->indexOfSource(second) >= 0);
        QCOMPARE(playlists->defaultPlaylist()->indexOfSource(nested), -1);
        QCOMPARE(playlists->defaultPlaylist()->indexOfSource(unrelated), -1);
        QCOMPARE(playlists->addPlaylist("Other"), 1);
        QCOMPARE(playlists->currentPlaylist()->count(), 0);
        QCOMPARE(playlists->defaultPlaylist()->count(), 2);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
    }

    void windowsVideoRegistrationQuotesPathsAndPreservesDefaults()
    {
#ifdef Q_OS_WIN
        const QString registryRoot = "Software\\KPlayerTests\\"
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSettings settings("HKEY_CURRENT_USER\\" + registryRoot, QSettings::NativeFormat);
        // Clean up this isolated test key even if an assertion fails.
        struct Cleanup {
            QSettings &settings;
            ~Cleanup() { settings.clear(); settings.sync(); }
        } cleanup{settings};
        settings.setValue("Classes/.mp4/Default", "OtherPlayer.Video");
        settings.setValue("Microsoft/Windows/CurrentVersion/Explorer/FileExts/.mp4/UserChoice/ProgId",
                          "OtherPlayer.Video");
        settings.sync();
        const QString executable = QDir::toNativeSeparators(
            QDir::tempPath() + "/含空格 folder/KPlayer.exe");
        QString error;
        QVERIFY2(FileAssociations::registerApplication(executable, registryRoot, &error),
                 qPrintable(error));
        settings.sync();
        QCOMPARE(settings.value("Classes/KPlayer.Video/shell/open/command/Default").toString(),
                 QStringLiteral("\"%1\" \"%2\"").arg(executable, QStringLiteral("%1")));
        QCOMPARE(settings.value("RegisteredApplications/KPlayer").toString(),
                 registryRoot + "\\KPlayer\\Capabilities");
        for (const QString &extension : VideoFormats::extensions()) {
            QCOMPARE(settings.value("KPlayer/Capabilities/FileAssociations/." + extension).toString(),
                     QStringLiteral("KPlayer.Video"));
            QVERIFY(settings.contains("Classes/." + extension + "/OpenWithProgids/KPlayer.Video"));
        }
        QCOMPARE(settings.value("Classes/.mp4/Default").toString(), QStringLiteral("OtherPlayer.Video"));
        QCOMPARE(settings.value("Microsoft/Windows/CurrentVersion/Explorer/FileExts/.mp4/UserChoice/ProgId")
                     .toString(), QStringLiteral("OtherPlayer.Video"));
#else
        QSKIP("Windows registration test");
#endif
    }

    void namedPlaylistsKeepIndependentQueues()
    {
        PlaylistCollection playlists;
        QAbstractItemModelTester modelTester(&playlists,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto *defaultQueue = playlists.currentPlaylist();
        const QUrl first("https://example.com/first.mp4");
        const QUrl second("https://example.com/second.mp4");
        QCOMPARE(defaultQueue->appendUrls({first, second}), 2);
        defaultQueue->setCurrentIndex(1);
        QVERIFY(!playlists.removePlaylist(0));
        QVERIFY(!playlists.renamePlaylist(0, "Changed"));
        QCOMPARE(playlists.addPlaylist("  "), -1);
        QCOMPARE(playlists.addPlaylist(playlists.currentName()), -1);
        QCOMPARE(playlists.addPlaylist("  Music  "), 1);
        auto *musicQueue = playlists.currentPlaylist();
        QVERIFY(musicQueue != defaultQueue);
        QCOMPARE(playlists.currentName(), QStringLiteral("Music"));
        QCOMPARE(musicQueue->count(), 0);
        QCOMPARE(musicQueue->appendUrls({first}), 1);
        QCOMPARE(playlists.addPlaylist("music"), -1);
        QCOMPARE(playlists.addPlaylist("Films"), 2);
        auto *filmsQueue = playlists.currentPlaylist();
        QCOMPARE(filmsQueue->appendUrls({second}), 1);
        QVERIFY(!playlists.renamePlaylist(2, "MUSIC"));
        QVERIFY(playlists.renamePlaylist(2, "  Movies  "));
        QCOMPARE(playlists.currentName(), QStringLiteral("Movies"));

        // Removing an earlier queue keeps the active queue and its media intact.
        QVERIFY(playlists.removePlaylist(1));
        QCOMPARE(playlists.currentIndex(), 1);
        QCOMPARE(playlists.currentPlaylist(), filmsQueue);
        QCOMPARE(filmsQueue->currentSource(), second);
        QVERIFY(playlists.removePlaylist(1));
        QCOMPARE(playlists.currentIndex(), 0);
        QCOMPARE(playlists.currentPlaylist(), defaultQueue);
        QCOMPARE(defaultQueue->count(), 2);
        QCOMPARE(defaultQueue->currentIndex(), 1);
        QCOMPARE(defaultQueue->currentSource(), second);
        QCOMPARE(playlists.count(), 1);
        playlists.setCurrentIndex(-1);
        playlists.setCurrentIndex(5);
        QCOMPARE(playlists.currentPlaylist(), defaultQueue);
        QVERIFY(!playlists.removePlaylist(5));
        QVERIFY(!playlists.renamePlaylist(-1, "Invalid"));
    }

    void playlistSelectorManagesNamedQueues()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"), true);
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *playlists = window->findChild<PlaylistCollection *>();
        auto *menu = window->findChild<QObject *>("playlistOptionsMenu");
        auto *dialog = window->findChild<QObject *>("playlistNameDialog");
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(playlists && menu && dialog && video);
        auto *defaultQueue = playlists->currentPlaylist();
        auto *arrow = item("playlistOptionsButton");
        QVERIFY(arrow);
        click(window, arrow);
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *remove = item("deleteNamedPlaylistMenuItem");
        auto *edit = item("editNamedPlaylistMenuItem");
        auto *add = item("addNamedPlaylistMenuItem");
        QVERIFY(remove && edit && add);
        QVERIFY(!remove->isEnabled() && !edit->isEnabled());
        QVERIFY(menuItemAt(menu, 0)->property("checked").toBool());
        click(window, add);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *name = item("playlistNameField");
        QVERIFY(name);
        name->setProperty("text", "Music");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(playlists->count(), 2);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        auto *musicQueue = playlists->currentPlaylist();
        QCOMPARE(musicQueue->appendUrls({second}), 1);
        QTRY_COMPARE(video->source(), second);
        click(window, item("playButton"));
        QTRY_VERIFY(video->playing());

        click(window, arrow);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(remove->isEnabled() && edit->isEnabled());
        click(window, edit);
        QTRY_VERIFY(dialog->property("opened").toBool());
        name->setProperty("text", playlists->data(playlists->index(0),
                                                  PlaylistCollection::NameRole).toString());
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(playlists->currentName(), QStringLiteral("Music"));
        name->setProperty("text", "Albums");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(playlists->currentName(), QStringLiteral("Albums"));
        QVERIFY(video->playing());

        click(window, arrow);
        QTRY_VERIFY(menu->property("opened").toBool());
        click(window, menuItemAt(menu, 0));
        QTRY_COMPARE(playlists->currentPlaylist(), defaultQueue);
        QCOMPARE(video->source(), second);
        QVERIFY(video->playing());
        QCOMPARE(defaultQueue->count(), 1);
        QCOMPARE(musicQueue->currentSource(), second);
        click(window, arrow);
        QTRY_VERIFY(menu->property("opened").toBool());
        const QString capturePath = qEnvironmentVariable("KPLAYER_PLAYLIST_CAPTURE");
        if (!capturePath.isEmpty())
            QVERIFY(window->grabWindow().save(capturePath));
        click(window, menuItemAt(menu, 1));
        QTRY_COMPARE(playlists->currentPlaylist(), musicQueue);
        QTRY_COMPARE(video->source(), second);
        auto *defaultTab = item("playlistTab0");
        QVERIFY(defaultTab);
        defaultTab->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(playlists->currentPlaylist(), defaultQueue);
        QCOMPARE(video->source(), second);
        QVERIFY(video->playing());
        auto *musicTab = item("playlistTab1");
        QVERIFY(musicTab);
        click(window, musicTab);
        QTRY_COMPARE(playlists->currentPlaylist(), musicQueue);
        click(window, arrow);
        QTRY_VERIFY(menu->property("opened").toBool());
        click(window, remove);
        QTRY_COMPARE(playlists->count(), 1);
        QTRY_COMPARE(playlists->currentPlaylist(), defaultQueue);
        QTRY_COMPARE(video->source(), first);
        click(window, arrow);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(!remove->isEnabled() && !edit->isEnabled());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY2(warnings.isEmpty(), "Playlist management generated QML warnings");
    }

    void playlistEditingPreservesActiveSource()
    {
        PlaylistModel playlist;
        QAbstractItemModelTester modelTester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QUrl gamma("https://example.com/gamma.wav");
        const QUrl beta("https://example.com/beta.wav");
        const QUrl alpha("https://example.com/alpha.wav");
        QCOMPARE(playlist.appendUrls({gamma, beta, alpha}), 3);
        playlist.setCurrentIndex(2);
        QSignalSpy indexChanges(&playlist, &PlaylistModel::currentIndexChanged);
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);
        QSignalSpy countChanges(&playlist, &PlaylistModel::countChanged);
        QSignalSpy rowMoves(&playlist, &QAbstractItemModel::rowsMoved);
        QSignalSpy rowRemovals(&playlist, &QAbstractItemModel::rowsRemoved);

        QVERIFY(!playlist.removeAt(-1));
        QVERIFY(!playlist.removeAt(3));
        QVERIFY(!playlist.moveTrack(-1, 0));
        QVERIFY(!playlist.moveTrack(0, 3));
        QVERIFY(!playlist.moveTrack(3, 0));
        playlist.moveTrack(2, 2);
        QCOMPARE(playlist.count(), 3);
        QCOMPARE(indexChanges.count(), 0);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 0);
        QCOMPARE(rowMoves.count(), 0);
        QCOMPARE(rowRemovals.count(), 0);

        // Moving another row past the active track changes its row, not its media.
        QVERIFY(playlist.moveTrack(0, 2));
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(playlist.currentSource(), alpha);
        QCOMPARE(playlist.data(playlist.index(2), PlaylistModel::SourceRole).toUrl(), gamma);
        QCOMPARE(indexChanges.count(), 1);
        QCOMPARE(rowMoves.count(), 1);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 0);

        QVERIFY(playlist.moveTrack(1, 0));
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), alpha);
        QCOMPARE(indexChanges.count(), 2);
        QCOMPARE(rowMoves.count(), 2);
        QCOMPARE(sourceChanges.count(), 0);
        QVERIFY(playlist.removeAt(2));
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), alpha);
        QCOMPARE(indexChanges.count(), 2);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 1);
        QCOMPARE(rowRemovals.count(), 1);

        playlist.setCurrentIndex(1);
        indexChanges.clear();
        sourceChanges.clear();
        QVERIFY(playlist.removeAt(0));
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), beta);
        QCOMPARE(indexChanges.count(), 1);
        QCOMPARE(sourceChanges.count(), 0);
    }

    void playlistGroupMovesPreserveOrderAndPlayback()
    {
        PlaylistModel playlist;
        QAbstractItemModelTester tester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QList<QUrl> urls = {QUrl("https://example.com/1"), QUrl("https://example.com/2"),
            QUrl("https://example.com/3"), QUrl("https://example.com/4"), QUrl("https://example.com/5")};
        playlist.appendUrls(urls);
        playlist.setCurrentIndex(2);
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);
        QCOMPARE(playlist.moveTracks({1, 2}, 5), QList<int>({3, 4}));
        const QList<QUrl> expected = {urls[0], urls[3], urls[4], urls[1], urls[2]};
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(playlist.data(playlist.index(i), PlaylistModel::SourceRole).toUrl(), expected[i]);
        QCOMPARE(playlist.currentIndex(), 4);
        QCOMPARE(playlist.currentSource(), urls[2]);
        QCOMPARE(playlist.moveTracks({4, 1, 4}, 0), QList<int>({0, 1}));
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(playlist.currentSource(), urls[2]);
        QCOMPARE(playlist.moveTracks({0, 1}, 1), QList<int>({0, 1}));
        QVERIFY(playlist.moveTracks({-1}, 0).isEmpty());
        QVERIFY(playlist.moveTracks({0}, 6).isEmpty());
        QCOMPARE(sourceChanges.count(), 0);
    }

    void configurablePlaybackSpeed()
    {
        MpvVideo video;
        QCOMPARE(video.maximumSpeed(), 16.0);
        video.setSpeed(30);
        QCOMPARE(video.speed(), 16.0);
        video.setMaximumSpeed(32);
        video.setSpeed(24.5);
        QCOMPARE(video.speed(), 24.5);
        video.setMaximumSpeed(8);
        QCOMPARE(video.speed(), 8.0);
        video.toggleSpeed();
        QCOMPARE(video.speed(), 1.0);
        video.toggleSpeed();
        QCOMPARE(video.speed(), 8.0);
        video.setMaximumSpeed(0);
        QCOMPARE(video.maximumSpeed(), 1.0);
        QCOMPARE(video.speed(), 1.0);
    }

    void playlistRemovalAndClearChooseValidMedia()
    {
        PlaylistModel playlist;
        QAbstractItemModelTester modelTester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QUrl first("https://example.com/first.wav");
        const QUrl second("https://example.com/second.wav");
        QCOMPARE(playlist.appendUrls({first, first, second}), 3);
        QSignalSpy indexChanges(&playlist, &PlaylistModel::currentIndexChanged);
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);
        QSignalSpy countChanges(&playlist, &PlaylistModel::countChanged);

        // Replacing the active entry with a duplicate must not restart playback.
        QVERIFY(playlist.removeAt(0));
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), first);
        QCOMPARE(indexChanges.count(), 0);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 1);
        QVERIFY(playlist.removeAt(0));
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), second);
        QCOMPARE(sourceChanges.count(), 1);
        QVERIFY(playlist.removeAt(0));
        QCOMPARE(playlist.count(), 0);
        QCOMPARE(playlist.currentIndex(), -1);
        QCOMPARE(playlist.currentSource(), QUrl{});
        QCOMPARE(indexChanges.count(), 1);
        QCOMPARE(sourceChanges.count(), 2);
        QCOMPARE(countChanges.count(), 3);

        QCOMPARE(playlist.appendUrls({first, second}), 2);
        playlist.setCurrentIndex(1);
        indexChanges.clear();
        sourceChanges.clear();
        countChanges.clear();
        // Removing the final active row selects the preceding row.
        QVERIFY(playlist.removeAt(1));
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), first);
        QCOMPARE(indexChanges.count(), 1);
        QCOMPARE(sourceChanges.count(), 1);
        indexChanges.clear();
        sourceChanges.clear();
        countChanges.clear();
        QSignalSpy resets(&playlist, &QAbstractItemModel::modelReset);
        playlist.clear();
        QCOMPARE(playlist.count(), 0);
        QCOMPARE(playlist.currentIndex(), -1);
        QCOMPARE(playlist.currentSource(), QUrl{});
        QCOMPARE(indexChanges.count(), 1);
        QCOMPARE(sourceChanges.count(), 1);
        QCOMPARE(countChanges.count(), 1);
        QCOMPARE(resets.count(), 1);
        playlist.clear();
        QCOMPARE(indexChanges.count(), 1);
        QCOMPARE(sourceChanges.count(), 1);
        QCOMPARE(countChanges.count(), 1);
        QCOMPARE(resets.count(), 1);
    }

    void playlistSortPreservesDuplicateIdentity()
    {
        PlaylistModel playlist;
        QAbstractItemModelTester modelTester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QUrl betaFirst("https://first.example/Beta.wav");
        const QUrl alpha("https://example.com/alpha.wav");
        const QUrl betaSecond("https://second.example/beta.wav");
        const QUrl gamma("https://example.com/gamma.wav");
        QCOMPARE(playlist.appendUrls({betaFirst, gamma, betaSecond, alpha}), 4);
        playlist.setCurrentIndex(2);
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);
        QSignalSpy countChanges(&playlist, &PlaylistModel::countChanged);
        QCOMPARE(playlist.sortByTitle(true, 0), 1);
        QCOMPARE(playlist.currentIndex(), 2);
        QCOMPARE(playlist.currentSource(), betaSecond);
        const QList<QUrl> ascending{alpha, betaFirst, betaSecond, gamma};
        for (int row = 0; row < ascending.size(); ++row)
            QCOMPARE(playlist.data(playlist.index(row), PlaylistModel::SourceRole).toUrl(),
                     ascending.at(row));
        QCOMPARE(playlist.sortByTitle(false, 1), 1);
        QCOMPARE(playlist.currentIndex(), 2);
        QCOMPARE(playlist.currentSource(), betaSecond);
        const QList<QUrl> descending{gamma, betaFirst, betaSecond, alpha};
        for (int row = 0; row < descending.size(); ++row)
            QCOMPARE(playlist.data(playlist.index(row), PlaylistModel::SourceRole).toUrl(),
                     descending.at(row));
        QCOMPARE(playlist.sortByTitle(true, 99), -1);
        QCOMPARE(playlist.sortByTitle(true), -1);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 0);
        playlist.setCurrentIndex(3);
        QCOMPARE(playlist.currentSource(), gamma);
        sourceChanges.clear();
        QCOMPARE(playlist.sortByTitle(false, 1), 1);
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.currentSource(), gamma);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 0);
        playlist.clear();
        QCOMPARE(playlist.sortByTitle(true, 0), -1);
    }

    void playlistSearchMapsRowsWithoutChangingQueue()
    {
        PlaylistModel playlist;
        PlaylistFilterModel filter;
        QAbstractItemModelTester modelTester(&playlist,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester filterTester(&filter,
                QAbstractItemModelTester::FailureReportingMode::QtTest);
        filter.setPlaylist(&playlist);
        QCOMPARE(filter.playlist(), &playlist);
        const QUrl alpha("https://example.com/Alpha.wav");
        const QUrl beta("https://example.com/beta.wav");
        const QUrl literal("https://example.com/alpha%5B1%5D.wav");
        QCOMPARE(playlist.appendUrls({alpha, beta, literal}), 3);
        playlist.setCurrentIndex(1);
        QSignalSpy sourceChanges(&playlist, &PlaylistModel::currentSourceChanged);
        QSignalSpy countChanges(&playlist, &PlaylistModel::countChanged);
        filter.setSearchText(QStringLiteral("ALPHA"));
        QCOMPARE(filter.rowCount(), 2);
        QCOMPARE(filter.sourceIndex(0), 0);
        QCOMPARE(filter.sourceIndex(1), 2);
        QCOMPARE(filter.filteredIndex(1), -1);
        QCOMPARE(filter.filteredIndex(2), 1);
        QCOMPARE(filter.sourceIndex(-1), -1);
        QCOMPARE(filter.sourceIndex(2), -1);
        QCOMPARE(filter.filteredIndex(-1), -1);
        QCOMPARE(filter.filteredIndex(3), -1);
        filter.setSearchText(QStringLiteral("[1]"));
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.sourceIndex(0), 2);
        filter.setSearchText(QStringLiteral("missing"));
        QCOMPARE(filter.rowCount(), 0);
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(playlist.currentSource(), beta);
        QCOMPARE(playlist.count(), 3);
        QCOMPARE(sourceChanges.count(), 0);
        QCOMPARE(countChanges.count(), 0);
        filter.setSearchText({});
        QCOMPARE(filter.rowCount(), 3);
        QVERIFY(playlist.moveTrack(2, 0));
        QCOMPARE(filter.sourceIndex(0), 0);
        QCOMPARE(filter.data(filter.index(0, 0), PlaylistModel::SourceRole).toUrl(), literal);
        filter.setSearchText(QStringLiteral("alpha"));
        QVERIFY(playlist.removeAt(1));
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.sourceIndex(0), 0);
        playlist.clear();
        QCOMPARE(filter.rowCount(), 0);
        filter.setPlaylist(nullptr);
        QCOMPARE(filter.playlist(), nullptr);
        QCOMPARE(filter.sourceIndex(0), -1);
    }

    void dropFilesIntoPlaylist()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first media.y4m"));
        const QUrl second = makeVideo(directory.filePath(QString::fromUtf8("第二集.Y4M")));
        const QUrl audio = makeAudio(directory.filePath("audio.wav"));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = window->findChild<QQuickItem *>("playlistPanel");
        auto *playlist = window->findChild<PlaylistModel *>();
        QVERIFY(panel && playlist);
        const QPoint point = panel->mapToScene({panel->width() / 2, panel->height() / 2}).toPoint();
        QMimeData files;
        files.setUrls({first, audio, second, QUrl::fromLocalFile(directory.path())});
        QDragEnterEvent enter(point, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(point, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(playlist->count(), 2);
        QCOMPARE(playlist->currentSource(), first);
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(video && video->stopped());

        QMimeData text;
        text.setText("not a file");
        QDragEnterEvent invalid(point, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &invalid);
        QVERIFY(!invalid.isAccepted());
        QCOMPARE(playlist->count(), 2);
        QVERIFY2(warnings.isEmpty(), "Video file drops generated QML warnings");
    }

    void playlistRejectsNonVideoDrops()
    {
        QTemporaryDir directory;
        const QUrl audio = makeAudio(directory.filePath("audio.wav"));
        const QUrl supported = makeVideo(directory.filePath("episode.y4m"));
        QList<QUrl> rejected{audio, QUrl::fromLocalFile(directory.filePath("missing.mp4")),
                             QUrl("https://example.com/audio.wav"),
                             QUrl("https://example.com/image.png"),
                             QUrl("https:/missing-host.mp4"),
                             QUrl("ftp://example.com/video.mp4")};
        for (const QString &name : {QStringLiteral("image.png"), QStringLiteral("notes.txt"),
                                    QStringLiteral("source.ts")}) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("export const text = 'not a video';\n");
            file.close();
            rejected.append(QUrl::fromLocalFile(file.fileName()));
        }
        QVERIFY(QDir(directory.path()).mkdir("empty"));
        rejected.append(QUrl::fromLocalFile(directory.filePath("empty")));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *playlists = window->findChild<PlaylistCollection *>();
        QVERIFY(playlists);
        auto *playlist = playlists->currentPlaylist();
        for (const char *name : {"trackList", "playlistTab0", "playlistTabs"}) {
            auto *target = findItem(window->contentItem(), QString::fromLatin1(name));
            QVERIFY2(target, name);
            // Use the unused tab-strip space as well as an actual tab.
            const qreal x = QByteArray(name) == "playlistTabs"
                ? target->width() - 2 : target->width() / 2;
            const QPoint point = target->mapToScene({x, target->height() / 2}).toPoint();
            for (const QUrl &url : std::as_const(rejected)) {
                QMimeData files;
                files.setUrls({url});
                QDragEnterEvent enter(point, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &enter);
                QVERIFY2(!enter.isAccepted(), qPrintable(url.toString()));
                QDropEvent drop(point, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &drop);
                QVERIFY2(!drop.isAccepted(), qPrintable(url.toString()));
                QCOMPARE(playlist->count(), 0);
                QCOMPARE(playlists->count(), 1);
            }

            // Revalidate at the drop itself if the MIME payload changes after entry.
            QMimeData changed;
            changed.setUrls({supported});
            QDragEnterEvent enter(point, Qt::CopyAction, &changed, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            QVERIFY(enter.isAccepted());
            changed.setUrls({audio});
            QDropEvent drop(point, Qt::CopyAction, &changed, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &drop);
            QVERIFY(!drop.isAccepted());
            QCOMPARE(playlist->count(), 0);
            QCOMPARE(playlists->count(), 1);
        }
        QVERIFY2(warnings.isEmpty(), "Rejected playlist drops generated QML warnings");
    }

    void playlistAcceptsFilesInOrder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl first = makeAudio(directory.filePath("first media.wav"));
        const QUrl second = makeAudio(directory.filePath(QString::fromUtf8("第二首.wav")));
        PlaylistModel playlist;
        QCOMPARE(playlist.currentIndex(), -1);
        QCOMPARE(playlist.appendUrls({first, second,
                    QUrl::fromLocalFile(directory.path()),
                    QUrl::fromLocalFile(directory.filePath("missing.wav")),
                    QUrl("https://example.com/third%20track.flac"),
                    QUrl("ftp://example.com/unsupported.wav")}), 3);
        QCOMPARE(playlist.count(), 3);
        QCOMPARE(playlist.currentSource(), first);
        QCOMPARE(playlist.data(playlist.index(0), PlaylistModel::TitleRole).toString(),
                 QStringLiteral("first media.wav"));
        QCOMPARE(playlist.data(playlist.index(1), PlaylistModel::TitleRole).toString(),
                 QString::fromUtf8("第二首.wav"));
        QCOMPARE(playlist.data(playlist.index(2), PlaylistModel::TitleRole).toString(),
                 QStringLiteral("third track.flac"));
        playlist.setCurrentIndex(1);
        playlist.setCurrentIndex(-1);
        playlist.setCurrentIndex(99);
        QCOMPARE(playlist.currentSource(), second);
        QCOMPARE(playlist.appendUrls({first}), 1);
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(playlist.appendUrls({QUrl{}, QUrl("not-a-file"),
                                     QUrl("https:/missing-host.mp4")}), 0);
    }

    void playbackResumesPerFileAndAcrossRestarts()
    {
        QTemporaryDir directory;
        const QUrl first = makeAudio(directory.filePath("resume.wav"));
        const QUrl second = makeAudio(directory.filePath("other.wav"));
        const auto load = [](QQmlApplicationEngine &engine, const QUrl &source) {
            QQmlComponent component(&engine);
            component.setData(R"(
                import QtQuick
                import KPlayer
                Window {
                    width: 640; height: 360; visible: true
                    property url mediaSource
                    MpvVideo { anchors.fill: parent; persistent: true; source: mediaSource }
                }
            )", QUrl());
            return qobject_cast<QQuickWindow *>(component.createWithInitialProperties({{"mediaSource", source}}));
        };
        {
            QQmlApplicationEngine engine;
            QScopedPointer<QQuickWindow> root(load(engine, first));
            auto *window = root.data();
            QVERIFY(window && exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->seekable());
            video->togglePause();
            video->seek(8);
            QTRY_VERIFY(qAbs(video->position() - 8) < 0.1);
            video->stop();
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->position() >= 8);
            video->togglePause();
            video->setSource(second);
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->seekable());
            QVERIFY(video->position() < 2);
            video->togglePause();
            video->seek(4);
            QTRY_VERIFY(qAbs(video->position() - 4) < 0.1);
            video->setSource(first);
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->position() >= 8);
            video->togglePause();
            video->seek(12);
            QTRY_VERIFY(qAbs(video->position() - 12) < 0.1);
            // Destruction saves even if the player was paused rather than stopped.
        }
        {
            QQmlApplicationEngine engine;
            QScopedPointer<QQuickWindow> root(load(engine, first));
            auto *window = root.data();
            QVERIFY(window && exposeWindow(window));
            auto *video = window->findChild<MpvVideo *>();
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->seekable());
            // Loaded/seekable can arrive before mpv's resumed time-pos event.
            QTRY_VERIFY_WITH_TIMEOUT(video->position() >= 12 && video->position() < 14, 1000);
            video->setSource(second);
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->seekable());
            QTRY_VERIFY_WITH_TIMEOUT(video->position() >= 4 && video->position() < 6, 1000);
            QTRY_VERIFY(video->playing());
            video->seek(29.5);
            QTRY_VERIFY_WITH_TIMEOUT(video->stopped(), 5000);
            video->play();
            QTRY_VERIFY(video->mediaLoaded() && video->seekable());
            QVERIFY(video->position() < 2);
        }
    }

    void playlistTabsPreservePlayback()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("original.y4m"));
        const QUrl second = makeVideo(directory.filePath("next.y4m"));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *queues = window->findChild<PlaylistCollection *>();
        auto *original = queues->currentPlaylist();
        original->appendUrls({second});
        queues->addPlaylist("Browsing");
        queues->currentPlaylist()->appendUrls({second});
        queues->setCurrentIndex(0);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->seek(6);
        QTRY_VERIFY(video->position() >= 6);
        const quint64 revision = video->loadRevision();
        click(window, findItem(window->contentItem(), "playlistTab1"));
        QTRY_COMPARE(queues->currentIndex(), 1);
        QCOMPARE(video->source(), first);
        QCOMPARE(video->loadRevision(), revision);
        QVERIFY(video->playing());
        QVERIFY(video->position() >= 6);
        QTRY_VERIFY(findItem(window->contentItem(), "track0"));
        click(window, findItem(window->contentItem(), "track0"));
        QCOMPARE(video->source(), first);
        QVERIFY(video->playing());
        QVERIFY(QMetaObject::invokeMethod(window, "skipTrack", Q_ARG(int, 1)));
        QTRY_COMPARE(video->source(), second);
        QCOMPARE(original->currentIndex(), 1);
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        // Explicit activation switches the playing queue to the browsed tab.
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->property("playingPlaylist").value<PlaylistModel *>(), queues->currentPlaylist());
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        QVERIFY(queues->removePlaylist(1));
        QTRY_COMPARE(queues->count(), 1);
        QTest::qWait(100);
        QVERIFY2(warnings.isEmpty(), "Playlist browsing generated QML warnings");
    }

    void playlistHorizontalScrollKeepsSelection()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *queue = window->findChild<PlaylistModel *>();
        queue->appendUrls({QUrl("https://example.com/" + QString(180, 'W') + ".mkv"),
                           QUrl("https://example.com/short.mkv")});
        auto *list = findItem(window->contentItem(), "trackList");
        auto *scroll = findItem(list, "playlistHorizontalScrollBar");
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        QVERIFY(list && scroll && panel);
        QTRY_VERIFY(list->property("contentWidth").toReal() > list->width() + 500);
        QTRY_VERIFY(scroll->isVisible());
        const QPoint start = scroll->mapToScene({20, scroll->height() / 2}).toPoint();
        const QPoint end = scroll->mapToScene({scroll->width() - 30, scroll->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, end, 100);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_VERIFY(list->property("contentX").toReal() > 100);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, list->mapToScene({100, 63}).toPoint());
        QCOMPARE(panel->property("selectedSourceIndex").toInt(), 1);
        QCOMPARE(queue->currentIndex(), 0);
        auto *search = findItem(window->contentItem(), "playlistSearch");
        search->setProperty("text", "short");
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_COMPARE(list->property("contentWidth").toReal(), list->width());
        QTRY_VERIFY(!scroll->isVisible());
    }

    void playlistShiftWheelScrollsHorizontally()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *queue = window->findChild<PlaylistModel *>();
        QList<QUrl> tracks{QUrl("https://example.com/" + QString(180, 'W') + ".mkv")};
        for (int i = 0; i < 40; ++i)
            tracks.append(QUrl(QStringLiteral("https://example.com/short%1.mkv").arg(i)));
        queue->appendUrls(tracks);
        auto *list = findItem(window->contentItem(), "trackList");
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        QVERIFY(list && panel);
        QTRY_VERIFY(list->property("contentWidth").toReal() > list->width() + 500);
        QTRY_VERIFY(list->property("contentHeight").toReal() > list->height());
        const auto wheelAt = [window, list](QPoint angle, QPoint pixels,
                                          Qt::KeyboardModifiers modifiers) {
            const QPoint local = list->mapToScene({100, 60}).toPoint();
            QTest::mouseMove(window, local);
            QWheelEvent event(local, window->mapToGlobal(local), pixels, angle,
                              Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &event);
        };
        wheelAt({0, -120}, {}, Qt::NoModifier);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);
        QTRY_VERIFY(!list->property("moving").toBool());
        QCOMPARE(list->property("contentX").toReal(), 0.0);
        const qreal verticalPosition = list->property("contentY").toReal();
        const int selected = panel->property("selectedSourceIndex").toInt();
        wheelAt({0, -120}, {}, Qt::ShiftModifier);
        QTRY_VERIFY(list->property("contentX").toReal() > 0);
        QCOMPARE(list->property("contentY").toReal(), verticalPosition);
        wheelAt({0, 120}, {}, Qt::ShiftModifier);
        QTRY_COMPARE(list->property("contentX").toReal(), 0.0);
        // Trackpad pixels and platforms that remap Shift's wheel delta to X.
        wheelAt({}, {0, -45}, Qt::ShiftModifier);
        QTRY_COMPARE(list->property("contentX").toReal(), 45.0);
        wheelAt({-120, 0}, {}, Qt::ShiftModifier);
        QTRY_VERIFY(list->property("contentX").toReal() > 45);
        const qreal maximum = list->property("contentWidth").toReal() - list->width();
        wheelAt({}, {-10000, 0}, Qt::ShiftModifier);
        QTRY_COMPARE(list->property("contentX").toReal(), maximum);
        wheelAt({}, {0, 10000}, Qt::ShiftModifier);
        QTRY_COMPARE(list->property("contentX").toReal(), 0.0);
        QCOMPARE(list->property("contentY").toReal(), verticalPosition);
        QCOMPARE(panel->property("selectedSourceIndex").toInt(), selected);
        QCOMPARE(queue->currentIndex(), 0);
        // With no title overflow, Shift must not fall back to vertical scrolling.
        findItem(window->contentItem(), "playlistSearch")->setProperty("text", "short");
        QTRY_COMPARE(list->property("contentWidth").toReal(), list->width());
        const qreal filteredPosition = list->property("contentY").toReal();
        wheelAt({0, -120}, {}, Qt::ShiftModifier);
        QCOMPARE(list->property("contentX").toReal(), 0.0);
        QCOMPARE(list->property("contentY").toReal(), filteredPosition);

        // The same horizontal scrolling must stay local over fullscreen video.
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(video);
        video->setVolume(37);
        video->setMuted(true);
        QTest::qWait(50);
        QSignalSpy volumeChanges(video, &MpvVideo::volumeChanged);
        QSignalSpy muteChanges(video, &MpvVideo::mutedChanged);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(window->size(), window->screen()->size());
        window->setProperty("playlistVisible", true);
        findItem(window->contentItem(), "playlistSearch")->setProperty("text", "");
        QTRY_VERIFY(list->property("contentWidth").toReal() > list->width() + 500);
        QSignalSpy renderedFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!renderedFrames.isEmpty());
        wheelAt({0, -120}, {}, Qt::ShiftModifier);
        QTRY_VERIFY(list->property("contentX").toReal() > 0);
        wheelAt({}, {0, -45}, Qt::ShiftModifier);
        wheelAt({0, 120}, {}, Qt::ShiftModifier);
        QVERIFY(volumeChanges.isEmpty());
        QVERIFY(muteChanges.isEmpty());
        QCOMPARE(video->volume(), 37.0);
        QVERIFY(video->muted());
    }

    void playlistSearchAndEditingPreservePlayback()
    {
        QTemporaryDir directory;
        const QUrl alpha = makeAudio(directory.filePath("alpha.wav"));
        const QUrl beta = makeAudio(directory.filePath("beta.wav"));
        const QUrl gamma = makeAudio(directory.filePath("gamma.wav"));
        QVERIFY(!alpha.isEmpty() && !beta.isEmpty() && !gamma.isEmpty());
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &errors) { warnings.append(errors); });
        engine.setInitialProperties({{"mediaSource", alpha}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *video = window->findChild<MpvVideo *>();
        auto *panel = item("playlistPanel");
        auto *list = item("trackList");
        auto *search = item("playlistSearch");
        QVERIFY(playlist && video && panel && list && search);
        QCOMPARE(playlist->appendUrls({beta, gamma}), 2);
        click(window, item("playButton"));
        QTRY_VERIFY_WITH_TIMEOUT(video->playing() && video->seekable(), 10000);
        QSignalSpy sourceChanges(video, &MpvVideo::sourceChanged);
        QTRY_VERIFY(item("track2"));
        click(window, item("track2"));
        QCOMPARE(list->property("currentIndex").toInt(), 2);
        QCOMPARE(playlist->currentIndex(), 0);
        const QList<QPair<const char *, int>> moves = {
            {"moveTopButton", 0}, {"moveBottomButton", 2},
            {"moveUpButton", 1}, {"moveDownButton", 2}
        };
        for (const auto &[name, selectedRow] : moves) {
            auto *button = item(name);
            QVERIFY(button && button->isEnabled());
            click(window, button);
            QTRY_COMPARE(list->property("currentIndex").toInt(), selectedRow);
            QCOMPARE(playlist->data(playlist->index(selectedRow),
                                   PlaylistModel::SourceRole).toUrl(), gamma);
            QCOMPARE(video->source(), alpha);
            QVERIFY(video->playing());
        }
        QCOMPARE(sourceChanges.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(panel, "sortTracks", Q_ARG(bool, false)));
        QTRY_COMPARE(list->property("currentIndex").toInt(), 0);
        QCOMPARE(playlist->currentIndex(), 2);
        QCOMPARE(video->source(), alpha);
        QVERIFY(video->playing());
        QCOMPARE(sourceChanges.count(), 0);

        // Filtering and selecting a result never changes the active media.
        QVERIFY(search->setProperty("text", QStringLiteral("BETA")));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(item("track0"));
        click(window, item("track0"));
        QCOMPARE(playlist->currentSource(), alpha);
        QCOMPARE(panel->property("selectedSourceIndex").toInt(), 1);
        QCOMPARE(panel->property("selectedRows").value<QJSValue>().toVariant().toList(), QVariantList({1}));
        QVERIFY(item("removePlaylistButton")->isEnabled());
        click(window, item("removePlaylistButton"));
        QTRY_COMPARE(playlist->count(), 2);
        QTRY_COMPARE(list->property("count").toInt(), 0);
        QCOMPARE(playlist->currentIndex(), 1);
        QCOMPARE(video->source(), alpha);
        QVERIFY(video->playing());
        QCOMPARE(sourceChanges.count(), 0);

        // Enter on a filtered result resolves back to the correct queue row.
        QVERIFY(search->setProperty("text", QStringLiteral("gamma")));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(item("track0"));
        click(window, item("track0"));
        QTRY_COMPARE(list->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(list->hasActiveFocus());
        QCOMPARE(panel->property("selectedSourceIndex").toInt(), 0);
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(playlist->currentIndex(), 0);
        QTRY_COMPARE(video->source(), gamma);
        QTRY_VERIFY_WITH_TIMEOUT(video->playing() && video->seekable(), 10000);
        QCOMPARE(sourceChanges.count(), 1);
        click(window, item("removePlaylistButton"));
        QTRY_COMPARE(playlist->count(), 1);
        QTRY_COMPARE(video->source(), alpha);
        QCOMPARE(playlist->currentIndex(), 0);
        QCOMPARE(sourceChanges.count(), 2);
        QVERIFY(video->stopped());
        QVERIFY(search->setProperty("text", QString{}));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QCOMPARE(playlist->currentSource(), alpha);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
        video->stop();
    }

    void playlistDividerCursorWorksOutsideFullscreen()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setProperty("alwaysOnTop", true);
        window->setPosition(window->screen()->availableGeometry().topLeft() + QPoint(24, 24));
        QVERIFY(exposeWindow(window));
        auto *handle = findItem(window->contentItem(), "playlistResizeHandle");
        QVERIFY(handle);
        for (const auto state : {QWindow::Windowed, QWindow::Maximized}) {
            if (state == QWindow::Maximized)
                window->showMaximized();
            QTRY_COMPARE(window->visibility(), state);
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            QVERIFY(handle->isVisible());
            auto hoverDivider = [window, handle] {
                const QPoint point = handle->mapToScene(
                        {handle->width() / 2, handle->height() / 2}).toPoint();
                QEnterEvent enter(point, point, window->mapToGlobal(point));
                QCoreApplication::sendEvent(window, &enter);
                QTest::mouseMove(window, QPoint(100, 100));
                QTest::mouseMove(window, point);
                return handle->property("containsMouse").toBool()
                        && window->cursor().shape() == Qt::SplitHCursor;
            };
            QTRY_VERIFY(hoverDivider());
            // Moving away must restore the normal cursor.
            QTest::mouseMove(window, QPoint(100, 100));
            QTRY_COMPARE(window->cursor().shape(), Qt::ArrowCursor);
        }
        window->setProperty("playlistVisible", false);
        QTRY_VERIFY(!handle->isVisible());
    }

    void playlistRangeDragAndResize()
    {
        QTemporaryDir directory;
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) { return findItem(window->contentItem(), name); };
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *panel = item("playlistPanel");
        QVERIFY(playlist && panel);
        QList<QUrl> urls;
        for (int i = 0; i < 5; ++i)
            urls.append(makeAudio(directory.filePath(QString::number(i) + ".wav")));
        playlist->appendUrls(urls);
        QSignalSpy sourceChanges(playlist, &PlaylistModel::currentSourceChanged);
        QTRY_VERIFY(item("track4"));
        click(window, item("track1"));
        const auto center = [](QQuickItem *row) {
            return row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        };
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, center(item("track2")));
        QCOMPARE(panel->property("selectedRows").value<QJSValue>().toVariant().toList().size(), 2);
        const QPoint from = center(item("track1"));
        const QPoint to = center(item("track4")) + QPoint(0, 15);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, to, 50);
        QTRY_VERIFY(panel->property("draggingTracks").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_COMPARE(playlist->data(playlist->index(3), PlaylistModel::SourceRole).toUrl(), urls[1]);
        QCOMPARE(playlist->data(playlist->index(4), PlaylistModel::SourceRole).toUrl(), urls[2]);
        QCOMPARE(sourceChanges.count(), 0);
        QTRY_VERIFY(item("trackNumber4"));
        QCOMPARE(item("trackNumber4")->property("text").toString(), QStringLiteral("5."));
        // Shift+Up extends a keyboard range, while Delete removes the entire selection.
        click(window, item("track4"));
        QTest::keyClick(window, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_COMPARE(playlist->count(), 3);
        QCOMPARE(playlist->currentSource(), urls[0]);
        auto *handle = item("playlistResizeHandle");
        QVERIFY(handle);
        const qreal originalWidth = panel->width();
        const QPoint handlePoint = center(handle);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, handlePoint);
        QTest::mouseMove(window, handlePoint + QPoint(70, 0), 50);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, handlePoint + QPoint(70, 0));
        QTRY_COMPARE(panel->width(), originalWidth - 70);
        click(window, item("playbackMenuButton"));
        click(window, item("playbackMenuButton"));
        QCOMPARE(panel->width(), originalWidth - 70);
        // Restore the saved width for other layout tests.
        const QPoint restorePoint = center(handle);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, restorePoint);
        QTest::mouseMove(window, restorePoint - QPoint(70, 0), 50);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, restorePoint - QPoint(70, 0));
        // Scrolling must not destroy the pointer grab when the pressed row is recycled.
        QList<QUrl> more;
        for (int i = 0; i < 40; ++i)
            more.append(QUrl(QStringLiteral("https://example.com/%1.mp4").arg(i)));
        playlist->appendUrls(more);
        auto *list = item("trackList");
        QTRY_VERIFY(item("track0"));
        click(window, item("track0"));
        const QPoint start = center(item("track0"));
        const QPoint edge = list->mapToScene(QPointF(list->width() / 2, list->height() - 5)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, edge, 50);
        QTRY_VERIFY(list->property("contentY").toReal() > 300);
        QVERIFY(panel->property("draggingTracks").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, edge);
        QVERIFY(playlist->indexOfSource(urls[0]) > 3);
        QCOMPARE(playlist->currentSource(), urls[0]);
        QCOMPARE(sourceChanges.count(), 0);
    }

    void playlistTogglePreservesQueueAndLayout()
    {
        QTemporaryDir directory;
        const QUrl first = makeAudio(directory.filePath("first.wav"));
        const QUrl second = makeAudio(directory.filePath("second.wav"));
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = findItem(window->contentItem(), QStringLiteral("playlistPanel"));
        auto *button = findItem(window->contentItem(), QStringLiteral("playbackMenuButton"));
        auto *stage = findItem(window->contentItem(), QStringLiteral("videoStage"));
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(panel && button && stage && playlist && video);
        QCOMPARE(playlist->appendUrls({second}), 1);
        playlist->setCurrentIndex(1);
        QTRY_COMPARE(video->source(), second);
        QVERIFY(window->property("playlistVisible").toBool());
        QVERIFY(panel->isVisible());
        const qreal expandedPlaylistWidth = panel->width();
        const qreal initialVideoWidth = stage->width();
        const QSize initialWindowSize = window->size();

        click(window, button);
        QTRY_VERIFY(!window->property("playlistVisible").toBool());
        QTRY_VERIFY(!panel->isVisible());
        QTRY_VERIFY(stage->width() >= initialVideoWidth + expandedPlaylistWidth);
        QCOMPARE(window->size(), initialWindowSize);
        QCOMPARE(playlist->count(), 2);
        QCOMPARE(playlist->currentIndex(), 1);
        QCOMPARE(playlist->currentSource(), second);
        QVERIFY(video->stopped());

        click(window, button);
        QTRY_VERIFY(window->property("playlistVisible").toBool());
        QTRY_VERIFY(panel->isVisible());
        QTRY_COMPARE(stage->width(), initialVideoWidth);
        QCOMPARE(playlist->count(), 2);
        QCOMPARE(playlist->currentIndex(), 1);
        QCOMPARE(video->source(), second);
    }

    void applicationMenuOpensFromTitleAndVideo()
    {
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &errors) { warnings.append(errors); });
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *menu = window->findChild<QObject *>("appMenu");
        auto *openSubmenu = window->findChild<QObject *>("openSubmenu");
        auto *titleButton = item("appMenuButton");
        auto *stage = item("videoStage");
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(menu && openSubmenu && titleButton && stage && video);

        click(window, titleButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("width").toReal(), std::min(420.0, window->width() - 16.0));
        auto *openMedia = item("openMediaMenuItem");
        auto *closeMedia = item("closeMediaMenuItem");
        auto *playlistItem = item("playlistMenuItem");
        QVERIFY(openMedia && closeMedia && playlistItem);
        QCOMPARE(openMedia->property("text").toString(), QString::fromUtf8("開啟檔案…"));
        QCOMPARE(closeMedia->property("text").toString(), QString::fromUtf8("關閉檔案"));
        QCOMPARE(openMedia->height(), 22.0);
        QVERIFY(playlistItem->property("checked").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());

        const QString capturePath = qEnvironmentVariable("KPLAYER_TEST_CAPTURE");
        if (!capturePath.isEmpty()) {
            QSignalSpy renderedFrames(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY_WITH_TIMEOUT(!renderedFrames.isEmpty(), 5000);
            QVERIFY(window->grabWindow().save(capturePath));
        }

        click(window, playlistItem);
        QTRY_VERIFY(!window->property("playlistVisible").toBool());
        QTRY_VERIFY(!menu->property("visible").toBool());
        click(window, titleButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(!playlistItem->property("checked").toBool());
        click(window, playlistItem);
        QTRY_VERIFY(window->property("playlistVisible").toBool());
        QTRY_VERIFY(!menu->property("visible").toBool());
        click(window, titleButton);
        QTRY_VERIFY(menu->property("opened").toBool());

        auto *openEntry = menuItemAt(menu, 1);
        QVERIFY(openEntry && openEntry->isVisible() && openEntry->isEnabled());
        click(window, openEntry);
        QTRY_VERIFY(openSubmenu->property("opened").toBool());
        QTRY_VERIFY(item("clearPlaylistMenuItem") && item("clearPlaylistMenuItem")->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!openSubmenu->property("visible").toBool());
        QTRY_VERIFY(menu->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());

        click(window, titleButton, Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            stage->mapToScene(QPointF(stage->width() - 8, stage->height() - 8)).toPoint());
        QTRY_VERIFY(!menu->property("visible").toBool());

        QSignalSpy playRequests(stage, SIGNAL(playPauseRequested()));
        QSignalSpy fullscreenRequests(stage, SIGNAL(fullscreenRequested()));
        QVERIFY(playRequests.isValid() && fullscreenRequests.isValid());
        for (const bool minimumSize : {false, true}) {
            if (minimumSize) {
                QSignalSpy resizedFrames(window, &QQuickWindow::frameSwapped);
                window->resize(window->minimumSize());
                QTRY_COMPARE(window->size(), window->minimumSize());
                window->update();
                QTRY_VERIFY_WITH_TIMEOUT(!resizedFrames.isEmpty(), 5000);
            }
            const QPoint point = stage->mapToScene(
                    {stage->width() - 10, stage->height() - 10}).toPoint();
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
            QTRY_VERIFY(menu->property("opened").toBool());
            const qreal menuX = menu->property("x").toReal();
            const qreal menuY = menu->property("y").toReal();
            const qreal menuWidth = menu->property("width").toReal();
            const qreal menuHeight = menu->property("height").toReal();
            QVERIFY(menuX >= 0 && menuY >= 0);
            QVERIFY(menuX + menuWidth <= window->width());
            QVERIFY(menuY + menuHeight <= window->height());
            QCOMPARE(playRequests.count(), 0);
            QCOMPARE(fullscreenRequests.count(), 0);
            QVERIFY(video->stopped());
            if (minimumSize && !capturePath.isEmpty()) {
                QSignalSpy renderedFrames(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY_WITH_TIMEOUT(!renderedFrames.isEmpty(), 5000);
                QVERIFY(window->grabWindow().save(capturePath + ".minimum.png"));
            }
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
    }

    void middleClickFullscreenIsImmediate()
    {
        // Isolate input from native window resizing so a double click stays at
        // the same coordinates and the test measures dispatch, not the driver.
        QQmlApplicationEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import KPlayer
            Window {
                width: 640; height: 360; visible: true
                VideoStage { anchors.fill: parent }
            }
        )", QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(exposeWindow(window));
        auto *stage = findItem(window->contentItem(), "videoStage");
        QVERIFY(stage);
        QSignalSpy fullscreenRequests(stage, SIGNAL(fullscreenRequested()));
        QSignalSpy playRequests(stage, SIGNAL(playPauseRequested()));
        QVERIFY(fullscreenRequests.isValid() && playRequests.isValid());
        const QPoint point(320, 180);

        QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, point, 10);
        // No event-loop wait: the request must arrive on the first release.
        QCOMPARE(fullscreenRequests.count(), 1);
        QCOMPARE(playRequests.count(), 0);
        QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, point, 10);
        QCOMPARE(fullscreenRequests.count(), 1);
        QCOMPARE(playRequests.count(), 0);

        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(playRequests.count(), 1);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QCOMPARE(fullscreenRequests.count(), 1);
        QCOMPARE(playRequests.count(), 1);

        QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, point);
        QCOMPARE(fullscreenRequests.count(), 2);
    }

    void openMediaShortcutsOpenDialog()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *dialog = window->findChild<QObject *>("mediaDialog");
        QVERIFY(dialog);
        QTest::keyClick(window, Qt::Key_F3);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(QGuiApplication::modalWindow() == nullptr);
        QVERIFY(exposeWindow(window));
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(QGuiApplication::modalWindow() == nullptr);
    }

    void numpadShortcutsResizeVideoFrame()
    {
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("frame-size.y4m"), true);
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *stage = findItem(window->contentItem(), "videoStage");
        auto *search = findItem(window->contentItem(), "playlistSearch");
        auto *settings = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(video && stage && search && settings);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        QTRY_VERIFY(!video->playing());
        QTRY_VERIFY(previewBrightness(window, video) > 160);
        click(window, stage);
        const QSize windowSize = window->size();
        auto press = [window](Qt::Key key) {
            QTest::keyClick(window, key, Qt::KeypadModifier);
        };
        const QList<Qt::Key> keys{Qt::Key_4, Qt::Key_6, Qt::Key_8, Qt::Key_2,
                                  Qt::Key_9, Qt::Key_1, Qt::Key_5};
        const QList<QSizeF> sizes{{0.98, 1.0}, {1.0, 1.0}, {1.0, 1.02}, {1.0, 1.0},
                                  {1.02, 1.02}, {1.0, 1.0}, {1.0, 1.0}};
        for (qsizetype i = 0; i < keys.size(); ++i) {
            press(keys[i]);
            QTRY_COMPARE(video->frameWidthScale(), sizes[i].width());
            QTRY_COMPARE(video->frameHeightScale(), sizes[i].height());
        }
        // Linux reports navigation keys from the keypad with Num Lock off.
        const QList<Qt::Key> navigationKeys{Qt::Key_Left, Qt::Key_Right, Qt::Key_Up,
                                            Qt::Key_Down, Qt::Key_PageUp, Qt::Key_End,
                                            Qt::Key_Clear};
        for (qsizetype i = 0; i < navigationKeys.size(); ++i) {
            press(navigationKeys[i]);
            QTRY_COMPARE(video->frameWidthScale(), sizes[i].width());
            QTRY_COMPARE(video->frameHeightScale(), sizes[i].height());
        }
        press(Qt::Key_9);
        press(Qt::Key_5);
        QTRY_COMPARE(video->frameWidthScale(), 1.0);
        QTRY_COMPARE(video->frameHeightScale(), 1.0);

        // Inspect the rendered picture, not just the exposed scale properties.
        auto pictureSize = [window, video]() {
            const QImage image = window->grabWindow();
            if (image.isNull())
                return QSize{};
            const qreal sx = qreal(image.width()) / window->width();
            const qreal sy = qreal(image.height()) / window->height();
            const QPointF origin = video->mapToScene({0, 0});
            const QRect area(qRound(origin.x() * sx), qRound(origin.y() * sy),
                             qRound(video->width() * sx), qRound(video->height() * sy));
            if (!image.rect().contains(area))
                return QSize{};
            int width = 0, height = 0;
            for (int x = area.left(); x <= area.right(); ++x)
                width += qGray(image.pixel(x, area.center().y())) > 140;
            for (int y = area.top(); y <= area.bottom(); ++y)
                height += qGray(image.pixel(area.center().x(), y)) > 140;
            return QSize(width, height);
        };
        const QSize originalPicture = pictureSize();
        QVERIFY(originalPicture.width() > 100 && originalPicture.height() > 100);
        for (int i = 0; i < 10; ++i)
            press(Qt::Key_4);
        QTRY_COMPARE(video->frameWidthScale(), 0.8);
        QTRY_VERIFY(qAbs(pictureSize().width() - originalPicture.width() * 0.8) < 3);
        QTRY_VERIFY(qAbs(pictureSize().height() - originalPicture.height()) < 3);
        for (int i = 0; i < 10; ++i)
            press(Qt::Key_2);
        QTRY_COMPARE(video->frameHeightScale(), 0.8);
        QTRY_VERIFY(qAbs(pictureSize().height() - originalPicture.height() * 0.8) < 3);
        QCOMPARE(window->size(), windowSize);
        press(Qt::Key_5);
        QTRY_COMPARE(video->frameWidthScale(), 1.0);
        QTRY_COMPARE(video->frameHeightScale(), 1.0);
        QTRY_VERIFY(qAbs(pictureSize().width() - originalPicture.width()) < 3);
        QTRY_VERIFY(qAbs(pictureSize().height() - originalPicture.height()) < 3);

        // Top-row digits and focused text fields retain their normal behavior.
        for (Qt::Key key : keys)
            QTest::keyClick(window, key);
        QCOMPARE(video->frameWidthScale(), 1.0);
        QCOMPARE(video->frameHeightScale(), 1.0);
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(search->hasActiveFocus());
        for (Qt::Key key : keys)
            press(key);
        QCOMPARE(search->property("text").toString(), QStringLiteral("4682915"));
        QCOMPARE(video->frameWidthScale(), 1.0);
        QCOMPARE(video->frameHeightScale(), 1.0);
        click(window, stage);
        QTest::keyClick(window, Qt::Key_F5);
        QTRY_VERIFY(settings->property("opened").toBool());
        press(Qt::Key_9);
        QCOMPARE(video->frameWidthScale(), 1.0);
        QCOMPARE(video->frameHeightScale(), 1.0);
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        click(window, stage);
        window->showFullScreen();
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        press(Qt::Key_9);
        QTRY_COMPARE(video->frameWidthScale(), 1.02);
        QTRY_COMPARE(video->frameHeightScale(), 1.02);
        press(Qt::Key_5);
        QTRY_COMPARE(video->frameWidthScale(), 1.0);
        QTRY_COMPARE(video->frameHeightScale(), 1.0);

        video->adjustFrameSize(-1000, 1000);
        QCOMPARE(video->frameWidthScale(), 0.1);
        QCOMPARE(video->frameHeightScale(), 4.0);
        video->setAspectRatio("4:3");
        video->setBrightness(10);
        video->resetFrameSize();
        QCOMPARE(video->frameWidthScale(), 1.0);
        QCOMPARE(video->frameHeightScale(), 1.0);
        QCOMPARE(video->aspectRatio(), QStringLiteral("4:3"));
        QCOMPARE(video->brightness(), 10);
        QCOMPARE(video->errorString(), QString{});
    }

    void trackNavigationAfterSeeking_data()
    {
        QTest::addColumn<bool>("fullscreen");
        QTest::addColumn<bool>("drag");
        QTest::newRow("window-click") << false << false;
        QTest::newRow("window-drag") << false << true;
        QTest::newRow("fullscreen-click") << true << false;
        QTest::newRow("fullscreen-drag") << true << true;
    }

    void trackNavigationAfterSeeking()
    {
        QFETCH(bool, fullscreen);
        QFETCH(bool, drag);
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *seek = findItem(window->contentItem(), "positionSlider");
        QVERIFY(video && playlist && seek);
        QCOMPARE(playlist->appendUrls({second}), 1);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        if (fullscreen) {
            QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
            QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
            QTRY_COMPARE(window->size(), window->screen()->size());
            QTest::mouseMove(window, {window->width() / 2, window->height() / 2}, 20);
            QTest::mouseMove(window, {window->width() / 2, window->height() - 2}, 20);
            QTRY_VERIFY(seek->isVisible());
        }
        click(window, seek);
        if (drag) {
            const QPoint start = seek->mapToScene({seek->width() / 2, seek->height() / 2}).toPoint();
            const QPoint end = seek->mapToScene({seek->width() * 0.75, seek->height() / 2}).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, end, 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        }
        QTRY_VERIFY(seek->hasActiveFocus());
        QTRY_VERIFY(qAbs(video->position() - (drag ? 23 : 15)) < 1);
        const double position = video->position();
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(qAbs(video->position() - (position - 1)) < 0.2);
        QTest::keyClick(window, Qt::Key_PageDown);
        QTRY_COMPARE(video->source(), second);
        QCOMPARE(playlist->currentIndex(), 1);
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        click(window, seek);
        QTRY_VERIFY(seek->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_PageUp);
        QTRY_COMPARE(video->source(), first);
        QCOMPARE(playlist->currentIndex(), 0);
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        auto *search = findItem(window->contentItem(), "playlistSearch");
        QTRY_VERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(video->source(), first);
        QCOMPARE(playlist->currentIndex(), 0);
    }

    void keyboardShortcutsRespectPlaylistSearch()
    {
        QTemporaryDir directory;
        const QUrl first = makeAudio(directory.filePath("first.wav"));
        const QUrl second = makeAudio(directory.filePath("second.wav"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *video = window->findChild<MpvVideo *>();
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *search = item("playlistSearch");
        QVERIFY(video && playlist && search);
        QCOMPARE(playlist->appendUrls({second}), 1);
        click(window, item("playButton"));
        QTRY_VERIFY_WITH_TIMEOUT(video->playing() && video->seekable(), 10000);
        click(window, item("pauseButton"));
        QTRY_VERIFY(!video->playing() && !video->stopped());
        video->seek(6);
        QTRY_VERIFY(qAbs(video->position() - 6) < 0.1);
        const double position = video->position();
        const double volume = video->volume();
        const bool muted = video->muted();
        click(window, item("playbackMenuButton"));
        QTRY_VERIFY(!window->property("playlistVisible").toBool());
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(window->property("playlistVisible").toBool());
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QTest::keyClick(window, Qt::Key_M);
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_M);
        QTest::keyClick(window, Qt::Key_Left);
        QTest::keyClick(window, Qt::Key_Right);
        QTest::keyClick(window, Qt::Key_Up);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(search->property("text").toString(), QStringLiteral("m m"));
        QCOMPARE(video->muted(), muted);
        QCOMPARE(video->volume(), volume);
        QVERIFY(qAbs(video->position() - position) < 0.2);
        QVERIFY(!video->playing() && !video->stopped());

        click(window, item("videoStage"));
        QTRY_VERIFY(window->property("playbackShortcutsEnabled").toBool());
        auto *playButton = item("playButton");
        QVERIFY(playButton);
        playButton->forceActiveFocus();
        QTRY_VERIFY(playButton->hasActiveFocus());
        QVERIFY(window->property("playbackShortcutsEnabled").toBool());
        QVERIFY(!window->property("activationShortcutsEnabled").toBool());
        QTest::keyClick(window, Qt::Key_M);
        QTRY_COMPARE(video->muted(), !muted);
        click(window, item("videoStage"));
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(video->playing());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(!video->playing() && !video->stopped());
        QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(!window->property("playlistVisible").toBool());
        QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(window->property("playlistVisible").toBool());
        QTest::keyClick(window, Qt::Key_F6);
        QTRY_VERIFY(!window->property("playlistVisible").toBool());
        QTest::keyClick(window, Qt::Key_F6);
        QTRY_VERIFY(window->property("playlistVisible").toBool());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_X);
        QCOMPARE(search->property("text").toString(), QStringLiteral("x"));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(window->visibility(), QWindow::FullScreen);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(search->property("text").toString(), QString{});
        QCOMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_VERIFY(item("trackList")->hasActiveFocus());

        auto *sortMenu = window->findChild<QObject *>("playlistSortMenu");
        QVERIFY(sortMenu);
        click(window, item("sortPlaylistButton"));
        QTRY_VERIFY(sortMenu->property("opened").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!sortMenu->property("visible").toBool());
        QCOMPARE(window->visibility(), QWindow::FullScreen);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        click(window, item("videoStage"));
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(video->playing());
        QTest::keyClick(window, Qt::Key_F4);
        QTRY_VERIFY(video->stopped());
        QCOMPARE(playlist->count(), 2);
        QCOMPARE(playlist->currentIndex(), 0);
        QCOMPARE(video->source(), first);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(video->playing() && video->seekable(), 10000);
        video->stop();
    }

    void menusAndRepeatModesControlPlayback()
    {
        QTemporaryDir directory;
        const QUrl first = makeAudio(directory.filePath("first.wav"));
        const QUrl second = makeAudio(directory.filePath("second.wav"));
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &errors) { warnings.append(errors); });
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(playlist && video);
        QCOMPARE(playlist->appendUrls({second}), 1);
        QCOMPARE(window->property("repeatMode").toInt(), 0);
        QVERIFY(!window->property("alwaysOnTop").toBool());
        click(window, item("pinButton"));
        QTRY_VERIFY(window->property("alwaysOnTop").toBool());
        QVERIFY(window->flags().testFlag(Qt::WindowStaysOnTopHint));
        click(window, item("pinButton"));
        QTRY_VERIFY(!window->property("alwaysOnTop").toBool());
        QVERIFY(!window->flags().testFlag(Qt::WindowStaysOnTopHint));

        auto *settings = window->findChild<QObject *>("playbackSubmenu");
        auto *appMenu = window->findChild<QObject *>("appMenu");
        QVERIFY(settings && appMenu);
        click(window, item("appMenuButton"));
        QTRY_VERIFY(appMenu->property("opened").toBool());
        click(window, menuItemAt(appMenu, 6));
        QTRY_VERIFY(settings->property("opened").toBool());
        QTRY_VERIFY(item("repeatTrackMenuItem"));
        click(window, item("repeatTrackMenuItem"));
        QTRY_COMPARE(window->property("repeatMode").toInt(), 1);
        QTRY_VERIFY(!settings->property("visible").toBool());
        click(window, item("playButton"));
        QTRY_VERIFY_WITH_TIMEOUT(video->playing() && video->seekable(), 10000);
        QSignalSpy ends(video, &MpvVideo::endOfFile);
        video->seek(29.5);
        QTRY_VERIFY(video->position() > 29);
        QTRY_COMPARE_WITH_TIMEOUT(ends.count(), 1, 5000);
        QTRY_VERIFY(video->playing() && video->seekable() && video->position() < 2);
        QCOMPARE(playlist->currentIndex(), 0);
        QCOMPARE(video->source(), first);

        click(window, item("appMenuButton"));
        QTRY_VERIFY(appMenu->property("opened").toBool());
        click(window, menuItemAt(appMenu, 6));
        QTRY_VERIFY(settings->property("opened").toBool());
        click(window, item("repeatPlaylistMenuItem"));
        QTRY_COMPARE(window->property("repeatMode").toInt(), 2);
        click(window, item("nextButton"), Qt::RightButton);
        QTRY_COMPARE(playlist->currentIndex(), 1);
        QTRY_VERIFY(video->playing() && video->seekable());
        video->seek(29.5);
        QTRY_VERIFY(video->position() > 29);
        QTRY_COMPARE_WITH_TIMEOUT(playlist->currentIndex(), 0, 5000);
        QTRY_VERIFY(video->playing() && video->seekable());
        QCOMPARE(video->source(), first);

        click(window, item("appMenuButton"));
        QTRY_VERIFY(appMenu->property("opened").toBool());
        click(window, menuItemAt(appMenu, 6));
        QTRY_VERIFY(settings->property("opened").toBool());
        click(window, item("repeatOffMenuItem"));
        QTRY_COMPARE(window->property("repeatMode").toInt(), 0);
        click(window, item("nextButton"), Qt::RightButton);
        QTRY_COMPARE(playlist->currentIndex(), 1);
        QTRY_VERIFY(video->playing() && video->seekable());
        video->seek(29.5);
        QTRY_VERIFY_WITH_TIMEOUT(video->stopped(), 5000);
        QCOMPARE(playlist->currentIndex(), 1);

        click(window, item("appMenuButton"));
        QTRY_VERIFY(appMenu->property("opened").toBool());
        auto *openSubmenu = window->findChild<QObject *>("openSubmenu");
        auto *openEntry = menuItemAt(appMenu, 1);
        QVERIFY(openSubmenu && openEntry);
        click(window, openEntry);
        QTRY_VERIFY(openSubmenu->property("opened").toBool());
        QTRY_VERIFY(item("clearPlaylistMenuItem") && item("clearPlaylistMenuItem")->isVisible());
        click(window, item("clearPlaylistMenuItem"));
        QTRY_COMPARE(playlist->count(), 0);
        QCOMPARE(playlist->currentIndex(), -1);
        QCOMPARE(video->source(), QUrl{});
        QVERIFY(video->stopped());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
    }

    void resizeFrameTracksWindowState()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        // Native pointer input must reach this window even if a running player is pinned.
        window->setProperty("alwaysOnTop", true);
        // Repeated test windows cascade beyond the desktop; keep every edge reachable.
        window->setPosition(window->screen()->availableGeometry().topLeft() + QPoint(24, 24));
        QVERIFY(exposeWindow(window));
        auto *frame = findItem(window->contentItem(), QStringLiteral("windowResizeFrame"));
        QVERIFY(frame && frame->isEnabled());
        const QList<QPair<QString, int>> handles = {
            {QStringLiteral("resizeLeft"), Qt::LeftEdge},
            {QStringLiteral("resizeRight"), Qt::RightEdge},
            {QStringLiteral("resizeTop"), Qt::TopEdge},
            {QStringLiteral("resizeBottom"), Qt::BottomEdge},
            {QStringLiteral("resizeTopLeft"), Qt::TopEdge | Qt::LeftEdge},
            {QStringLiteral("resizeTopRight"), Qt::TopEdge | Qt::RightEdge},
            {QStringLiteral("resizeBottomLeft"), Qt::BottomEdge | Qt::LeftEdge},
            {QStringLiteral("resizeBottomRight"), Qt::BottomEdge | Qt::RightEdge}
        };
#ifdef Q_OS_WIN
        const QPoint originalCursorPosition = QCursor::pos();
        const auto restoreCursor = qScopeGuard([originalCursorPosition] {
            QCursor::setPos(originalCursorPosition);
        });
#endif
        for (const auto &[name, edges] : handles) {
            auto *handle = findItem(frame, name);
            QVERIFY2(handle, qPrintable(name));
            QVERIFY2(handle->isEnabled(), qPrintable(name));
            QCOMPARE(handle->property("resizeEdges").toInt(), edges);
            QVERIFY(handle->width() > 0 && handle->height() > 0);
            const QRectF bounds(handle->mapToItem(frame, QPointF{}), handle->size());
            QVERIFY(QRectF(QPointF{}, frame->size()).contains(bounds));
            if (edges & Qt::LeftEdge)
                QCOMPARE(bounds.left(), 0.0);
            if (edges & Qt::RightEdge)
                QCOMPARE(bounds.right(), frame->width());
            if (edges & Qt::TopEdge)
                QCOMPARE(bounds.top(), 0.0);
            if (edges & Qt::BottomEdge)
                QCOMPARE(bounds.bottom(), frame->height());

            const Qt::CursorShape expectedCursor = edges == Qt::LeftEdge || edges == Qt::RightEdge
                    ? Qt::SizeHorCursor : edges == Qt::TopEdge || edges == Qt::BottomEdge
                    ? Qt::SizeVerCursor : edges == (Qt::TopEdge | Qt::LeftEdge)
                        || edges == (Qt::BottomEdge | Qt::RightEdge)
                    ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
#ifdef Q_OS_WIN
            // Synthetic Qt mouse events do not exercise Windows' WM_SETCURSOR.
            QCursor::setPos(window->mapToGlobal(handle->mapToScene(
                    {handle->width() / 2, handle->height() / 2}).toPoint()));
            window->raise();
            const int nativeCursorId = expectedCursor == Qt::SizeHorCursor ? 32644
                    : expectedCursor == Qt::SizeVerCursor ? 32645
                    : expectedCursor == Qt::SizeFDiagCursor ? 32642 : 32643;
            const HCURSOR expectedNativeCursor = static_cast<HCURSOR>(LoadImageW(
                    nullptr, MAKEINTRESOURCEW(nativeCursorId), IMAGE_CURSOR,
                    0, 0, LR_DEFAULTSIZE | LR_SHARED));
            auto nativeCursorMatches = [window, handle, expectedNativeCursor] {
                // Check this thread's cursor immediately after the native request;
                // the global desktop cursor can change while other apps are in use.
                QCursor::setPos(window->mapToGlobal(handle->mapToScene(
                        {handle->width() / 2, handle->height() / 2}).toPoint()));
                SendMessageW(reinterpret_cast<HWND>(window->winId()), WM_SETCURSOR,
                             window->winId(), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
                return GetCursor() == expectedNativeCursor;
            };
            QTRY_VERIFY2(nativeCursorMatches(), qPrintable(name + " at "
                    + QString::number(QCursor::pos().x()) + ","
                    + QString::number(QCursor::pos().y())));
#else
            // Wayland clients cannot reliably warp the desktop pointer. Exercise
            // Qt Quick's resize hover handling with a local mouse event instead.
            QTest::mouseMove(window, handle->mapToScene(
                    {handle->width() / 2, handle->height() / 2}).toPoint());
            QTRY_COMPARE(window->cursor().shape(), expectedCursor);
#endif
        }

        // Geometry bindings follow window resizing and keep the configured limits.
        window->resize(window->width() + 80, window->height() + 60);
        QTRY_COMPARE(frame->width(), qreal(window->width()));
        QTRY_COMPARE(frame->height(), qreal(window->height()));
        QCOMPARE(window->minimumWidth(), 1100);
        QCOMPARE(window->minimumHeight(), 500);
        window->showMaximized();
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QTRY_VERIFY(!frame->isEnabled());
        for (const auto &[name, edges] : handles) {
            Q_UNUSED(edges);
            QVERIFY(!findItem(frame, name)->isEnabled());
        }
        window->showFullScreen();
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_VERIFY(!frame->isEnabled());
        window->showNormal();
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTRY_VERIFY(frame->isEnabled());
    }

    void settingHintsExpireAndRestart()
    {
        QSettings().setValue("interface/language", "en");
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *hint = findItem(window->contentItem(), "playbackHint");
        QVERIFY(video && hint);
        QCOMPARE(hint->property("font").value<QFont>().family(), QGuiApplication::font().family());
        video->setSpeed(1.1);
        QTRY_VERIFY(hint->isVisible());
        QCOMPARE(hint->property("text").toString(), QStringLiteral("1.1x"));
        QTRY_VERIFY_WITH_TIMEOUT(!hint->isVisible(), 3000);
        video->setSpeed(1.0);
        QTRY_VERIFY(hint->isVisible());
        QCOMPARE(hint->property("text").toString(), QStringLiteral("1.0x"));
        video->setVolume(65);
        QCOMPARE(hint->property("text").toString(), QStringLiteral("Volume: 65%"));
        video->setMuted(true);
        QCOMPARE(hint->property("text").toString(), QStringLiteral("Muted"));
        video->setSubtitlesVisible(false);
        QCOMPARE(hint->property("text").toString(), QStringLiteral("Subtitles off"));
        window->setProperty("repeatMode", 2);
        QCOMPARE(hint->property("text").toString(), QStringLiteral("Repeat playlist"));
        QTRY_VERIFY_WITH_TIMEOUT(!hint->isVisible(), 3000);
        QSettings().remove("interface/language");
    }

    void arrowSeekingShowsCurrentTime()
    {
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("seek-hint.y4m"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *stage = findItem(window->contentItem(), "videoStage");
        auto *hint = findItem(stage, "playbackHint");
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        click(window, stage);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(qAbs(video->position() - 10) < 1);
        QTRY_VERIFY(hint->isVisible());
        QTRY_COMPARE(hint->property("text").toString(), QStringLiteral("00:00:10 / 00:00:30"));
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(video->position() < 1);
        QTRY_COMPARE(hint->property("text").toString(), QStringLiteral("00:00:00 / 00:00:30"));
        QTest::keyClick(window, Qt::Key_Right, Qt::ControlModifier);
        QTRY_VERIFY(video->position() >= 29);
        QTRY_VERIFY(hint->property("text").toString().startsWith("00:00:29")
                    || hint->property("text").toString().startsWith("00:00:30"));
    }

    void fullscreenRestoresLayoutAndWindowState()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *title = findItem(window->contentItem(), "titleBar");
        auto *stage = findItem(window->contentItem(), "videoStage");
        QVERIFY(title && stage);
        QVERIFY(title->isVisible());
        const QSize normalSize = window->size();
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_VERIFY(!title->isVisible());
        QTRY_COMPARE(stage->mapToScene(QPointF{}), QPointF(0, 0));
        QTRY_COMPARE(window->size(), window->screen()->size());
        auto *bar = findItem(window->contentItem(), "playbackBar");
        QVERIFY(bar);
        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!bar->isVisible());
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() - 2));
        QTRY_VERIFY(bar->isVisible());
        auto *video = window->findChild<MpvVideo *>();
        QTRY_COMPARE(video->subtitleBottomInset(), bar->height());
        QTRY_COMPARE(stage->size(), QSizeF(window->size()));
        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!bar->isVisible());
        QTRY_COMPARE(video->subtitleBottomInset(), 0.0);
        QTest::mouseMove(window, QPoint(window->width() / 2, 2));
        QTRY_VERIFY(title->isVisible());
        QTRY_COMPARE(stage->size(), QSizeF(window->size()));
        auto *pin = findItem(title, "pinButton");
        const bool pinned = window->property("alwaysOnTop").toBool();
        click(window, pin);
        QTRY_COMPARE(window->property("alwaysOnTop").toBool(), !pinned);
        QVERIFY(title->isVisible());
        click(window, findItem(title, "pinButton"));
        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!title->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTRY_VERIFY(title->isVisible());
        QTRY_COMPARE(window->size(), normalSize);

        window->showMaximized();
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(stage->size(), QSizeF(window->size()));
        QVERIFY(!window->property("playlistVisible").toBool());
        QVERIFY(!window->property("controlsVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QTRY_VERIFY(title->isVisible());
        QVERIFY(window->property("playlistVisible").toBool());
        QVERIFY(window->property("controlsVisible").toBool());
    }

    void fullscreenControlsStayVisibleOverClickableAreas()
    {
        QTemporaryDir directory;
        const QUrl source = makeAudio(directory.filePath("fullscreen.wav"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *bar = findItem(window->contentItem(), "playbackBar");
        QVERIFY(video && bar);
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        video->togglePause();
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(window->size(), window->screen()->size());
        QTRY_COMPARE(bar->mapToScene({0, bar->height()}).y(), qreal(window->height()));
        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!bar->isVisible());
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() - 2));
        QTRY_VERIFY(bar->isVisible());

        QSignalSpy visibilityChanges(bar, &QQuickItem::visibleChanged);
        QTRY_COMPARE(bar->mapToScene({0, bar->height()}).y(), qreal(window->height()));
        for (const char *name : {"playButton", "stopButton", "previousButton", "nextButton",
                                "ejectButton", "positionSlider", "muteButton", "volumeSlider",
                                "searchMediaButton", "settingsButton",
                                "playbackMenuButton"}) {
            auto *control = findItem(bar, QString::fromLatin1(name));
            QVERIFY2(control && control->isEnabled(), name);
            const QPoint center = control->mapToScene(
                    {control->width() / 2, control->height() / 2}).toPoint();
            QTest::mouseMove(window, center, 20);
            QTest::qWait(80);
            QVERIFY2(bar->isVisible(), name);
            // A hide/show cycle between frames is also a failure.
            QVERIFY2(visibilityChanges.isEmpty(), name);
        }

        auto *mute = findItem(bar, "muteButton");
        const bool wasMuted = video->muted();
        click(window, mute);
        QTRY_COMPARE(video->muted(), !wasMuted);
        QVERIFY(bar->isVisible());
        QVERIFY(visibilityChanges.isEmpty());
        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!bar->isVisible());
    }

    void fullscreenPlaylistToggleRemainsClickable()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        auto *bar = findItem(window->contentItem(), "playbackBar");
        auto *button = findItem(bar, "playbackMenuButton");
        QVERIFY(panel && bar && button);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(window->size(), window->screen()->size());
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() - 2));
        QTRY_VERIFY(bar->isVisible());
        click(window, button);
        QTRY_VERIFY(window->property("playlistVisible").toBool());
        QVERIFY(panel->isVisible());
        QTRY_COMPARE(panel->mapToScene({0, panel->height()}).y(), qreal(window->height()));
        QTRY_COMPARE(bar->width(), qreal(window->width()) - panel->width());
        QVERIFY(bar->mapToScene({bar->width(), 0}).x() <= panel->mapToScene({0, 0}).x());
        click(window, button);
        QTRY_VERIFY(!window->property("playlistVisible").toBool());
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(bar->isVisible());
        click(window, button);
        QTRY_VERIFY(panel->isVisible());
    }

    void fullscreenPlaylistRevealsAtRightEdge()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        auto *stage = findItem(window->contentItem(), "videoStage");
        auto *divider = findItem(window->contentItem(), "playlistResizeHandle");
        QVERIFY(panel && stage && divider);
        const qreal savedWidth = panel->width();
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(window->size(), window->screen()->size());
        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!panel->isVisible());
        QTest::mouseMove(window, QPoint(window->width() - 2, 100));
        QTRY_VERIFY(panel->isVisible());
        QTRY_COMPARE(stage->size(), QSizeF(window->size()));
        QTRY_COMPARE(panel->x() + panel->width(), qreal(window->width()));
        QTRY_COMPARE(divider->x(), panel->x() - divider->width() / 2);
        QVERIFY(!window->property("playlistVisible").toBool());

        QSignalSpy visibilityChanges(panel, &QQuickItem::visibleChanged);
        QTest::mouseMove(window, panel->mapToScene(
                {panel->width() / 2, panel->height() / 2}).toPoint(), 20);
        QTest::qWait(80);
        QVERIFY(panel->isVisible());
        QVERIFY(visibilityChanges.isEmpty());
        QTest::mouseMove(window, divider->mapToScene(
                {divider->width() / 2, divider->height() / 2}).toPoint(), 20);
        QTRY_COMPARE(window->cursor().shape(), Qt::SplitHCursor);
        QVERIFY(panel->isVisible());

        QTest::mouseMove(window, QPoint(window->width() / 2, 100));
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTRY_VERIFY(panel->isVisible());
        QCOMPARE(panel->width(), savedWidth);
    }

    void fullscreenVideoDragRevealsPlaylist_data()
    {
        QTest::addColumn<bool>("initiallyVisible");
        QTest::addColumn<bool>("enterAtRightEdge");
        QTest::addColumn<bool>("dropOnTab");
        QTest::newRow("video-to-content-shown") << true << false << false;
        QTest::newRow("video-to-tab-hidden") << false << false << true;
        QTest::newRow("edge-to-content-hidden") << false << true << false;
        QTest::newRow("edge-to-tab-shown") << true << true << true;
    }

    void fullscreenVideoDragRevealsPlaylist()
    {
        QFETCH(bool, initiallyVisible);
        QFETCH(bool, enterAtRightEdge);
        QFETCH(bool, dropOnTab);
        QTemporaryDir directory;
        const QUrl originalSource = makeVideo(directory.filePath("playing.y4m"));
        const QUrl addedSource = makeVideo(directory.filePath("added.y4m"));
        const QUrl audio = makeAudio(directory.filePath("audio.wav"));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.setInitialProperties({{"mediaSource", originalSource}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        auto *stage = findItem(window->contentItem(), "videoStage");
        auto *video = window->findChild<MpvVideo *>();
        auto *playlists = window->findChild<PlaylistCollection *>();
        QVERIFY(panel && stage && video && playlists);
        auto *playlist = playlists->currentPlaylist();
        window->setProperty("playlistVisible", initiallyVisible);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        const quint64 revision = video->loadRevision();
        QCOMPARE(playlist->count(), 1);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(window->size(), window->screen()->size());
        const QPoint center(window->width() / 2, window->height() / 2);
        QTest::mouseMove(window, center);
        QTRY_VERIFY(!panel->isVisible());
        const QPoint start = enterAtRightEdge
            ? QPoint(window->width() - 2, center.y()) : center;

        // Unsupported files neither accept the drag nor expose the hidden panel.
        QMimeData rejected;
        rejected.setUrls({audio});
        QDragEnterEvent invalid(start, Qt::CopyAction, &rejected, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &invalid);
        QVERIFY(!invalid.isAccepted());
        QVERIFY(!panel->isVisible());
        QDragLeaveEvent invalidLeave;
        QCoreApplication::sendEvent(window, &invalidLeave);

        QMimeData files;
        files.setUrls({addedSource, audio});
        QDragEnterEvent enter(start, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QVERIFY(enter.isAccepted());
        QTRY_VERIFY(panel->isVisible());
        QVERIFY(!window->property("playlistVisible").toBool());
        QTRY_COMPARE(stage->size(), QSizeF(window->size()));
        QCOMPARE(video->source(), originalSource);
        QCOMPARE(video->loadRevision(), revision);
        QVERIFY(video->playing());

        // Leaving the application clears only the temporary drag reveal.
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(window, &leave);
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(playlist->count(), 1);
        QDragEnterEvent reenter(start, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &reenter);
        QVERIFY(reenter.isAccepted());
        QTRY_VERIFY(panel->isVisible());
        auto *target = findItem(panel, dropOnTab ? "playlistTab0" : "trackList");
        QVERIFY(target);
        const QPoint end = target->mapToScene({target->width() / 2, target->height() / 2}).toPoint();
        QDragMoveEvent move(end, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &move);
        QVERIFY(move.isAccepted());
        QCoreApplication::processEvents();
        QVERIFY(panel->isVisible());
        QDropEvent drop(end, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE(playlist->count(), 2);
        QCOMPARE(playlists->count(), 1);
        QCOMPARE(playlist->data(playlist->index(1), PlaylistModel::SourceRole).toUrl(), addedSource);
        QCOMPARE(playlist->currentSource(), originalSource);
        QCOMPARE(video->source(), originalSource);
        QCOMPARE(video->loadRevision(), revision);
        QVERIFY(video->playing());
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(!window->property("playlistVisible").toBool());

        // A manually opened fullscreen playlist stays open after cancellation.
        window->setProperty("playlistVisible", true);
        QTRY_VERIFY(panel->isVisible());
        QDragEnterEvent manual(start, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &manual);
        QVERIFY(manual.isAccepted());
        QDragLeaveEvent manualLeave;
        QCoreApplication::sendEvent(window, &manualLeave);
        QVERIFY(panel->isVisible());
        window->setProperty("playlistVisible", false);
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTRY_COMPARE(window->property("playlistVisible").toBool(), initiallyVisible);
        QTRY_COMPARE(panel->isVisible(), initiallyVisible);
        QVERIFY2(warnings.isEmpty(), "Fullscreen video drops generated QML warnings");
    }

    void mouseWheelDoesNotLeakThroughUi_data()
    {
        QTest::addColumn<bool>("fullscreen");
        QTest::newRow("windowed") << false;
        QTest::newRow("fullscreen") << true;
    }

    void mouseWheelDoesNotLeakThroughUi()
    {
        QFETCH(bool, fullscreen);
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *queue = window->findChild<PlaylistModel *>();
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        auto *list = findItem(panel, "trackList");
        auto *bar = findItem(window->contentItem(), "playbackBar");
        QVERIFY(video && queue && panel && list && bar);
        if (fullscreen) {
            QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
            QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
            QTRY_COMPARE(window->size(), window->screen()->size());
            window->setProperty("playlistVisible", true);
            window->setProperty("controlsVisible", true);
        }
        QTRY_VERIFY(panel->isVisible() && bar->isVisible());
        video->setVolume(50);
        video->setMuted(true);
        QTest::qWait(50);
        QSignalSpy volumeChanges(video, &MpvVideo::volumeChanged);
        QSignalSpy muteChanges(video, &MpvVideo::mutedChanged);
        QElapsedTimer wheelTimer;
        wheelTimer.start();
        const auto wheelAt = [window, &wheelTimer](QQuickItem *target, QPointF position,
                                      QPoint angle, QPoint pixels = {},
                                      Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            const QPoint local = target->mapToScene(position).toPoint();
            QTest::mouseMove(window, local, 20);
            QWheelEvent event(local, window->mapToGlobal(local), pixels, angle,
                              Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            // Flickable calculates velocity from timestamps across wheel events.
            event.setTimestamp(wheelTimer.elapsed() + 1);
            QCoreApplication::sendEvent(window, &event);
            QTest::qWait(30);
        };

        // Empty and short lists cannot consume scrolling through movement.
        for (int count : {0, 1}) {
            if (count)
                queue->appendUrls({QUrl("https://example.com/short.mkv")});
            QTRY_COMPARE(list->property("count").toInt(), count);
            for (const QPoint angle : {QPoint(0, 120), QPoint(0, -120)}) {
                wheelAt(list, {100, 60}, angle);
                QVERIFY2(volumeChanges.isEmpty(), "Wheel over an empty/short playlist changed volume");
                QVERIFY(muteChanges.isEmpty());
            }
            wheelAt(list, {100, 60}, {}, {0, 40});
            QVERIFY2(volumeChanges.isEmpty(), "Playlist trackpad scrolling changed volume");
            QVERIFY(muteChanges.isEmpty());
            wheelAt(list, {100, 60}, {0, -120}, {}, Qt::ShiftModifier);
            QVERIFY(volumeChanges.isEmpty());
            QVERIFY(muteChanges.isEmpty());
        }

        // Real scrolling still works; both ends must keep unhandled events local.
        QList<QUrl> tracks;
        for (int i = 0; i < 80; ++i)
            tracks.append(QUrl(QStringLiteral("https://example.com/episode%1.mkv").arg(i)));
        queue->appendUrls(tracks);
        QTRY_VERIFY(list->property("contentHeight").toReal() > list->height());
        QSignalSpy renderedFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!renderedFrames.isEmpty());
        QTRY_VERIFY(!list->property("moving").toBool());
        list->setProperty("contentY", list->property("originY"));
        const int selected = panel->property("selectedSourceIndex").toInt();
        wheelAt(list, {100, 60}, {0, 120});
        QVERIFY2(volumeChanges.isEmpty(), "Wheel at the playlist's top changed volume");
        wheelAt(list, {100, 60}, {0, -120});
        QTRY_VERIFY(list->property("contentY").toReal() > list->property("originY").toReal());
        QTRY_VERIFY(!list->property("moving").toBool());
        QVERIFY(volumeChanges.isEmpty());
        const qreal bottom = list->property("originY").toReal()
                + list->property("contentHeight").toReal() - list->height();
        list->setProperty("contentY", bottom - 10);
        wheelAt(list, {100, 60}, {0, -240});
        QTRY_VERIFY(list->property("atYEnd").toBool());
        QTRY_VERIFY(!list->property("moving").toBool());
        QVERIFY2(volumeChanges.isEmpty(), "Scrolling to the playlist's bottom changed volume");
        list->setProperty("contentY", bottom);
        wheelAt(list, {100, 60}, {0, -120});
        QVERIFY2(volumeChanges.isEmpty(), "Wheel at the playlist's bottom changed volume");
        wheelAt(list, {100, 60}, {}, {0, -40});
        QVERIFY(volumeChanges.isEmpty());
        QCOMPARE(panel->property("selectedSourceIndex").toInt(), selected);
        QCOMPARE(queue->currentIndex(), 0);

        // Backgrounds, buttons, text fields and disabled controls sit above video
        // in fullscreen, but none should pass wheel events to its volume handler.
        for (const char *name : {"playlistTabs", "playlistVerticalScrollBar", "playlistFooter", "playlistSearch",
                                "playlistOptionsButton", "showCurrentTrackButton",
                                "moveUpButton", "addPlaylistButton", "sortPlaylistButton",
                                "playbackTimestamps", "playButton", "stopButton", "muteButton",
                                "settingsButton", "positionSlider", "playlistResizeHandle",
                                "titleBar", "appMenuButton", "pinButton"}) {
            auto *target = findItem(window->contentItem(), QString::fromLatin1(name));
            QVERIFY2(target, name);
            // Moving to the top first reveals the fullscreen title bar.
            if (target->height() == 0) {
                QTest::mouseMove(window, {window->width() / 2, 1}, 20);
                QTRY_VERIFY(target->height() > 0);
            }
            for (const QPoint angle : {QPoint(0, 120), QPoint(0, -120)}) {
                wheelAt(target, {target->width() / 2, target->height() / 2}, angle);
                QVERIFY2(volumeChanges.isEmpty(), name);
                QVERIFY2(muteChanges.isEmpty(), name);
            }
            wheelAt(target, {target->width() / 2, target->height() / 2}, {}, {0, 40});
            QVERIFY2(volumeChanges.isEmpty(), name);
            QVERIFY2(muteChanges.isEmpty(), name);
        }

        // Modal dialog content and the dimmed area must also isolate scrolling.
        QVERIFY(QMetaObject::invokeMethod(window, "showAdvancedSettings"));
        auto *settings = window->findChild<QObject *>("advancedSettingsDialog");
        QVERIFY(settings);
        QTRY_VERIFY(settings->property("opened").toBool());
        auto *settingsScroll = findItem(window->contentItem(), "advancedSettingsScroll");
        QVERIFY(settingsScroll);
        wheelAt(settingsScroll, {settingsScroll->width() / 2, 15}, {0, 120});
        QVERIFY(volumeChanges.isEmpty());
        wheelAt(window->contentItem(), {30, window->height() / 2.0}, {0, 120});
        QVERIFY(volumeChanges.isEmpty());
        QVERIFY(muteChanges.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QTRY_VERIFY(!settings->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(window, "showAppMenu"));
        auto *menu = window->findChild<QObject *>("appMenu");
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *menuItem = menuItemAt(menu, 0);
        QVERIFY(menuItem);
        wheelAt(menuItem, {menuItem->width() / 2, menuItem->height() / 2}, {0, 120});
        QVERIFY(volumeChanges.isEmpty());
        QVERIFY(muteChanges.isEmpty());
        QCOMPARE(video->volume(), 50.0);
        QVERIFY(video->muted());
    }

    void mouseWheelAdjustsVolume()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *stage = findItem(window->contentItem(), "videoStage");
        QVERIFY(video && stage);
        auto wheelAt = [window](QQuickItem *target, QPoint angle, QPoint pixels = {}) {
            // Let prior asynchronous libmpv property updates reach the UI.
            QTest::qWait(30);
            const QPoint local = target->mapToScene(
                    {target->width() / 2, target->height() / 2}).toPoint();
            QTest::mouseMove(window, local);
            QWheelEvent event(local, window->mapToGlobal(local), pixels, angle,
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &event);
        };
        video->setVolume(50);
        video->setMuted(true);
        wheelAt(stage, {0, 120});
        QTRY_COMPARE(video->volume(), 55.0);
        QVERIFY(!video->muted());
        wheelAt(stage, {0, -240});
        QTRY_COMPARE(video->volume(), 45.0);
        wheelAt(stage, {120, 0});
        QCOMPARE(video->volume(), 45.0);
        wheelAt(stage, {}, {0, 40});
        QTRY_COMPARE(video->volume(), 50.0);
        video->setVolume(98);
        wheelAt(stage, {0, 120});
        QTRY_COMPARE(video->volume(), 100.0);
        video->setVolume(2);
        wheelAt(stage, {0, -120});
        QTRY_COMPARE(video->volume(), 0.0);

        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        wheelAt(stage, {0, 120});
        QTRY_COMPARE(video->volume(), 5.0);

        // An enabled volume slider keeps its own one-percent wheel step, with
        // no second change from the fullscreen video below the playback bar.
        window->setProperty("controlsVisible", true);
        auto *bar = findItem(window->contentItem(), "playbackBar");
        auto *slider = findItem(bar, "volumeSlider");
        QVERIFY(bar && slider);
        QTRY_VERIFY(bar->isVisible());
        video->setVolume(50);
        video->setMuted(true);
        QSignalSpy seekRequests(bar, SIGNAL(seekRequested(double)));
        QVERIFY(seekRequests.isValid());
        wheelAt(slider, {0, 120});
        QTRY_COMPARE(video->volume(), 51.0);
        QVERIFY(!video->muted());
        wheelAt(slider, {0, -120});
        QTRY_COMPARE(video->volume(), 50.0);
        QCOMPARE(seekRequests.count(), 0);
        slider->setEnabled(false);
        video->setMuted(true);
        wheelAt(slider, {0, 120});
        QTest::qWait(50);
        QCOMPARE(video->volume(), 50.0);
        QVERIFY(video->muted());
    }

    void videoRenderingResetsPixelTransferState()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath("ten-bit.y4m"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("YUV4MPEG2 W64 H36 F1:1 Ip A1:1 C420p10\n");
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        for (int frame = 0; frame < 30; ++frame) {
            file.write("FRAME\n");
            for (int pixel = 0; pixel < 64 * 36; ++pixel)
                stream << quint16(frame < 15 ? 128 : 880);
            for (int pixel = 0; pixel < 64 * 36 / 2; ++pixel)
                stream << quint16(512);
        }
        file.close();

        std::atomic<bool> desktopGl{false};
        std::atomic<int> injectedFrames{0};
        QQuickWindow window;
        window.resize(640, 360);
        // Qt's resetOpenGLState() leaves pixel-transfer state alone. Emulate
        // another renderer leaving byte swapping enabled in the shared context.
        connect(&window, &QQuickWindow::beforeRendering, &window, [&] {
            auto *context = QOpenGLContext::currentContext();
            if (context && !context->isOpenGLES()) {
                desktopGl = true;
                context->functions()->glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
                ++injectedFrames;
            }
        }, Qt::DirectConnection);
        connect(&window, &QQuickWindow::afterRendering, &window, [] {
            auto *context = QOpenGLContext::currentContext();
            if (context && !context->isOpenGLES())
                context->functions()->glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
        }, Qt::DirectConnection);
        auto *video = new MpvVideo(window.contentItem());
        video->setSize(window.size());
        video->setSource(QUrl::fromLocalFile(file.fileName()));
        window.show();
        QVERIFY(exposeWindow(&window));
        if (!desktopGl)
            QSKIP("Byte swapping is specific to desktop OpenGL.");
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        QTRY_VERIFY(!video->playing());
        const auto centerColor = [&] {
            const QImage image = window.grabWindow();
            return image.isNull() ? QColor{} : image.pixelColor(image.rect().center());
        };
        const auto neutral = [](const QColor &color) {
            return color.isValid() && qAbs(color.red() - color.green()) < 8
                && qAbs(color.green() - color.blue()) < 8;
        };
        video->seek(20);
        QTRY_VERIFY(qAbs(video->position() - 20) < 0.1);
        QTRY_VERIFY_WITH_TIMEOUT(neutral(centerColor()) && centerColor().red() > 160, 5000);
        video->seek(3);
        QTRY_VERIFY(qAbs(video->position() - 3) < 0.1);
        QTRY_VERIFY_WITH_TIMEOUT(neutral(centerColor()) && centerColor().red() < 80, 5000);
        // The same frame remains correct when Qt replaces the target FBO.
        window.resize(800, 450);
        video->setSize(window.size());
        QTRY_VERIFY_WITH_TIMEOUT(neutral(centerColor()) && centerColor().red() < 80, 5000);
        QVERIFY(injectedFrames > 2);
        QCOMPARE(video->errorString(), QString{});
        video->stop();
    }

    void videoBackgroundIncludesLetterboxing()
    {
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("background.y4m"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *stage = findItem(window->contentItem(), "videoStage");
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(stage && video);
        const auto backgroundColor = [&] {
            const QImage image = window->grabWindow();
            if (image.isNull())
                return QColor{};
            // Below hints and outside both the watermark and 16:9 video.
            const QPointF point = stage->mapToScene({stage->width() / 2, 8});
            return image.pixelColor(qRound(point.x() * image.width() / window->width()),
                                    qRound(point.y() * image.height() / window->height()));
        };
        QTRY_COMPARE(backgroundColor(), QColor("#101010"));
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(backgroundColor(), QColor("#101010"));
        video->stop();
        QTRY_COMPARE(backgroundColor(), QColor("#101010"));
    }

    void fullscreenCursorHidesAndRestores()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *cursor = window->findChild<FullscreenCursor *>();
        QVERIFY(cursor);
        QCOMPARE(cursor->idleInterval(), 3000);
        cursor->setIdleInterval(150);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTest::mouseMove(window, {window->width() / 2, window->height() / 2}, 20);
        QTRY_VERIFY(cursor->hidden());
        QCOMPARE(window->cursor().shape(), Qt::BlankCursor);
        QTest::mouseMove(window, {window->width() / 2 + 20, window->height() / 2}, 20);
        QVERIFY(!cursor->hidden());
        QVERIFY(window->cursor().shape() != Qt::BlankCursor);
        QTRY_VERIFY(cursor->hidden());
        QVERIFY(QMetaObject::invokeMethod(window, "showAppMenu"));
        auto *menu = window->findChild<QObject *>("appMenu");
        QTRY_VERIFY(menu->property("visible").toBool());
        QTRY_VERIFY(!cursor->hidden());
        QTest::qWait(250);
        QVERIFY(!cursor->hidden());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(cursor->hidden());
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QVERIFY(!cursor->hidden());
        QVERIFY(window->cursor().shape() != Qt::BlankCursor);
        QTest::qWait(250);
        QVERIFY(!cursor->hidden());
    }

    void playlistFileMenuOpensClickedLocation()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("first.y4m"));
        const QUrl second = makeVideo(directory.filePath("字幕, second.y4m"));
        const QUrl remote("https://example.com/remote.mkv");
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *queue = window->findChild<PlaylistModel *>();
        auto *video = window->findChild<MpvVideo *>();
        auto *library = window->findChild<VideoLibrary *>();
        auto *panel = findItem(window->contentItem(), "playlistPanel");
        auto *search = findItem(panel, "playlistSearch");
        auto *menu = window->findChild<QObject *>("playlistFileMenu");
        QVERIFY(queue && video && library && menu);
        auto *openLocation = menuItemAt(menu, 0);
        QVERIFY(openLocation);
        queue->appendUrls({second, remote});
        QVERIFY(library->canOpenFileLocation(second));
        QVERIFY(!library->canOpenFileLocation(remote));
        QVERIFY(!library->openFileLocation(remote));
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        video->seek(6);
        QTRY_VERIFY(qAbs(video->position() - 6) < 0.1);
        search->setProperty("text", "second");
        QTRY_COMPARE(findItem(panel, "trackList")->property("count").toInt(), 1);
        QSignalSpy requests(panel, SIGNAL(trackRequested(int)));
        click(window, findItem(panel, "track0"), Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(panel->property("contextSource").toUrl(), second);
        QVERIFY(openLocation->isEnabled());
        QCOMPARE(video->source(), first);
        QCOMPARE(video->position(), 6.0);
        QVERIFY(!video->playing() && !video->stopped());
        QCOMPARE(requests.count(), 0);
#ifndef Q_OS_WIN
        // Capture the OS handoff without launching a file manager during the test.
        FileLocationReceiver receiver;
        QDesktopServices::setUrlHandler("file", &receiver, "capture");
        const auto cleanup = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        click(window, openLocation);
        QCOMPARE(receiver.opened, QUrl::fromLocalFile(directory.path()));
#else
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
#endif
        search->setProperty("text", "remote");
        QTRY_COMPARE(findItem(panel, "trackList")->property("count").toInt(), 1);
        click(window, findItem(panel, "track0"), Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(!openLocation->isEnabled());
        QCOMPARE(panel->property("contextSource").toUrl(), remote);
        QCOMPARE(video->source(), first);
        QCOMPARE(requests.count(), 0);
    }

    void timelinePreviewCanBeDisabled()
    {
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("preview.y4m"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *seek = findItem(window->contentItem(), "positionSlider");
        auto *preview = window->findChild<MpvPreview *>();
        auto *preferences = window->findChild<QObject *>("playbackPreferences");
        QVERIFY(video && seek && preview && preferences);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        click(window, seek);
        QTRY_VERIFY(preview->active());
        QVERIFY(preferences->setProperty("previewEnabled", false));
        QTRY_VERIFY(!seek->property("previewVisible").toBool());
        QTRY_VERIFY(!preview->active());
        const QPoint point = seek->mapToScene({seek->width() * 0.25, seek->height() / 2}).toPoint();
        QTest::mouseMove(window, point, 20);
        QVERIFY(!preview->active());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(!seek->property("previewVisible").toBool());
        QVERIFY(!preview->active());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(qAbs(video->position() - 7) < 1);
        QVERIFY(preferences->setProperty("previewEnabled", true));
        QTRY_VERIFY(seek->property("previewVisible").toBool());
        QTRY_VERIFY(preview->active());
    }

    void fullscreenTimelinePreviewShowsHoveredFrame()
    {
        QTemporaryDir directory;
        const QUrl source = makeVideo(directory.filePath("fullscreen-preview.y4m"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *bar = findItem(window->contentItem(), "playbackBar");
        auto *seek = findItem(bar, "positionSlider");
        auto *preview = window->findChild<MpvPreview *>();
        QVERIFY(video && seek && preview);
        video->play();
        QTRY_VERIFY(video->mediaLoaded() && video->seekable());
        video->togglePause();
        video->seek(3);
        QTRY_VERIFY(qAbs(video->position() - 3) < 0.1);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTRY_COMPARE(window->size(), window->screen()->size());
        QTest::mouseMove(window, {window->width() / 2, window->height() - 2});
        QTRY_VERIFY(bar->isVisible());
        auto *handle = seek->property("handle").value<QQuickItem *>();
        const auto hoverAt = [&](double fraction) {
            const qreal x = seek->property("leftPadding").toReal() + handle->width() / 2
                + (seek->property("availableWidth").toReal() - handle->width()) * fraction;
            QTest::mouseMove(window, seek->mapToScene({x, seek->height() / 2}).toPoint(), 20);
        };
        hoverAt(0.2);
        QTRY_VERIFY(preview->active());
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) >= 0
                                && previewBrightness(window, preview) < 80, 10000);
        hoverAt(0.8);
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) > 160, 10000);
        QCOMPARE(video->position(), 3.0);
        QVERIFY(!video->playing());
    }

    void timelinePreviewShowsHoveredFrame()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("dark-then-light.y4m"));
        const QUrl second = makeVideo(directory.filePath("light-then-dark.y4m"), true);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        auto *video = window->findChild<MpvVideo *>();
        auto *playlist = window->findChild<PlaylistModel *>();
        auto *seek = item("positionSlider");
        auto *preview = qobject_cast<MpvPreview *>(item("previewVideo"));
        QVERIFY(video && playlist && seek && preview);
        QCOMPARE(playlist->appendUrls({second}), 1);
        auto *handle = seek->property("handle").value<QQuickItem *>();
        QVERIFY(handle);
        auto hoverAt = [window, seek, handle](double fraction) {
            const qreal x = seek->property("leftPadding").toReal() + handle->width() / 2
                    + (seek->property("availableWidth").toReal() - handle->width()) * fraction;
            QTest::mouseMove(window, seek->mapToScene({x, seek->height() / 2}).toPoint(), 20);
        };

        click(window, item("playButton"));
        QTRY_VERIFY_WITH_TIMEOUT(video->seekable() && video->duration() > 29, 10000);
        click(window, item("pauseButton"));
        QTRY_VERIFY(!video->playing() && !video->stopped());
        video->seek(3);
        QTRY_VERIFY(qAbs(video->position() - 3) < 0.1);
        const double pausedPosition = video->position();
        QSignalSpy seekRequests(item("playbackBar"), SIGNAL(seekRequested(double)));
        QVERIFY(seekRequests.isValid());

        hoverAt(0.2);
        QTRY_VERIFY(preview->active());
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        QCOMPARE(preview->source(), first);
        QVERIFY(qAbs(preview->position() - 6) < 0.15);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) >= 0
                                && previewBrightness(window, preview) < 80, 10000);
        QCOMPARE(preview->errorString(), QString{});
        QCOMPARE(seekRequests.count(), 0);
        QCOMPARE(video->position(), pausedPosition);
        QVERIFY(!video->playing());

        hoverAt(0.8);
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        QVERIFY(qAbs(preview->position() - 24) < 0.15);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) > 160, 10000);
        QCOMPARE(seekRequests.count(), 0);
        QCOMPARE(video->position(), pausedPosition);
        QVERIFY(!video->playing());

        // The timeline endpoint still returns the final available frame.
        QTest::mouseMove(window,
                        seek->mapToScene({seek->width() - 1, seek->height() / 2}).toPoint(), 20);
        QTRY_COMPARE(preview->position(), video->duration());
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) > 160, 10000);

        // Several changes inside the debounce interval must settle on the last target.
        for (const double fraction : {0.1, 0.9, 0.3, 0.7, 0.2})
            hoverAt(fraction);
        QVERIFY(qAbs(preview->position() - 6) < 0.15);
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) >= 0
                                && previewBrightness(window, preview) < 80, 10000);
        QCOMPARE(video->position(), pausedPosition);
        QCOMPARE(seekRequests.count(), 0);
        QCOMPARE(preview->errorString(), QString{});

        QTest::mouseMove(window, item("videoStage")->mapToScene({20, 20}).toPoint(), 20);
        QTRY_VERIFY(!preview->active());
        QTRY_VERIFY(!item("timelinePreview")->isVisible());
        playlist->setCurrentIndex(1);
        QTRY_COMPARE(video->source(), second);
        click(window, item("playButton"));
        QTRY_VERIFY_WITH_TIMEOUT(video->seekable() && video->duration() > 29, 10000);
        click(window, item("pauseButton"));
        QTRY_VERIFY(!video->playing() && !video->stopped());
        const double secondPausedPosition = video->position();
        hoverAt(0.2);
        QTRY_COMPARE(preview->source(), second);
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 10000);
        if (qEnvironmentVariableIsSet("KPLAYER_TEST_CAPTURE"))
            qInfo() << "Source-change preview" << preview->source() << preview->position()
                    << preview->active() << preview->ready() << preview->errorString()
                    << "brightness" << previewBrightness(window, preview);
        QTRY_VERIFY_WITH_TIMEOUT(previewBrightness(window, preview) > 160, 10000);
        QCOMPARE(video->position(), secondPausedPosition);
        QCOMPARE(seekRequests.count(), 0);
        QCOMPARE(preview->errorString(), QString{});
        video->stop();
    }

    void externalMediaPlayback()
    {
        const QString mediaPath = qEnvironmentVariable("KPLAYER_TEST_MEDIA");
        if (mediaPath.isEmpty())
            QSKIP("Set KPLAYER_TEST_MEDIA to smoke-test an existing video file.");
        QVERIFY(QFileInfo(mediaPath).isFile());
        const QUrl source = QUrl::fromLocalFile(QFileInfo(mediaPath).absoluteFilePath());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"mediaSource", source}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        QVERIFY(video);
        video->play();
        QTRY_VERIFY_WITH_TIMEOUT(video->seekable() && video->duration() > 3, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(video->position() > 0, 10000);
        QCOMPARE(video->errorString(), QString{});
        video->togglePause();
        QTRY_VERIFY(!video->playing() && !video->stopped());
        const double target = video->duration() / 3;
        video->seek(target);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(video->position() - target) < 0.5, 10000);
        video->setMuted(true);
        QTRY_VERIFY(video->muted());
        video->setVolume(45);
        QTRY_COMPARE(video->volume(), 45.0);
        video->setMuted(false);
        QTRY_VERIFY(!video->muted());

        auto *seek = findItem(window->contentItem(), "positionSlider");
        auto *preview = qobject_cast<MpvPreview *>(
                findItem(window->contentItem(), "previewVideo"));
        QVERIFY(seek && preview);
        QSignalSpy seekRequests(findItem(window->contentItem(), "playbackBar"),
                                SIGNAL(seekRequested(double)));
        QVERIFY(seekRequests.isValid());
        const double pausedPosition = video->position();
        QTest::mouseMove(window, seek->mapToScene(
                {seek->width() * 0.65, seek->height() / 2}).toPoint(), 20);
        QTRY_VERIFY(preview->active());
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(), 15000);
        QCOMPARE(preview->source(), source);
        QCOMPARE(preview->errorString(), QString{});
        // Compressed video can settle on a nearby frame after an exact seek.
        QVERIFY(qAbs(video->position() - pausedPosition) < 0.1);
        QVERIFY(!video->playing());
        QCOMPARE(seekRequests.count(), 0);
        const QImage screenshot = window->grabWindow();
        QVERIFY(!screenshot.isNull());
        const QString capturePath = qEnvironmentVariable("KPLAYER_TEST_CAPTURE");
        if (!capturePath.isEmpty())
            QVERIFY(screenshot.save(capturePath));
        qInfo() << "Local media playback verified:" << source
                << "duration" << video->duration() << "preview" << preview->position();
        video->stop();
        QVERIFY(video->stopped());
    }

    void playbackInteractions()
    {
        QTemporaryDir directory;
        const QUrl first = makeAudio(directory.filePath("first.wav"));
        const QUrl second = makeAudio(directory.filePath("second.wav"));
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &errors) { warnings.append(errors); });
        engine.setInitialProperties({{"mediaSource", first}});
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *playlist = window->findChild<PlaylistModel *>();
        QVERIFY(video && playlist);
        QCOMPARE(playlist->count(), 1);
        QCOMPARE(playlist->appendUrls({second}), 1);
        QVERIFY(video->stopped());

        auto item = [window](const char *name) {
            return findItem(window->contentItem(), QString::fromLatin1(name));
        };
        QVERIFY(item("playButton"));
        click(window, item("playButton"));
        QTRY_VERIFY(video->playing());
        QTRY_VERIFY_WITH_TIMEOUT(video->duration() > 29, 10000);
        QTRY_VERIFY(video->seekable());
        QTRY_VERIFY(video->position() > 0);
        QCOMPARE(video->errorString(), QString{});

        auto *stage = item("videoStage");
        QVERIFY(stage);
        QSignalSpy stagePlayPauseRequests(stage, SIGNAL(playPauseRequested()));
        QVERIFY(stagePlayPauseRequests.isValid());
        click(window, stage);
        QCOMPARE(stagePlayPauseRequests.count(), 0);
        QVERIFY(video->playing());
        const QPoint stageCenter = stage->mapToScene(
                {stage->width() / 2, stage->height() / 2}).toPoint();
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, stageCenter);
        QTRY_COMPARE(stagePlayPauseRequests.count(), 1);
        QTRY_VERIFY(!video->playing() && !video->stopped());
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, stageCenter);
        QTRY_COMPARE(stagePlayPauseRequests.count(), 2);
        QTRY_VERIFY(video->playing());

        // The transport repeater replaces its delegates when playback resumes.
        // Wait for that layout to reach the scene before targeting the new button.
        QSignalSpy renderedFrames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!renderedFrames.isEmpty(), 5000);
        auto *pauseButton = item("pauseButton");
        QVERIFY(pauseButton && pauseButton->isEnabled() && pauseButton->isVisible());
        QVERIFY(pauseButton->width() > 0 && pauseButton->height() > 0);
        QSignalSpy pauseRequests(item("playbackBar"), SIGNAL(playPauseRequested()));
        QVERIFY(pauseRequests.isValid());
        click(window, pauseButton);
        QCOMPARE(pauseRequests.count(), 1);
        QTRY_VERIFY(!video->playing() && !video->stopped());
        auto *mute = item("muteButton");
        QVERIFY(mute);
        click(window, mute);
        QTRY_VERIFY(video->muted());
        click(window, mute);
        QTRY_VERIFY(!video->muted());

        auto *volume = item("volumeSlider");
        QVERIFY(volume);
        click(window, volume);
        QTRY_VERIFY(video->volume() > 40 && video->volume() < 60);
        QTest::keyClick(window, Qt::Key_Right);
        const double adjustedVolume = video->volume();
        QTRY_COMPARE(volume->property("value").toDouble(), adjustedVolume);
        video->setMuted(true);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(!video->muted());

        click(window, item("nextButton"));
        QTRY_VERIFY(video->position() > 9 && video->position() < 12);
        QCOMPARE(playlist->currentIndex(), 0);
        click(window, item("previousButton"));
        QTRY_VERIFY(video->position() < 2);
        QCOMPARE(playlist->currentIndex(), 0);

        auto *seek = item("positionSlider");
        QVERIFY(seek);
        click(window, seek);
        QTRY_VERIFY(video->position() > 13 && video->position() < 17);
        // Playback changes must keep updating the slider after an interaction.
        video->seek(5);
        QTRY_VERIFY(video->position() > 4 && video->position() < 6);
        QTRY_VERIFY(seek->property("value").toDouble() > 4
                    && seek->property("value").toDouble() < 6);

        QSignalSpy seekRequests(item("playbackBar"), SIGNAL(seekRequested(double)));
        QVERIFY(seekRequests.isValid());
        QVERIFY(seek->property("previewEnabled").toBool());
        const double pausedPosition = video->position();
        const qreal leftPadding = seek->property("leftPadding").toReal();
        const qreal availableWidth = seek->property("availableWidth").toReal();
        auto *seekHandle = seek->property("handle").value<QQuickItem *>();
        auto *preview = item("timelinePreview");
        auto *previewTimestamp = item("previewTimestamp");
        auto *previewVideo = qobject_cast<MpvPreview *>(item("previewVideo"));
        QVERIFY(seekHandle && preview && previewTimestamp && previewVideo);
        const double rangeStart = seek->property("from").toDouble();
        const double rangeEnd = seek->property("to").toDouble();
        for (const double fraction : {0.25, 0.75}) {
            const QPoint hoverPoint = seek->mapToScene(
                    {leftPadding + seekHandle->width() / 2
                     + (availableWidth - seekHandle->width()) * fraction,
                     seek->height() / 2}).toPoint();
            QTest::mouseMove(window, hoverPoint, 20);
            QTRY_VERIFY(seek->property("previewVisible").toBool());
            QVERIFY(preview->isVisible());
            const double expectedTime = rangeStart + (rangeEnd - rangeStart) * fraction;
            QTRY_VERIFY(qAbs(seek->property("previewTime").toDouble() - expectedTime) < 0.15);
            const int timestampSeconds = int(seek->property("previewTime").toDouble());
            QTRY_VERIFY_WITH_TIMEOUT(!previewVideo->errorString().isEmpty(), 10000);
            QVERIFY(!previewVideo->ready());
            QCOMPARE(previewTimestamp->property("text").toString(),
                     QStringLiteral("00:00:%1").arg(timestampSeconds, 2, 10, QLatin1Char('0')));
            QCOMPARE(seekRequests.count(), 0);
            QVERIFY(qAbs(video->position() - pausedPosition) < 0.001);
        }
        // Hovering the padded ends clamps previews to the media's actual range.
        QTest::mouseMove(window, seek->mapToScene({1, seek->height() / 2}).toPoint(), 20);
        QTRY_COMPARE(seek->property("previewTime").toDouble(), rangeStart);
        QTest::mouseMove(window,
                        seek->mapToScene({seek->width() - 1, seek->height() / 2}).toPoint(), 20);
        QTRY_COMPARE(seek->property("previewTime").toDouble(), rangeEnd);
        QCOMPARE(seekRequests.count(), 0);
        QVERIFY(qAbs(video->position() - pausedPosition) < 0.001);
        QTest::mouseMove(window, stageCenter, 20);
        QTRY_VERIFY(!seek->property("previewVisible").toBool());

        const QPoint dragStart = seek->mapToScene({seek->width() / 6, seek->height() / 2}).toPoint();
        const QPoint dragEnd = seek->mapToScene({seek->width() * 0.8, seek->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(window, dragEnd, 20);
        QCOMPARE(seekRequests.count(), 0);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        QCOMPARE(seekRequests.count(), 1);
        QTRY_VERIFY(video->position() > 22 && video->position() < 25);

        click(window, item("nextButton"), Qt::RightButton);
        QTRY_COMPARE(playlist->currentIndex(), 1);
        QTRY_VERIFY(video->playing() && video->seekable());
        QCOMPARE(video->source(), second);
        // Skipping past the last track does not disturb playback.
        click(window, item("nextButton"), Qt::RightButton);
        QCOMPARE(playlist->currentIndex(), 1);
        click(window, item("previousButton"), Qt::RightButton);
        QTRY_COMPARE(playlist->currentIndex(), 0);
        QTRY_VERIFY(video->seekable());
        QCOMPARE(video->source(), first);

        // End-of-file advances once, then stops at the end of the playlist.
        video->seek(29.5);
        QTRY_COMPARE_WITH_TIMEOUT(playlist->currentIndex(), 1, 5000);
        QTRY_VERIFY(video->seekable());
        video->seek(29.5);
        QTRY_VERIFY_WITH_TIMEOUT(video->stopped(), 5000);
        QCOMPARE(playlist->currentIndex(), 1);

        click(window, item("playButton"));
        QTRY_VERIFY(video->seekable());
        click(window, item("stopButton"));
        QVERIFY(video->stopped());
        QCOMPARE(video->position(), 0.0);
        QVERIFY(!video->seekable());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty()
                 ? QString{} : warnings.first().toString()));
    }
};

int main(int argc, char **argv)
{
    // File dialogs also use Quick Controls internally; match the application's
    // explicit Basic imports instead of inheriting the desktop's global style.
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    // Keep dialog interaction in Qt's event loop during automated tests.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QGuiApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QCoreApplication::setOrganizationName("KPlayerTests");
    QCoreApplication::setApplicationName("KPlayerTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    std::setlocale(LC_NUMERIC, "C");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    // Optional read-only preview of the private test display. Native activation
    // and shortcut checks still run normally inside the isolated compositor.
    const QString mirrorPath = qEnvironmentVariable("KPLAYER_TEST_MIRROR");
    QSharedPointer<QQuickItemGrabResult> mirrorGrab;
    QPointer<QQuickWindow> mirrorWindow;
    QMetaObject::Connection mirrorReady;
    quint64 mirrorRequest = 0;
    bool mirrorPending = false;
    QObject mirrorContext;
    QTimer mirrorTimer;
    if (!mirrorPath.isEmpty()) {
        QObject::connect(&mirrorTimer, &QTimer::timeout, &mirrorContext, [&] {
            if (mirrorPending && mirrorWindow)
                return;
            QObject::disconnect(mirrorReady);
            mirrorGrab.reset();
            mirrorPending = false;
            auto *window = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow());
            if (!window || !window->isExposed())
                return;
            // Capture the existing scene asynchronously rather than re-entering
            // QQuickWindow rendering during QtTest's event-loop waits.
            mirrorGrab = window->contentItem()->grabToImage();
            if (!mirrorGrab)
                return;
            mirrorWindow = window;
            mirrorPending = true;
            const quint64 request = ++mirrorRequest;
            mirrorReady = QObject::connect(mirrorGrab.data(), &QQuickItemGrabResult::ready,
                                           &mirrorContext, [&, request] {
                if (request != mirrorRequest || !mirrorGrab)
                    return;
                QSaveFile file(mirrorPath);
                if (file.open(QIODevice::WriteOnly) && mirrorGrab->image().save(&file, "PNG"))
                    file.commit();
                mirrorPending = false;
            });
        });
        mirrorTimer.start(1000);
    }
    PlayerTests tests;
    const int result = QTest::qExec(&tests, argc, argv);
    mirrorTimer.stop();
    QObject::disconnect(mirrorReady);
    mirrorGrab.reset();
    return result;
}

#include "tst_player.moc"
