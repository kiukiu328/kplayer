#include "PlaylistModel.h"

#include <QFileInfo>

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
