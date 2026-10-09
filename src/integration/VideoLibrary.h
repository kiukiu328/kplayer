#pragma once

#include "PlaylistModel.h"

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
};
