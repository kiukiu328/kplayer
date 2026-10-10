#include "PlaylistModel.h"

#include <QFileInfo>
#include <QCollator>
#include <QDir>
#include <QSettings>

#include <algorithm>
#include <numeric>

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const auto &track = m_tracks.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return track.title;
    case SourceRole: return track.source;
    default: return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {{TitleRole, "title"}, {SourceRole, "source"}};
}

void PlaylistModel::setCurrentIndex(int index)
{
    if (index < 0 || index >= count() || m_currentIndex == index)
        return;
    m_currentIndex = index;
    emit currentIndexChanged();
    emit currentSourceChanged();
}

QUrl PlaylistModel::currentSource() const
{
    return m_currentIndex >= 0 ? m_tracks.at(m_currentIndex).source : QUrl{};
}

QString PlaylistModel::currentTitle() const
{
    return m_currentIndex >= 0 ? m_tracks.at(m_currentIndex).title : QString{};
}

QString PlaylistModel::currentFormat() const
{
    return QFileInfo(currentSource().fileName(QUrl::FullyDecoded)).suffix().toUpper();
}

int PlaylistModel::appendUrls(const QList<QUrl> &urls)
{
    return appendUrlsImpl(urls, false);
}

int PlaylistModel::appendUniqueUrls(const QList<QUrl> &urls)
{
    return appendUrlsImpl(urls, true);
}

QString PlaylistModel::sourceKey(const QUrl &url)
{
    if (!url.isLocalFile())
        return url.adjusted(QUrl::NormalizePathSegments).toString(QUrl::FullyEncoded);
    const QFileInfo file(url.toLocalFile());
    QString path = file.canonicalFilePath();
    if (path.isEmpty())
        path = file.absoluteFilePath();
    path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

int PlaylistModel::indexOfSource(const QUrl &url) const
{
    const QString key = sourceKey(url);
    for (int index = 0; index < count(); ++index) {
        if (m_tracks.at(index).source == url || m_tracks.at(index).key == key)
            return index;
    }
    return -1;
}

int PlaylistModel::appendUrlsImpl(const QList<QUrl> &urls, bool unique)
{
    QList<Track> tracks;
    for (const QUrl &url : urls) {
        if (!url.isValid() || url.isEmpty())
            continue;
        QString title;
        QUrl source = url;
        if (url.isLocalFile()) {
            const QFileInfo file(url.toLocalFile());
            if (!file.isFile())
                continue;
            source = QUrl::fromLocalFile(file.absoluteFilePath());
            title = file.fileName();
        } else if (url.scheme() == QStringLiteral("http")
                   || url.scheme() == QStringLiteral("https")) {
            if (url.host().isEmpty())
                continue;
            title = url.fileName(QUrl::FullyDecoded);
            if (title.isEmpty())
                title = url.host();
        } else {
            continue;
        }
        const QString key = sourceKey(source);
        if (unique && m_sourceCounts.contains(key))
            continue;
        ++m_sourceCounts[key];
        tracks.append({source, title, key});
    }
    if (tracks.isEmpty())
        return 0;
    const int added = static_cast<int>(tracks.size());
    beginInsertRows({}, count(), count() + added - 1);
    m_tracks.append(tracks);
    endInsertRows();
    emit countChanged();
    if (m_currentIndex == -1)
        setCurrentIndex(0);
    return added;
}

bool PlaylistModel::removeAt(int index)
{
    if (index < 0 || index >= count())
        return false;
    const int previousIndex = m_currentIndex;
    const QUrl previousSource = currentSource();
    beginRemoveRows({}, index, index);
    const QString key = m_tracks.at(index).key;
    if (--m_sourceCounts[key] == 0)
        m_sourceCounts.remove(key);
    m_tracks.removeAt(index);
    if (m_tracks.isEmpty())
        m_currentIndex = -1;
    else if (index < m_currentIndex)
        --m_currentIndex;
    else if (index == m_currentIndex)
        m_currentIndex = std::min(index, count() - 1);
    endRemoveRows();
    emit countChanged();
    if (previousIndex != m_currentIndex)
        emit currentIndexChanged();
    if (previousSource != currentSource())
        emit currentSourceChanged();
    return true;
}

bool PlaylistModel::moveTrack(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to)
        return false;
    const int previousIndex = m_currentIndex;
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to))
        return false;
    m_tracks.move(from, to);
    if (m_currentIndex == from)
        m_currentIndex = to;
    else if (from < m_currentIndex && to >= m_currentIndex)
        --m_currentIndex;
    else if (from > m_currentIndex && to <= m_currentIndex)
        ++m_currentIndex;
    endMoveRows();
    if (previousIndex != m_currentIndex)
        emit currentIndexChanged();
    return true;
}

