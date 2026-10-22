#include "PlaylistFilterModel.h"

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

QUrl PlaylistFilterModel::sourceAt(int filteredRow) const
{
    return data(index(filteredRow, 0), PlaylistModel::SourceRole).toUrl();
}
