#include "Favorites.h"
#include "PlaylistModel.h"

#include <QSettings>

Favorites::Favorites(QObject *parent) : QObject(parent)
{
    const QStringList saved = QSettings().value(QStringLiteral("favorites/sources")).toStringList();
    for (const QString &value : saved) {
        const QUrl source(value);
        // Retain missing local files so removable drives do not erase favorites.
        if (source.isValid() && !source.isEmpty()
            && (source.isLocalFile() || ((source.scheme() == "https" || source.scheme() == "http")
                                         && !source.host().isEmpty()))
            && !contains(source))
            m_sources.append(source);
    }
}

QVariantList Favorites::entries() const
{
    QVariantList result;
    for (const QUrl &source : m_sources) {
        QString title = source.fileName(QUrl::FullyDecoded);
        if (title.isEmpty())
            title = source.toDisplayString();
        result.append(QVariantMap{{"source", source}, {"title", title}});
    }
    return result;
}

int Favorites::indexOf(const QUrl &source) const
{
    const QString key = PlaylistModel::sourceKey(source);
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources.at(i) == source || PlaylistModel::sourceKey(m_sources.at(i)) == key)
            return i;
    }
    return -1;
}

bool Favorites::contains(const QUrl &source) const
{
    return indexOf(source) >= 0;
}

bool Favorites::toggle(const QUrl &source)
{
    const int index = indexOf(source);
    if (index >= 0) {
        m_sources.removeAt(index);
    } else {
        PlaylistModel validated;
        if (!validated.appendUrls({source}))
            return false;
        m_sources.append(validated.currentSource());
    }
    save();
    return true;
}

void Favorites::clear()
{
    if (m_sources.isEmpty())
        return;
    m_sources.clear();
    save();
}

void Favorites::save()
{
    QStringList saved;
    for (const QUrl &source : m_sources)
        saved.append(source.toString(QUrl::FullyEncoded));
    QSettings().setValue(QStringLiteral("favorites/sources"), saved);
    emit entriesChanged();
}
