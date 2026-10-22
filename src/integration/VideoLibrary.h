#pragma once

#include "PlaylistCollection.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class VideoLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit VideoLibrary(QObject *parent = nullptr) : QObject(parent) {}
    // Discovery is limited to the immediate folders of explicitly opened files.
    Q_INVOKABLE int addSiblingVideos(PlaylistModel *playlist, const QList<QUrl> &files);
    Q_INVOKABLE int addFolderPlaylist(PlaylistCollection *playlists, const QUrl &folder);
    Q_INVOKABLE bool canDropUrls(const QList<QUrl> &urls) const;
    Q_INVOKABLE int addDroppedUrls(PlaylistCollection *playlists, const QList<QUrl> &urls);
    Q_INVOKABLE int addDroppedUrlsToPlaylist(PlaylistModel *playlist, const QList<QUrl> &urls);
    Q_INVOKABLE bool canOpenFileLocation(const QUrl &source) const;
    Q_INVOKABLE bool openFileLocation(const QUrl &source);
};
