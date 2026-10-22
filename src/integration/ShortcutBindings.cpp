#include "ShortcutBindings.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeySequence>
#include <QSet>
#include <QSettings>

namespace {
enum class Scope { Application, Player, Playlist, SubtitleRules };

struct Command {
    const char *id;
    const char *title;
    const char *category;
    const char *defaults;
    Scope scope = Scope::Player;
};

const Command commands[] = {
    {"openMedia", QT_TRANSLATE_NOOP("ShortcutBindings", "Open media"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "F3;Ctrl+O", Scope::Application},
    {"searchPlaylist", QT_TRANSLATE_NOOP("ShortcutBindings", "Search playlist"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "Ctrl+F", Scope::Application},
    {"showPlaylist", QT_TRANSLATE_NOOP("ShortcutBindings", "Show or hide playlist"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "F6;Ctrl+L", Scope::Application},
    {"closeMedia", QT_TRANSLATE_NOOP("ShortcutBindings", "Close media"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "F4", Scope::Application},
    {"preferences", QT_TRANSLATE_NOOP("ShortcutBindings", "Advanced settings"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "F5", Scope::Application},
    {"controls", QT_TRANSLATE_NOOP("ShortcutBindings", "Playback controls"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "F7", Scope::Application},
    {"mediaInfo", QT_TRANSLATE_NOOP("ShortcutBindings", "Media information"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "Ctrl+F1", Scope::Application},
    {"about", QT_TRANSLATE_NOOP("ShortcutBindings", "About KPlayer"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "F1", Scope::Application},
    {"exit", QT_TRANSLATE_NOOP("ShortcutBindings", "Exit application"), QT_TRANSLATE_NOOP("ShortcutBindings", "Application"), "Alt+F4", Scope::Application},
    {"fullscreen", QT_TRANSLATE_NOOP("ShortcutBindings", "Toggle fullscreen"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Return;Enter"},
    {"exitFullscreen", QT_TRANSLATE_NOOP("ShortcutBindings", "Leave fullscreen"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Escape", Scope::Application},
    {"playPause", QT_TRANSLATE_NOOP("ShortcutBindings", "Play or pause"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Space"},
    {"mute", QT_TRANSLATE_NOOP("ShortcutBindings", "Mute or unmute"), QT_TRANSLATE_NOOP("ShortcutBindings", "Audio"), "M"},
    {"captureFrame", QT_TRANSLATE_NOOP("ShortcutBindings", "Capture frame"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Ctrl+E"},
    {"seekBackward", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek backward 10 seconds"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Left"},
    {"seekForward", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek forward 10 seconds"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Right"},
    {"volumeUp", QT_TRANSLATE_NOOP("ShortcutBindings", "Increase volume"), QT_TRANSLATE_NOOP("ShortcutBindings", "Audio"), "Up"},
    {"volumeDown", QT_TRANSLATE_NOOP("ShortcutBindings", "Decrease volume"), QT_TRANSLATE_NOOP("ShortcutBindings", "Audio"), "Down"},
    {"toggleSpeed", QT_TRANSLATE_NOOP("ShortcutBindings", "Toggle playback speed"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Z"},
    {"resetFrame", QT_TRANSLATE_NOOP("ShortcutBindings", "Reset video frame"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+5;Num+Clear"},
    {"frameNarrower", QT_TRANSLATE_NOOP("ShortcutBindings", "Make video narrower"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+4;Num+Left"},
    {"frameWider", QT_TRANSLATE_NOOP("ShortcutBindings", "Make video wider"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+6;Num+Right"},
    {"frameTaller", QT_TRANSLATE_NOOP("ShortcutBindings", "Make video taller"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+8;Num+Up"},
    {"frameShorter", QT_TRANSLATE_NOOP("ShortcutBindings", "Make video shorter"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+2;Num+Down"},
    {"frameLarger", QT_TRANSLATE_NOOP("ShortcutBindings", "Make video larger"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+9;Num+PgUp"},
    {"frameSmaller", QT_TRANSLATE_NOOP("ShortcutBindings", "Make video smaller"), QT_TRANSLATE_NOOP("ShortcutBindings", "Video"), "Num+1;Num+End"},
    {"speedDown", QT_TRANSLATE_NOOP("ShortcutBindings", "Decrease playback speed"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "X"},
    {"speedUp", QT_TRANSLATE_NOOP("ShortcutBindings", "Increase playback speed"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "C"},
    {"previousTrack", QT_TRANSLATE_NOOP("ShortcutBindings", "Previous track"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "PgUp"},
    {"nextTrack", QT_TRANSLATE_NOOP("ShortcutBindings", "Next track"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "PgDown"},
    {"subtitlesVisible", QT_TRANSLATE_NOOP("ShortcutBindings", "Show or hide subtitles"), QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitles"), "Alt+H"},
    {"cycleSubtitle", QT_TRANSLATE_NOOP("ShortcutBindings", "Next subtitle track"), QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitles"), "Alt+L"},
    {"openSubtitles", QT_TRANSLATE_NOOP("ShortcutBindings", "Open subtitle file"), QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitles"), "Alt+O"},
    {"subtitleMenu", QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitle menu"), QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitles"), "L"},
    {"seekStart", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek to start"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Backspace"},
    {"seekMiddle", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek to middle"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Ctrl+Backspace"},
    {"seekNearEnd", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek near end"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Shift+Backspace"},
    {"seekBackward30", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek backward 30 seconds"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Ctrl+Left"},
    {"seekForward30", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek forward 30 seconds"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Ctrl+Right"},
    {"seekBackward60", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek backward 60 seconds"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Shift+Left"},
    {"seekForward60", QT_TRANSLATE_NOOP("ShortcutBindings", "Seek forward 60 seconds"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "Shift+Right"},
    {"previousFrame", QT_TRANSLATE_NOOP("ShortcutBindings", "Previous frame"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "D"},
    {"nextFrame", QT_TRANSLATE_NOOP("ShortcutBindings", "Next frame"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playback"), "F"},
    {"playlistPlay", QT_TRANSLATE_NOOP("ShortcutBindings", "Play selected item"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playlist"), "Return;Enter;Space", Scope::Playlist},
    {"playlistRemove", QT_TRANSLATE_NOOP("ShortcutBindings", "Remove selected items"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playlist"), "Del", Scope::Playlist},
    {"playlistSelectAll", QT_TRANSLATE_NOOP("ShortcutBindings", "Select all items"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playlist"), "Ctrl+A", Scope::Playlist},
    {"playlistMoveUp", QT_TRANSLATE_NOOP("ShortcutBindings", "Move selected items up"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playlist"), "Ctrl+Up", Scope::Playlist},
    {"playlistMoveDown", QT_TRANSLATE_NOOP("ShortcutBindings", "Move selected items down"), QT_TRANSLATE_NOOP("ShortcutBindings", "Playlist"), "Ctrl+Down", Scope::Playlist},
    {"subtitleRuleMoveUp", QT_TRANSLATE_NOOP("ShortcutBindings", "Move subtitle rule up"), QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitle rule editor"), "Alt+Up", Scope::SubtitleRules},
    {"subtitleRuleMoveDown", QT_TRANSLATE_NOOP("ShortcutBindings", "Move subtitle rule down"), QT_TRANSLATE_NOOP("ShortcutBindings", "Subtitle rule editor"), "Alt+Down", Scope::SubtitleRules},
};

QString translated(const char *text)
{
    return QCoreApplication::translate("ShortcutBindings", text);
}

const Command *findCommand(const QString &id)
{
    for (const auto &command : commands) {
        if (id == QLatin1String(command.id))
            return &command;
    }
    return nullptr;
}

bool usableKey(Qt::Key key)
{
    switch (key) {
    case Qt::Key_unknown:
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return false;
    default:
        return int(key) > 0;
    }
}

QString canonicalSequence(const QString &text)
{
    const QKeySequence sequence = QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
    if (sequence.count() != 1 || !usableKey(sequence[0].key()))
        return {};
    return sequence.toString(QKeySequence::PortableText);
}

QString conflictKey(const QString &text)
{
    const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() == 1 && sequence[0].key() == Qt::Key_Enter) {
        const QKeyCombination combination(sequence[0].keyboardModifiers() & ~Qt::KeypadModifier,
                                          Qt::Key_Enter);
        return QKeySequence(combination).toString(QKeySequence::PortableText);
    }
    return text;
}

QStringList defaultSequences(const Command &command)
{
    QStringList result;
    for (const QString &part : QString::fromLatin1(command.defaults).split(u';'))
        result.append(canonicalSequence(part));
    return result;
}
}

ShortcutBindings::ShortcutBindings(QObject *parent) : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    QVariantMap stored;
    for (const auto &command : commands) {
        const QString id = QString::fromLatin1(command.id);
        m_bindings.insert(id, defaultSequences(command));
        if (settings.contains(id))
            stored.insert(id, settings.value(id));
    }
    BindingMap normalized;
    QString error;
    if (normalizedDraft(stored, &normalized, &error))
        m_bindings = normalized;
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
}

QVariantList ShortcutBindings::entries() const
{
    QVariantList result;
    for (const auto &command : commands) {
        const QString id = QString::fromLatin1(command.id);
        result.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("title"), translated(command.title)},
                                  {QStringLiteral("category"), translated(command.category)},
                                  {QStringLiteral("sequences"), m_bindings.value(id)},
                                  {QStringLiteral("defaults"), defaultSequences(command)}});
    }
    return result;
}

QStringList ShortcutBindings::sequences(const QString &id) const
{
    return m_bindings.value(id);
}

QString ShortcutBindings::label(const QString &id) const
{
    QStringList labels;
    for (const QString &sequence : m_bindings.value(id))
        labels.append(QKeySequence::fromString(sequence, QKeySequence::PortableText)
                          .toString(QKeySequence::NativeText));
    return labels.join(QStringLiteral(", "));
}

bool ShortcutBindings::normalizedDraft(const QVariantMap &draft, BindingMap *result,
                                       QString *error, QVariantMap *issue) const
{
    *result = m_bindings;
    for (auto entry = draft.cbegin(); entry != draft.cend(); ++entry) {
        const Command *command = findCommand(entry.key());
        if (issue)
            issue->insert(QStringLiteral("commandId"), entry.key());
        if (!command) {
            *error = tr("Unknown shortcut command: %1").arg(entry.key());
            return false;
        }
        QStringList requested;
        if (entry.value().metaType().id() == QMetaType::QString) {
            requested = parse(entry.value().toString());
        } else if (entry.value().metaType().id() == QMetaType::QStringList) {
            requested = entry.value().toStringList();
        } else if (entry.value().metaType().id() == QMetaType::QVariantList) {
            for (const QVariant &value : entry.value().toList()) {
                if (value.metaType().id() != QMetaType::QString) {
                    *error = tr("Shortcuts for %1 must be text.").arg(translated(command->title));
                    return false;
                }
                requested.append(value.toString());
            }
        } else {
            *error = tr("Shortcuts for %1 must be a list of keys.").arg(translated(command->title));
            return false;
        }
        QStringList normalized;
        QSet<QString> seen;
        for (const QString &text : requested) {
            const QString sequence = canonicalSequence(text);
            if (sequence.isEmpty()) {
                *error = tr("Invalid shortcut '%1' for %2. Use a single key combination.")
                             .arg(text, translated(command->title));
                return false;
            }
            const QString matchKey = conflictKey(sequence);
            if (seen.contains(matchKey)) {
                *error = tr("Shortcut %1 is listed more than once for %2.")
                             .arg(sequence, translated(command->title));
                return false;
            }
            seen.insert(matchKey);
            normalized.append(sequence);
        }
        result->insert(entry.key(), normalized);
    }

    QMap<QString, QList<const Command *>> owners;
    for (const auto &command : commands) {
        for (const QString &sequence : result->value(QString::fromLatin1(command.id))) {
            const QString matchKey = conflictKey(sequence);
            for (const auto *owner : owners.value(matchKey)) {
                const bool separateModal = (command.scope == Scope::SubtitleRules)
                    != (owner->scope == Scope::SubtitleRules);
                const bool playlistAndPlayer = (command.scope == Scope::Playlist && owner->scope == Scope::Player)
                    || (command.scope == Scope::Player && owner->scope == Scope::Playlist);
                if (!separateModal && !playlistAndPlayer) {
                    if (issue) {
                        issue->insert(QStringLiteral("commandId"), QString::fromLatin1(command.id));
                        issue->insert(QStringLiteral("conflictingId"), QString::fromLatin1(owner->id));
                    }
                    *error = tr("Shortcut %1 is used by both %2 and %3.")
                                 .arg(sequence, translated(owner->title), translated(command.title));
                    return false;
                }
            }
            owners[matchKey].append(&command);
        }
    }
    return true;
}

QVariantMap ShortcutBindings::validate(const QVariantMap &draft) const
{
    BindingMap normalized;
    QString error;
    QVariantMap issue;
    const bool valid = normalizedDraft(draft, &normalized, &error, &issue);
    if (valid)
        issue.clear();
    issue.insert(QStringLiteral("valid"), valid);
    issue.insert(QStringLiteral("error"), error);
    return issue;
}

bool ShortcutBindings::save(const QVariantMap &draft)
{
    BindingMap normalized;
    QString error;
    if (!normalizedDraft(draft, &normalized, &error))
        return false;
    if (normalized == m_bindings)
        return true;
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    QVariantMap previous;
    for (auto entry = normalized.cbegin(); entry != normalized.cend(); ++entry) {
        if (settings.contains(entry.key()))
            previous.insert(entry.key(), settings.value(entry.key()));
        settings.setValue(entry.key(), entry.value());
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        // QSettings shares its pending cache with other instances. Roll back that
        // cache as well as leaving the active bindings unchanged on a failed save.
        for (auto entry = normalized.cbegin(); entry != normalized.cend(); ++entry) {
            if (previous.contains(entry.key()))
                settings.setValue(entry.key(), previous.value(entry.key()));
            else
                settings.remove(entry.key());
        }
        settings.sync();
        return false;
    }
    m_bindings = normalized;
    ++m_revision;
    emit bindingsChanged();
    return true;
}

QStringList ShortcutBindings::parse(const QString &text) const
{
    if (text.trimmed().isEmpty())
        return {};
    QStringList parts;
    QString part;
    // The editor escapes separators and literal backslashes, allowing shortcuts
    // such as Ctrl+; and Ctrl+\ to coexist with semicolon-separated alternatives.
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (character == u'\\' && i + 1 < text.size()
            && (text.at(i + 1) == u';' || text.at(i + 1) == u'\\')) {
            part.append(text.at(++i));
        } else if (character == u';') {
            parts.append(part);
            part.clear();
        } else {
            part.append(character);
        }
    }
    parts.append(part);
    for (QString &part : parts) {
        part = part.trimmed();
        const QString normalized = canonicalSequence(part);
        if (!normalized.isEmpty())
            part = normalized;
    }
    return parts;
}

QString ShortcutBindings::format(const QStringList &sequences) const
{
    QStringList escaped;
    for (QString sequence : sequences) {
        sequence.replace(u'\\', QStringLiteral("\\\\"));
        sequence.replace(u';', QStringLiteral("\\;"));
        escaped.append(sequence);
    }
    return escaped.join(QStringLiteral("; "));
}

QString ShortcutBindings::fromKeyEvent(int key, int modifiers) const
{
    if (!usableKey(Qt::Key(key)))
        return {};
    const auto supportedModifiers = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier
        | Qt::MetaModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier;
    const QKeySequence sequence(QKeyCombination(Qt::KeyboardModifiers(modifiers) & supportedModifiers,
                                               Qt::Key(key)));
    return canonicalSequence(sequence.toString(QKeySequence::PortableText));
}

bool ShortcutBindings::matches(const QString &id, int key, int modifiers) const
{
    const QString sequence = fromKeyEvent(key, modifiers);
    if (sequence.isEmpty())
        return false;
    const QString matchKey = conflictKey(sequence);
    for (const QString &binding : m_bindings.value(id)) {
        if (conflictKey(binding) == matchKey)
            return true;
    }
    return false;
}

bool ShortcutBindings::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ++m_revision;
        emit bindingsChanged();
    }
    return QObject::eventFilter(watched, event);
}
