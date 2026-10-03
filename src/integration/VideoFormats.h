#pragma once

#include <QStringList>

namespace VideoFormats {
// Shared by discovery and Windows registration so every discovered type can open here.
inline const QStringList &extensions()
{
    static const QStringList types = {
        "3g2", "3gp", "asf", "avi", "divx", "f4v", "flv", "m2t", "m2ts",
        "m4v", "mkv", "mov", "mp4", "mpeg", "mpg", "mts", "mxf", "ogm",
        "ogv", "qt", "rm", "rmvb", "ts", "vob", "webm", "wmv", "y4m"
    };
    return types;
}
}
