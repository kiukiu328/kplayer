#include "PlaylistCollection.h"

#include <QSettings>

#include <algorithm>

PlaylistCollection::PlaylistCollection(QObject *parent) : QAbstractListModel(parent)
{
    auto *playlist = new PlaylistModel(this);
    playlist->setObjectName(QStringLiteral("playlistModel"));
    m_playlists.append({tr("預設"), playlist});
    watchPlaylist(playlist);
}

void PlaylistCollection::setPersistent(bool persistent)
{
    if (m_persistent == persistent)
        return;
    m_persistent = persistent;
    if (persistent) {
        loadPlaylists();
        savePlaylists();
    }
    emit persistentChanged();
}

void PlaylistCollection::watchPlaylist(PlaylistModel *model)
{
    connect(model, &PlaylistModel::countChanged, this, &PlaylistCollection::savePlaylists);
    connect(model, &PlaylistModel::currentIndexChanged, this, &PlaylistCollection::savePlaylists);
    connect(model, &QAbstractItemModel::rowsMoved, this, &PlaylistCollection::savePlaylists);
    connect(model, &QAbstractItemModel::modelReset, this, &PlaylistCollection::savePlaylists);
}

void PlaylistCollection::savePlaylists() const
{
    if (!m_persistent || m_loading)
        return;
    QSettings settings;
    QVariantList entries;
    for (int index = 0; index < count(); ++index) {
        const auto &playlist = m_playlists.at(index);
        QStringList sources;
        for (const auto &track : playlist.model->m_tracks)
            sources.append(track.source.toString(QUrl::FullyEncoded));
        const QVariantMap entry{{"name", playlist.name}, {"sources", sources},
                                {"currentIndex", playlist.model->currentIndex()}};
        if (index == 0)
            settings.setValue("playlists/default", entry);
        else
            entries.append(entry);
    }
    settings.setValue("playlists/entries", entries);
    settings.setValue("playlists/currentIndex", m_currentIndex);
}

void PlaylistCollection::loadPlaylists()
{
    m_loading = true;
    QSettings settings;
    QVariantList entries = settings.value("playlists/entries").toList();
    entries.prepend(settings.value("playlists/default").toMap());
    for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
        const QVariantMap values = entries.at(entryIndex).toMap();
        auto *model = defaultPlaylist();
        if (entryIndex > 0) {
            if (addPlaylist(values.value("name").toString()) < 0)
                continue;
            model = currentPlaylist();
        }
        QList<PlaylistModel::Track> tracks;
        for (const QString &encoded : values.value("sources").toStringList()) {
            const QUrl source(encoded);
            if (!source.isValid() || source.isEmpty()
                || (!source.isLocalFile() && !((source.scheme() == "http" || source.scheme() == "https")
                                              && !source.host().isEmpty())))
                continue;
            QString title = source.fileName(QUrl::FullyDecoded);
            if (title.isEmpty())
                title = source.host();
            tracks.append({source, title, PlaylistModel::sourceKey(source)});
        }
        if (!tracks.isEmpty()) {
            model->beginInsertRows({}, 0, static_cast<int>(tracks.size()) - 1);
            model->m_tracks = tracks;
            for (const auto &track : tracks)
                ++model->m_sourceCounts[track.key];
            model->endInsertRows();
            emit model->countChanged();
            model->setCurrentIndex(std::clamp(values.value("currentIndex").toInt(), 0, model->count() - 1));
        }
    }
    setCurrentIndex(std::clamp(settings.value("playlists/currentIndex", 0).toInt(), 0, count() - 1));
    m_loading = false;
}

int PlaylistCollection::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant PlaylistCollection::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    return role == Qt::DisplayRole || role == NameRole
        ? QVariant(m_playlists.at(index.row()).name) : QVariant{};
}

QHash<int, QByteArray> PlaylistCollection::roleNames() const
{
    return {{NameRole, "name"}};
}

void PlaylistCollection::setCurrentIndex(int index)
{
    if (index < 0 || index >= count() || index == m_currentIndex)
        return;
    m_currentIndex = index;
    emit currentIndexChanged();
    emit currentPlaylistChanged();
    emit currentNameChanged();
    savePlaylists();
}

bool PlaylistCollection::nameAvailable(const QString &name, int exceptIndex) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    for (int index = 0; index < count(); ++index) {
        if (index != exceptIndex
            && m_playlists.at(index).name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return false;
    }
    return true;
}

int PlaylistCollection::addPlaylist(const QString &name)
{
    if (!nameAvailable(name))
        return -1;
    const int index = count();
    beginInsertRows({}, index, index);
    m_playlists.append({name.trimmed(), new PlaylistModel(this)});
    watchPlaylist(m_playlists.last().model);
    endInsertRows();
    emit countChanged();
    setCurrentIndex(index);
    savePlaylists();
    return index;
}

int PlaylistCollection::saveCurrentPlaylistAs(const QString &name)
{
    if (!nameAvailable(name))
        return -1;
    const auto *source = currentPlaylist();
    auto *copy = new PlaylistModel(this);
    copy->m_tracks = source->m_tracks;
    copy->m_sourceCounts = source->m_sourceCounts;
    copy->m_currentIndex = source->m_currentIndex;
    const int index = count();
    beginInsertRows({}, index, index);
    m_playlists.append({name.trimmed(), copy});
    watchPlaylist(copy);
    endInsertRows();
    emit countChanged();
    setCurrentIndex(index);
    savePlaylists();
    return index;
}

bool PlaylistCollection::renamePlaylist(int index, const QString &name)
{
    if (index <= 0 || index >= count() || !nameAvailable(name, index))
        return false;
    const QString trimmed = name.trimmed();
    if (m_playlists.at(index).name == trimmed)
        return true;
    m_playlists[index].name = trimmed;
    emit dataChanged(this->index(index), this->index(index), {Qt::DisplayRole, NameRole});
    if (m_currentIndex == index)
        emit currentNameChanged();
    savePlaylists();
    return true;
}

bool PlaylistCollection::removePlaylist(int index)
{
    if (index <= 0 || index >= count())
        return false;
    // Rebind QML consumers before releasing the selected queue.
    if (index == m_currentIndex)
        setCurrentIndex(index - 1);
    auto *removed = m_playlists.at(index).model;
    emit playlistAboutToBeRemoved(removed);
    const bool shifted = index < m_currentIndex;
    beginRemoveRows({}, index, index);
    m_playlists.removeAt(index);
    if (shifted)
        --m_currentIndex;
    endRemoveRows();
    emit countChanged();
    if (shifted)
        emit currentIndexChanged();
    removed->deleteLater();
    savePlaylists();
    return true;
}
