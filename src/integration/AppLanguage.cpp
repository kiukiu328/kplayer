#include "AppLanguage.h"

#include <QCoreApplication>
#include <QQmlEngine>
#include <QSettings>

AppLanguage::AppLanguage(QObject *parent) : QObject(parent)
{
    const QString saved = QSettings().value("interface/language", "zh_TW").toString();
    setLanguage(saved == "en" ? saved : QStringLiteral("zh_TW"));
}

AppLanguage::~AppLanguage()
{
    if (m_translator)
        QCoreApplication::removeTranslator(m_translator.get());
}

AppLanguage *AppLanguage::create(QQmlEngine *engine, QJSEngine *)
{
    auto *language = new AppLanguage;
    connect(language, &AppLanguage::languageChanged, engine, &QQmlEngine::retranslate);
    return language;
}

void AppLanguage::setLanguage(const QString &language)
{
    if ((language != "en" && language != "zh_TW") || language == m_language)
        return;
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/kplayer_%1.qm").arg(language))) {
        qWarning("Could not load KPlayer translation");
        return;
    }
    if (m_translator)
        QCoreApplication::removeTranslator(m_translator.get());
    m_translator = std::move(translator);
    QCoreApplication::installTranslator(m_translator.get());
    m_language = language;
    QSettings().setValue("interface/language", language);
    emit languageChanged();
}
