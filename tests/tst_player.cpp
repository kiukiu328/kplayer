#include "MpvVideo.h"
#include "MpvPreview.h"
#include "PlaylistModel.h"

#include <QAbstractItemModelTester>
#include <QDataStream>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
        QTest::mouseClick(window, button, Qt::NoModifier,
                         item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QCOMPARE(playlist.appendUrls({QUrl{}, QUrl("not-a-file")}), 0);
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QCOMPARE(openMedia->property("text").toString(), QString::fromUtf8("開啟檔案..."));
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

    void openMediaShortcutsOpenDialog()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("KPlayer", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto *dialog = window->findChild<QObject *>("mediaDialog");
        QVERIFY(dialog);
        QTest::keyClick(window, Qt::Key_F3);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(!window->property("playbackShortcutsEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
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
        QVERIFY(QTest::qWaitForWindowExposed(window));
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
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    PlayerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_player.moc"
