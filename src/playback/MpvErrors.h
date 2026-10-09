#pragma once

#include <QCoreApplication>
#include <QString>
#include <mpv/client.h>

namespace MpvErrors {
// libmpv's English diagnostics bypass Qt's translation catalogs.
inline QString description(int error)
{
    switch (error) {
    case MPV_ERROR_SUCCESS: return QCoreApplication::translate("MpvErrors", "Success");
    case MPV_ERROR_EVENT_QUEUE_FULL: return QCoreApplication::translate("MpvErrors", "Event queue is full");
    case MPV_ERROR_NOMEM: return QCoreApplication::translate("MpvErrors", "Out of memory");
    case MPV_ERROR_UNINITIALIZED: return QCoreApplication::translate("MpvErrors", "Player is not initialized");
    case MPV_ERROR_INVALID_PARAMETER: return QCoreApplication::translate("MpvErrors", "Invalid parameter");
    case MPV_ERROR_OPTION_NOT_FOUND: return QCoreApplication::translate("MpvErrors", "Option not found");
    case MPV_ERROR_OPTION_FORMAT: return QCoreApplication::translate("MpvErrors", "Unsupported option format");
    case MPV_ERROR_OPTION_ERROR: return QCoreApplication::translate("MpvErrors", "Could not set option");
    case MPV_ERROR_PROPERTY_NOT_FOUND: return QCoreApplication::translate("MpvErrors", "Property not found");
    case MPV_ERROR_PROPERTY_FORMAT: return QCoreApplication::translate("MpvErrors", "Unsupported property format");
    case MPV_ERROR_PROPERTY_UNAVAILABLE: return QCoreApplication::translate("MpvErrors", "Property is unavailable");
    case MPV_ERROR_PROPERTY_ERROR: return QCoreApplication::translate("MpvErrors", "Could not access property");
    case MPV_ERROR_COMMAND: return QCoreApplication::translate("MpvErrors", "Command failed");
    case MPV_ERROR_LOADING_FAILED: return QCoreApplication::translate("MpvErrors", "Could not load media");
    case MPV_ERROR_AO_INIT_FAILED: return QCoreApplication::translate("MpvErrors", "Could not initialize audio output");
    case MPV_ERROR_VO_INIT_FAILED: return QCoreApplication::translate("MpvErrors", "Could not initialize video output");
    case MPV_ERROR_NOTHING_TO_PLAY: return QCoreApplication::translate("MpvErrors", "No audio or video to play");
    case MPV_ERROR_UNKNOWN_FORMAT: return QCoreApplication::translate("MpvErrors", "Unknown media format");
    case MPV_ERROR_UNSUPPORTED: return QCoreApplication::translate("MpvErrors", "Operation is unsupported");
    case MPV_ERROR_NOT_IMPLEMENTED: return QCoreApplication::translate("MpvErrors", "Operation is not implemented");
    case MPV_ERROR_GENERIC: return QCoreApplication::translate("MpvErrors", "Player error");
    default: return QCoreApplication::translate("MpvErrors", "Unknown player error (%1)").arg(error);
    }
}
}
