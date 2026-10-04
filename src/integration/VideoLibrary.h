#pragma once

#include "PlaylistModel.h"

#include <QObject>
#include <QThread>
#include <QtQml/qqmlregistration.h>

class VideoLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int addedCount READ addedCount NOTIFY progressChanged)
    Q_PROPERTY(int foldersVisited READ foldersVisited NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)

public:
    explicit VideoLibrary(QObject *parent = nullptr) : QObject(parent) {}
    ~VideoLibrary() override;
    bool scanning() const { return m_scanning; }
    int addedCount() const { return m_addedCount; }
    int foldersVisited() const { return m_foldersVisited; }
    QString status() const;
    static QList<QUrl> defaultRoots();
    Q_INVOKABLE bool scan(PlaylistModel *playlist, const QList<QUrl> &roots = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE int addSiblingVideos(PlaylistModel *playlist, const QList<QUrl> &files);

signals:
    void scanningChanged();
    void progressChanged();
    void finished(bool canceled);

private:
    QThread *m_worker = nullptr;
    bool m_scanning = false;
    bool m_canceled = false;
    bool m_hasScanned = false;
    int m_addedCount = 0;
    int m_foldersVisited = 0;
};
