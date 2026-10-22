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
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentSourceChanged)
    Q_PROPERTY(QString currentFormat READ currentFormat NOTIFY currentSourceChanged)

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
    QString currentTitle() const;
    QString currentFormat() const;
    Q_INVOKABLE int appendUrls(const QList<QUrl> &urls);
    Q_INVOKABLE int appendUniqueUrls(const QList<QUrl> &urls);
    Q_INVOKABLE int indexOfSource(const QUrl &url) const;
    Q_INVOKABLE bool removeAt(int index);
    Q_INVOKABLE bool moveTrack(int from, int to);
    Q_INVOKABLE QList<int> moveTracks(const QList<int> &rows, int destination);
    Q_INVOKABLE int sortByTitle(bool ascending = true, int selectedIndex = -1);
    Q_INVOKABLE int sortSelectedByTitle(const QList<int> &rows, bool ascending = true,
                                      int selectedIndex = -1);
    Q_INVOKABLE void clear();
    static QString sourceKey(const QUrl &url);

signals:
    void countChanged();
    void currentIndexChanged();
    void currentSourceChanged();

private:
    friend class PlaylistCollection;
    int appendUrlsImpl(const QList<QUrl> &urls, bool unique);
    int sortRowsByTitle(const QList<int> &rows, bool ascending, int selectedIndex);
    void applyRowOrder(const QList<int> &order);
    struct Track { QUrl source; QString title; QString key; };
    QList<Track> m_tracks;
    QHash<QString, int> m_sourceCounts;
    int m_currentIndex = -1;
};
