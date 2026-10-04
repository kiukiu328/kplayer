#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Favorites : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)

public:
    explicit Favorites(QObject *parent = nullptr);
    QVariantList entries() const;
    Q_INVOKABLE bool contains(const QUrl &source) const;
    Q_INVOKABLE bool toggle(const QUrl &source);
    Q_INVOKABLE void clear();

signals:
    void entriesChanged();

private:
    int indexOf(const QUrl &source) const;
    void save();
    QList<QUrl> m_sources;
};
