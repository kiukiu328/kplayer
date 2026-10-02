#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QUrl currentSource READ currentSource NOTIFY currentSourceChanged)

public:
    explicit PlaylistModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    enum Role { TitleRole = Qt::UserRole + 1, SourceRole };
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_tracks.size()); }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    QUrl currentSource() const;
    Q_INVOKABLE int appendUrls(const QList<QUrl> &urls);

signals:
    void countChanged();
    void currentIndexChanged();
    void currentSourceChanged();

private:
    struct Track { QUrl source; QString title; };
    QList<Track> m_tracks;
    int m_currentIndex = -1;
};
