package dev.ytmusic.core

data class Track(
    val vid: String,
    val title: String,
    val artist: String,
    val duration: Long, // seconds, 0 for live / unknown
)

/** A resolved, directly playable audio stream. */
data class Audio(
    val vid: String,
    val url: String,
    val ext: String,       // "m4a", "webm"
    val mime: String,
    val bitrate: Int,      // kbps, 0 if unknown
    val expiresAtMs: Long, // wall clock; stream URLs die after a few hours
) {
    val expired get() = System.currentTimeMillis() > expiresAtMs - 60_000
}

/** One page of list results plus a way to fetch the next page. */
class Page(val items: List<Track>, val more: (() -> Page)?)

/** A YouTube playlist as shown in search results. */
data class PlaylistRef(
    val url: String,
    val name: String,
    val uploader: String,
    val count: Long,        // number of videos, -1 if unknown
    val thumbUrl: String,   // cover image URL, "" if unknown
)

class PlaylistPage(val items: List<PlaylistRef>, val more: (() -> PlaylistPage)?)

private val EXPLICIT = Regex("(?i)(?<!non[- ])(?<!not )\\bexplicit\\b|🅴")
/** YouTube search results carry no "explicit" flag; uploaders usually say so in the title, e.g. "(Explicit)". */
val Track.explicit: Boolean get() = EXPLICIT.containsMatchIn(title)
