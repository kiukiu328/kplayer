#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
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
    Q_INVOKABLE bool removeAt(int index);
    Q_INVOKABLE bool moveTrack(int from, int to);
    Q_INVOKABLE int sortByTitle(bool ascending = true, int selectedIndex = -1);
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void currentIndexChanged();
    void currentSourceChanged();

private:
    struct Track { QUrl source; QString title; };
    QList<Track> m_tracks;
    int m_currentIndex = -1;
};

class PlaylistCollection : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(PlaylistModel *currentPlaylist READ currentPlaylist NOTIFY currentPlaylistChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentNameChanged)

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
    QString currentName() const { return m_playlists.at(m_currentIndex).name; }
    Q_INVOKABLE bool nameAvailable(const QString &name, int exceptIndex = -1) const;
    Q_INVOKABLE int addPlaylist(const QString &name);
    Q_INVOKABLE bool renamePlaylist(int index, const QString &name);
    Q_INVOKABLE bool removePlaylist(int index);

signals:
    void countChanged();
    void currentIndexChanged();
    void currentPlaylistChanged();
    void currentNameChanged();

private:
    struct Playlist { QString name; PlaylistModel *model; };
    QList<Playlist> m_playlists;
    int m_currentIndex = 0;
};

// Filtering changes the view only; playback always uses the original queue.
class PlaylistFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PlaylistModel *playlist READ playlist WRITE setPlaylist NOTIFY playlistChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)

public:
    explicit PlaylistFilterModel(QObject *parent = nullptr);
    PlaylistModel *playlist() const;
    void setPlaylist(PlaylistModel *playlist);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    Q_INVOKABLE int sourceIndex(int filteredRow) const;
    Q_INVOKABLE int filteredIndex(int sourceRow) const;

signals:
    void playlistChanged();
    void searchTextChanged();

private:
    QString m_searchText;
};
