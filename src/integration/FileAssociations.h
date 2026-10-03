#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class FileAssociations : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit FileAssociations(QObject *parent = nullptr) : QObject(parent) {}
    bool supported() const;
    QString errorString() const { return m_errorString; }
    // registryRoot allows isolated registration tests without changing real associations.
    static bool registerApplication(const QString &executable, const QString &registryRoot,
                                    QString *error = nullptr);
    Q_INVOKABLE bool registerVideoTypes();
    Q_INVOKABLE bool openDefaultApps();

signals:
    void errorStringChanged();

private:
    void setError(const QString &error);
    QString m_errorString;
};
