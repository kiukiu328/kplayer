#include "PlaylistModel.h"

#include <QFileInfo>
#include <QCollator>

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

int PlaylistModel::appendUrls(const QList<QUrl> &urls)
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
            title = url.fileName(QUrl::FullyDecoded);
            if (title.isEmpty())
                title = url.host();
        } else {
            continue;
        }
        tracks.append({source, title});
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

int PlaylistModel::sortByTitle(bool ascending, int selectedIndex)
{
    QList<int> order(count());
    std::iota(order.begin(), order.end(), 0);
    QCollator collator;
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
    m_currentIndex = -1;
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
    emit currentSourceChanged();
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