// Destination is an insertion boundary in the original queue (0 through count).
QList<int> PlaylistModel::moveTracks(const QList<int> &rows, int destination)
{
    QList<int> selected = rows;
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    if (selected.isEmpty() || selected.first() < 0 || selected.last() >= count()
        || destination < 0 || destination > count())
        return {};
    QList<int> order;
    for (int row = 0; row < count(); ++row) {
        if (!std::binary_search(selected.begin(), selected.end(), row))
            order.append(row);
    }
    const int insertion = destination - static_cast<int>(
        std::lower_bound(selected.begin(), selected.end(), destination) - selected.begin());
    QList<int> result;
    for (int i = 0; i < selected.size(); ++i) {
        order.insert(insertion + i, selected.at(i));
        result.append(insertion + i);
    }
    if (std::is_sorted(order.begin(), order.end()))
        return result;
    QList<Track> reordered;
    for (int row : order)
        reordered.append(m_tracks.at(row));
    const int previousIndex = m_currentIndex;
    beginResetModel();
    m_tracks = std::move(reordered);
    m_currentIndex = static_cast<int>(order.indexOf(previousIndex));
    endResetModel();
    if (previousIndex != m_currentIndex)
        emit currentIndexChanged();
    return result;
}

int PlaylistModel::sortByTitle(bool ascending, int selectedIndex)
{
    QList<int> order(count());
    std::iota(order.begin(), order.end(), 0);
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        const int comparison = collator.compare(m_tracks.at(left).title, m_tracks.at(right).title);
        return ascending ? comparison < 0 : comparison > 0;
    });
    const int newSelection = static_cast<int>(order.indexOf(selectedIndex));
    if (std::is_sorted(order.begin(), order.end()))
        return newSelection;
    QList<Track> sorted;
    sorted.reserve(count());
    for (int index : order)
        sorted.append(m_tracks.at(index));
    const int previousIndex = m_currentIndex;
    beginResetModel();
    m_tracks = std::move(sorted);
    m_currentIndex = static_cast<int>(order.indexOf(previousIndex));
    endResetModel();
    if (previousIndex != m_currentIndex)
        emit currentIndexChanged();
    return newSelection;
}

void PlaylistModel::clear()
{
    if (m_tracks.isEmpty())
        return;
    beginResetModel();
    m_tracks.clear();
    m_sourceCounts.clear();
    m_currentIndex = -1;
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
    emit currentSourceChanged();
}

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

PlaylistFilterModel::PlaylistFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setFilterRole(PlaylistModel::TitleRole);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

PlaylistModel *PlaylistFilterModel::playlist() const
{
    return qobject_cast<PlaylistModel *>(sourceModel());
}

void PlaylistFilterModel::setPlaylist(PlaylistModel *playlist)
{
    if (sourceModel() == playlist)
        return;
    setSourceModel(playlist);
    emit playlistChanged();
}

void PlaylistFilterModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    setFilterFixedString(text.trimmed());
    emit searchTextChanged();
}

int PlaylistFilterModel::sourceIndex(int filteredRow) const
{
    return mapToSource(index(filteredRow, 0)).row();
}

int PlaylistFilterModel::filteredIndex(int sourceRow) const
{
    return sourceModel() ? mapFromSource(sourceModel()->index(sourceRow, 0)).row() : -1;
}

QStringList PlaylistFilterModel::titles() const
{
    QStringList result;
    result.reserve(rowCount());
    for (int row = 0; row < rowCount(); ++row)
        result.append(data(index(row, 0), PlaylistModel::TitleRole).toString());
    return result;
}
