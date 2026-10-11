.pragma library

function trackName(track) {
    if (!track)
        return ""
    if (track.title && track.title.trim().length > 0)
        return track.title.trim()
    return (track["external-filename"] || "").split(/[\\/]/).pop().trim()
}

function valueForTrack(track, kind) {
    if (!track)
        return ""
    if (kind === "subtrackname")
        return trackName(track)
    if (kind === "language")
        return languageCode(track.lang)
    const number = Number(track.id)
    return kind === "tracknum" && Number.isInteger(number) && number > 0 ? String(number) : ""
}

function languageCode(code) {
    const value = (code || "").trim().toLowerCase().replace(/_/g, "-")
    return /^[a-z]{2,3}(-[a-z0-9]+)*$/.test(value)
        && !["und", "zxx", "mul"].includes(value.split("-")[0]) ? value : ""
}

function languageKey(code) {
    const value = languageCode(code)
    if (value.length === 0)
        return ""
    const locale = Qt.locale(value).name
    return locale === "C" ? value : locale.split("_")[0]
}

function sameRule(kind, first, second) {
    if (kind === "tracknum")
        return Number(first) === Number(second)
    if (kind === "language")
        return languageKey(first) === languageKey(second)
    return first.trim().toLowerCase() === second.trim().toLowerCase()
}
