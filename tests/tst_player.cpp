#include "MpvVideo.h"
#include "MpvPreview.h"
#include "PlaylistModel.h"
#include "Favorites.h"
#include "VideoLibrary.h"
#include "FileAssociations.h"
#include "VideoFormats.h"

#include <QAbstractItemModelTester>
#include <QDataStream>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
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

#include <clocale>

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
            auto *codes = findItem(window->contentItem(), "subtitleLanguageCodes");
            QVERIFY(codes);
            click(window, codes);
            QTest::keyClick(window, Qt::Key_X);
            QTest::keyClick(window, Qt::Key_C);
            QTest::keyClick(window, Qt::Key_Z);
            QCOMPARE(codes->property("text").toString(), QStringLiteral("xcz"));
            QCOMPARE(video->speed(), 1.0);
            codes->setProperty("text", " JA, JPN ");
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
            QTRY_COMPARE(video->defaultSubtitleLanguage(), QStringLiteral("ja,jpn"));
            QTest::keyClick(window, Qt::Key_F5);
            QTRY_VERIFY(dialog->property("opened").toBool());
            QCOMPARE(codes->property("text").toString(), QStringLiteral("ja,jpn"));
            codes->setProperty("text", "en");
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!dialog->property("visible").toBool());
            QCOMPARE(video->defaultSubtitleLanguage(), QStringLiteral("ja,jpn"));
        }
        {
            QQmlApplicationEngine engine;
            engine.loadFromModule("KPlayer", "Main");
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *video = engine.rootObjects().first()->findChild<MpvVideo *>();
            QVERIFY(video);
            QCOMPARE(video->defaultSubtitleLanguage(), QStringLiteral("ja,jpn"));
        }
        QSettings().clear();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString{} : warnings.first().toString()));
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
        video->setSubtitleTrack(english);
        QTRY_COMPARE(video->subtitleTrack(), english);
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), japanese);
        video->setDefaultSubtitleLanguage("en,eng");
        video->stop();
        video->play();
        QTRY_VERIFY(video->mediaLoaded());
        QTRY_COMPARE(video->subtitleTrack(), english);
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
        video->setSpeed(5.0);
        QTest::keyClick(window, Qt::Key_C);
        QTRY_COMPARE(video->speed(), 5.0);
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
        QCOMPARE(menu->property("count").toInt(), 7);
        QVERIFY(menuItemAt(menu, 5)->property("checked").toBool());
        click(window, menuItemAt(menu, 6));
        QTRY_COMPARE(video->subtitleTrack(), secondId);
        video->setSubtitleTrack(-1);
        QTRY_COMPARE(video->subtitleTrack(), -1);
        video->setSubtitlesVisible(false);
        video->setSubtitleTrack(firstId);
        QTRY_COMPARE(video->subtitleTrack(), firstId);
        QTRY_VERIFY(video->subtitlesVisible());

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

    void videoDiscoverySearchesRecursivelyAndCanRescan()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDir root(directory.path());
        QVERIFY(root.mkpath("nested/deep"));
        QVERIFY(root.mkpath(".hidden"));
        QVERIFY(root.mkpath("directory.mp4"));
        const QUrl first = makeVideo(root.filePath("first.y4m"));
        const QUrl second = makeVideo(root.filePath("nested/deep/第二部.MP4"));
        const QUrl third = makeVideo(root.filePath(".hidden/third.mkv"));
        makeAudio(root.filePath("audio.wav"));
        makeAudio(root.filePath("nested/readme.txt"));
        PlaylistModel playlist;
        playlist.appendUrls({second});
        VideoLibrary library;
        QSignalSpy changes(&playlist, &PlaylistModel::currentSourceChanged);
        QSignalSpy finished(&library, &VideoLibrary::finished);
        const QList<QUrl> roots = {QUrl::fromLocalFile(root.path()),
            QUrl::fromLocalFile(root.filePath("nested")),
            QUrl::fromLocalFile(root.filePath("missing")), QUrl("https://example.com/")};
        QVERIFY(library.scan(&playlist, roots));
        QVERIFY(library.scanning());
        QVERIFY(!library.scan(&playlist, roots));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!library.scanning());
        QCOMPARE(library.addedCount(), 2);
        QVERIFY(library.status().isEmpty());
        QCOMPARE(playlist.count(), 3);
        QVERIFY(playlist.indexOfSource(first) >= 0);
        QVERIFY(playlist.indexOfSource(third) >= 0);
        QCOMPARE(playlist.currentSource(), second);
        QCOMPARE(changes.size(), 0);
        QVERIFY(library.scan(&playlist, roots));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QCOMPARE(library.addedCount(), 0);
        QCOMPARE(playlist.count(), 3);
    }

    void videoDiscoveryCancellationAndDeletedTargetAreSafe()
    {
        QTemporaryDir directory;
        makeVideo(directory.filePath("movie.y4m"));
        const QList<QUrl> roots = {QUrl::fromLocalFile(directory.path())};
        PlaylistModel playlist;
        VideoLibrary library;
        QSignalSpy finished(&library, &VideoLibrary::finished);
        QVERIFY(library.scan(&playlist, roots));
        library.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(finished.first().first().toBool(), true);
        QCOMPARE(playlist.count(), 0);
        auto *temporaryPlaylist = new PlaylistModel;
        QVERIFY(library.scan(temporaryPlaylist, roots));
        delete temporaryPlaylist;
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QVERIFY(!library.scanning());
        QVERIFY(library.scan(&playlist, roots));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 5000);
        QCOMPARE(playlist.count(), 1);
    }

    void videoDiscoveryRejectsTypeScriptButKeepsTransportStreams()
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
        QSignalSpy finished(&library, &VideoLibrary::finished);
        QVERIFY(library.scan(&playlist, {QUrl::fromLocalFile(directory.path())}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(playlist.count(), 3);
        QCOMPARE(playlist.indexOfSource(QUrl::fromLocalFile(directory.filePath("source.ts"))), -1);
        QCOMPARE(playlist.indexOfSource(QUrl::fromLocalFile(directory.filePath("source.mts"))), -1);
    }

    void explorerStartupPlaysSelectedFileAndDiscoversOthers()
    {
        QTemporaryDir directory;
        const QUrl first = makeVideo(directory.filePath("第一部 video.y4m"));
        const QUrl second = makeVideo(directory.filePath("second.y4m"));
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&](const QList<QQmlError> &items) { warnings.append(items); });
        engine.setInitialProperties({
            {"startupFiles", QVariant::fromValue(QList<QUrl>{first})},
            {"discoverVideosOnStartup", true},
            {"videoSearchRoots", QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(directory.path())})}
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
        QTRY_VERIFY(!library->scanning());
        QCOMPARE(playlists->defaultPlaylist()->count(), 2);
        QCOMPARE(playlists->defaultPlaylist()->currentSource(), first);
        QCOMPARE(video->source(), first);
        QCOMPARE(video->loadRevision(), quint64(1));
        QVERIFY(playlists->defaultPlaylist()->indexOfSource(second) >= 0);
        QCOMPARE(playlists->addPlaylist("Other"), 1);
        QVERIFY(library->scan(playlists->defaultPlaylist(),
                             {QUrl::fromLocalFile(directory.path())}));
        QTRY_VERIFY(!library->scanning());
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
        QTRY_COMPARE(video->source(), first);
        QVERIFY(video->stopped());
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
        QCOMPARE(video->source(), first);
        QVERIFY(video->stopped());
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
        const QUrl first = makeAudio(directory.filePath("first media.wav"));
        const QUrl second = makeAudio(directory.filePath(QString::fromUtf8("第二首.wav")));
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *panel = window->findChild<QQuickItem *>("playlistPanel");
        auto *playlist = window->findChild<PlaylistModel *>();
        QVERIFY(panel && playlist);
        const QPoint point = panel->mapToScene({panel->width() / 2, panel->height() / 2}).toPoint();
        QMimeData files;
        files.setUrls({first, second, QUrl::fromLocalFile(directory.path())});
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
        auto *button = findItem(window->contentItem(), QStringLiteral("playlistButton"));
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
        QCOMPARE(menu->property("width").toReal(), 193.0);
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
        click(window, stage);
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
        click(window, item("playlistButton"));
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
        QTest::keyClick(window, Qt::Key_M);
        QTRY_COMPARE(video->muted(), !muted);
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
        QTest::keyClick(window, Qt::Key_F11);
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

        auto *settings = window->findChild<QObject *>("playbackSettingsMenu");
        auto *appMenu = window->findChild<QObject *>("appMenu");
        QVERIFY(settings && appMenu);
        click(window, item("settingsButton"));
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

        click(window, item("settingsButton"));
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

        click(window, item("settingsButton"));
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
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(exposeWindow(window));
        auto *video = window->findChild<MpvVideo *>();
        auto *hint = findItem(window->contentItem(), "playbackHint");
        QVERIFY(video && hint);
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
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreen"));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QTRY_VERIFY(title->isVisible());
        QTRY_COMPARE(window->size(), normalSize);

        window->showMaximized();
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreenPlus"));
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
            QCOMPARE(video->position(), pausedPosition);
        }
        // Hovering the padded ends clamps previews to the media's actual range.
        QTest::mouseMove(window, seek->mapToScene({1, seek->height() / 2}).toPoint(), 20);
        QTRY_COMPARE(seek->property("previewTime").toDouble(), rangeStart);
        QTest::mouseMove(window,
                        seek->mapToScene({seek->width() - 1, seek->height() / 2}).toPoint(), 20);
        QTRY_COMPARE(seek->property("previewTime").toDouble(), rangeEnd);
        QCOMPARE(seekRequests.count(), 0);
        QCOMPARE(video->position(), pausedPosition);
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
    PlayerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_player.moc"
