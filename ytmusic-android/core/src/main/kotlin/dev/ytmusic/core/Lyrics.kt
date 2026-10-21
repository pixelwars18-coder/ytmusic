package dev.ytmusic.core

import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import org.json.JSONObject
import java.net.URLEncoder

/** Lyrics from LRCLIB (https://lrclib.net): free, no account. Prefers time-synced lines. */
object Lyrics {
    class Line(val ms: Long, val text: String)
    /** [lrc] is the synced text in LRC format (may be empty), [plain] the unsynced text (may be empty). */
    class Result(val lrc: String, val plain: String) {
        val found get() = lrc.isNotBlank() || plain.isNotBlank()
        /** Community entries are sometimes junk (a single word); don't show those. */
        val usable get() = lrc.lines().count { it.contains(']') && it.substringAfter(']').isNotBlank() } >= 3 || plain.length >= 80
    }

    private val BRACKETS = Regex("""\s*[(\[【][^)\]】]*(official|video|audio|lyric|visuali[sz]er|explicit|remaster|hd|4k|mv|m/v|clip|live|full song|topic|feat|ft\.)[^)\]】]*[)\]】]""", RegexOption.IGNORE_CASE)
    private val TRAIL = Regex("""\s*[|｜]\s*.*$""")

    /** "Artist - Song (Official Video)" + "ArtistVEVO"  ->  (artist, song) as LRCLIB wants them. */
    fun clean(title: String, uploader: String): Pair<String, String> {
        var t = title.replace(BRACKETS, "").replace(TRAIL, "").trim()
        var a = uploader.replace(Regex("""(?i)\s*-\s*topic$"""), "").replace(Regex("""(?i)vevo$"""), "").trim()
        val dash = t.indexOf(" - ")
        if (dash > 0) { a = t.substring(0, dash).trim(); t = t.substring(dash + 3).trim() }
        return a to t
    }

    fun parseLrc(lrc: String): List<Line> {
        val out = ArrayList<Line>()
        val tag = Regex("""\[(\d+):(\d+)(?:[.:](\d+))?]""")
        for (raw in lrc.lines()) {
            val stamps = tag.findAll(raw).toList()
            if (stamps.isEmpty()) continue
            val text = raw.substring(stamps.last().range.last + 1).trim()
            for (m in stamps) {
                val frac = m.groupValues[3]
                val ms = m.groupValues[1].toLong() * 60_000 + m.groupValues[2].toLong() * 1000 +
                    (if (frac.isEmpty()) 0 else (frac + "00").substring(0, 3).toLong())
                out += Line(ms, text)
            }
        }
        return out.sortedBy { it.ms }
    }

    private fun get(client: OkHttpClient, url: String): String? = try {
        client.newCall(Request.Builder().url(url).header("User-Agent", "YTMusic personal player (github.com/pixelwars18-coder/ytmusic)").build()).execute().use { r ->
            if (r.isSuccessful) r.body?.string() else null
        }
    } catch (_: Throwable) { null }

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8")

    private fun fromObj(o: JSONObject) = Result(o.optString("syncedLyrics").let { if (it == "null") "" else it },
        o.optString("plainLyrics").let { if (it == "null") "" else it })

    /** null = nothing found (or the network failed). */
    fun fetch(client: OkHttpClient, title: String, uploader: String, durationSec: Long): Result? {
        val (artist, track) = clean(title, uploader)
        if (track.isBlank()) return null
        val dur = if (durationSec > 0) "&duration=$durationSec" else ""
        get(client, "https://lrclib.net/api/get?artist_name=${enc(artist)}&track_name=${enc(track)}$dur")?.let {
            val r = fromObj(JSONObject(it)); if (r.usable) return r
        }
        fun best(url: String): Result? {
            val arr = try { JSONArray(get(client, url) ?: return null) } catch (_: Throwable) { return null }
            var pick: JSONObject? = null; var pickScore = Long.MAX_VALUE
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                if (!fromObj(o).usable) continue
                val d = o.optDouble("duration", 0.0)
                val gap = if (durationSec > 0 && d > 0) Math.abs(d - durationSec).toLong() else 15
                if (gap > 12) continue
                val score = gap * 2 + (if (o.optString("syncedLyrics").isBlank() || o.optString("syncedLyrics") == "null") 5 else 0)
                if (score < pickScore) { pick = o; pickScore = score }
            }
            return pick?.let(::fromObj)?.takeIf { it.usable }
        }
        return best("https://lrclib.net/api/search?track_name=${enc(track)}&artist_name=${enc(artist)}")
            ?: best("https://lrclib.net/api/search?q=${enc("$artist $track")}")
    }
}
