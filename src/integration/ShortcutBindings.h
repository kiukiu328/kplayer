#pragma once

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ShortcutBindings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY bindingsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY bindingsChanged)

public:
    explicit ShortcutBindings(QObject *parent = nullptr);

    QVariantList entries() const;
    int revision() const { return m_revision; }
    Q_INVOKABLE QStringList sequences(const QString &id) const;
    Q_INVOKABLE QString label(const QString &id) const;
    Q_INVOKABLE QVariantMap validate(const QVariantMap &draft) const;
    Q_INVOKABLE bool save(const QVariantMap &draft);
    Q_INVOKABLE QStringList parse(const QString &text) const;
    Q_INVOKABLE QString format(const QStringList &sequences) const;
    Q_INVOKABLE QString fromKeyEvent(int key, int modifiers) const;
    Q_INVOKABLE bool matches(const QString &id, int key, int modifiers) const;

signals:
    void bindingsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using BindingMap = QMap<QString, QStringList>;
    bool normalizedDraft(const QVariantMap &draft, BindingMap *result, QString *error) const;
    BindingMap m_bindings;
    int m_revision = 0;
};
