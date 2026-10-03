#include "FileAssociations.h"
#include "VideoFormats.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#endif

bool FileAssociations::supported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void FileAssociations::setError(const QString &error)
{
    if (m_errorString == error)
        return;
    m_errorString = error;
    emit errorStringChanged();
}

bool FileAssociations::registerApplication(const QString &executable,
                                         const QString &registryRoot, QString *error)
{
#ifdef Q_OS_WIN
    const QString path = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(path, QStringLiteral("%1"));
    const QString classes = registryRoot + QStringLiteral("\\Classes\\");
    const QString progId = QStringLiteral("KPlayer.Video");
    const QString appKey = classes + "Applications\\" + QFileInfo(path).fileName();
    const QString capabilities = registryRoot + QStringLiteral("\\KPlayer\\Capabilities");
    LONG failure = ERROR_SUCCESS;
    auto write = [&](const QString &key, const QString &name, const QString &value,
                     bool emptyValue = false) {
        if (failure != ERROR_SUCCESS)
            return;
        HKEY handle = nullptr;
        failure = RegCreateKeyExW(HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(key.utf16()), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &handle, nullptr);
        if (failure != ERROR_SUCCESS)
            return;
        failure = RegSetValueExW(handle, reinterpret_cast<LPCWSTR>(name.utf16()), 0,
            emptyValue ? REG_NONE : REG_SZ,
            emptyValue ? nullptr : reinterpret_cast<const BYTE *>(value.utf16()),
            emptyValue ? 0 : DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(handle);
    };
    write(classes + progId, {}, QStringLiteral("KPlayer video"));
    write(classes + progId + "\\DefaultIcon", {}, QStringLiteral("\"%1\",0").arg(path));
    write(classes + progId + "\\shell\\open\\command", {}, command);
    write(appKey, "FriendlyAppName", "KPlayer");
    write(appKey + "\\shell\\open\\command", {}, command);
    write(capabilities, "ApplicationName", "KPlayer");
    write(capabilities, "ApplicationDescription", "Play videos with KPlayer");
    write(capabilities, "ApplicationIcon", QStringLiteral("\"%1\",0").arg(path));
    for (const QString &type : VideoFormats::extensions()) {
        const QString extension = "." + type;
        write(classes + extension + "\\OpenWithProgids", progId, {}, true);
        write(appKey + "\\SupportedTypes", extension, {}, true);
        write(capabilities + "\\FileAssociations", extension, progId);
    }
    // Windows owns the default-app choice. Register handlers without editing UserChoice.
    write(registryRoot + "\\RegisteredApplications", "KPlayer", capabilities);
    if (failure != ERROR_SUCCESS) {
        if (error)
            *error = tr("Could not register video file types (Windows error %1).").arg(failure);
        return false;
    }
    return true;
#else
    Q_UNUSED(executable)
    Q_UNUSED(registryRoot)
    if (error)
        *error = tr("Video file associations are available on Windows.");
    return false;
#endif
}

bool FileAssociations::registerVideoTypes()
{
    QString error;
    const bool success = registerApplication(QCoreApplication::applicationFilePath(),
                                             QStringLiteral("Software"), &error);
    setError(error);
#ifdef Q_OS_WIN
    if (success)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
    return success;
}

bool FileAssociations::openDefaultApps()
{
    if (!registerVideoTypes())
        return false;
    const bool success = QDesktopServices::openUrl(
        QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=KPlayer")));
    if (!success)
        setError(tr("Could not open Windows Default apps. Open Settings → Apps → Default apps and choose KPlayer."));
    return success;
}
