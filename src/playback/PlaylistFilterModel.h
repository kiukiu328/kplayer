#pragma once

#include "PlaylistModel.h"

#include <QSortFilterProxyModel>

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
    Q_INVOKABLE QStringList titles() const;
    Q_INVOKABLE QUrl sourceAt(int filteredRow) const;

signals:
    void playlistChanged();
    void searchTextChanged();

private:
    QString m_searchText;
};
