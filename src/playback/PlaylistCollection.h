#pragma once

#include "PlaylistModel.h"

class PlaylistCollection : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(PlaylistModel *currentPlaylist READ currentPlaylist NOTIFY currentPlaylistChanged)
    Q_PROPERTY(PlaylistModel *defaultPlaylist READ defaultPlaylist CONSTANT)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentNameChanged)
    Q_PROPERTY(bool persistent READ persistent WRITE setPersistent NOTIFY persistentChanged)

public:
    explicit PlaylistCollection(QObject *parent = nullptr);

    enum Role { NameRole = Qt::UserRole + 1 };
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_playlists.size()); }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    PlaylistModel *currentPlaylist() const { return m_playlists.at(m_currentIndex).model; }
    PlaylistModel *defaultPlaylist() const { return m_playlists.first().model; }
    QString currentName() const { return m_playlists.at(m_currentIndex).name; }
    bool persistent() const { return m_persistent; }
    void setPersistent(bool persistent);
    Q_INVOKABLE bool nameAvailable(const QString &name, int exceptIndex = -1) const;
    Q_INVOKABLE int addPlaylist(const QString &name);
    Q_INVOKABLE int saveCurrentPlaylistAs(const QString &name);
    Q_INVOKABLE bool renamePlaylist(int index, const QString &name);
    Q_INVOKABLE bool removePlaylist(int index);

signals:
    void countChanged();
    void currentIndexChanged();
    void currentPlaylistChanged();
    void playlistAboutToBeRemoved(PlaylistModel *playlist);
    void currentNameChanged();
    void persistentChanged();

private:
    struct Playlist { QString name; PlaylistModel *model; };
    QList<Playlist> m_playlists;
    int m_currentIndex = 0;
    bool m_persistent = false;
    bool m_loading = false;
    void watchPlaylist(PlaylistModel *model);
    void savePlaylists() const;
    void loadPlaylists();
};
