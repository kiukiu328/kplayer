#pragma once

#include <QObject>
#include <QTranslator>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QQmlEngine;
class QJSEngine;

class AppLanguage : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    ~AppLanguage() override;
    static AppLanguage *create(QQmlEngine *engine, QJSEngine *);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);

signals:
    void languageChanged();

private:
    // Force QML to use create(), which connects live retranslation.
    explicit AppLanguage(QObject *parent = nullptr);
    QString m_language;
    std::unique_ptr<QTranslator> m_translator;
};
