package dev.ytmusic

import android.content.Context
import dev.ytmusic.core.Lyrics
import dev.ytmusic.core.Track
import dev.ytmusic.core.Yt
import org.json.JSONObject
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList

/** Lyrics kept on disk (one small JSON file per song) so they show offline. Downloaded songs get theirs automatically. */
object LyricsStore {
    private lateinit var dir: File
    val listeners = CopyOnWriteArrayList<() -> Unit>()
    private const val RETRY_MS = 7L * 24 * 3600 * 1000
    private val inFlight = HashSet<String>()

    fun init(ctx: Context) { dir = File(ctx.filesDir, "lyrics").apply { mkdirs() } }

    private fun file(vid: String) = File(dir, "$vid.json")

    class Entry(val lrc: String, val plain: String, val missing: Boolean, val at: Long)

    fun get(vid: String): Entry? = try {
        val f = file(vid)
        if (!f.exists()) null else JSONObject(f.readText()).let { Entry(it.optString("lrc"), it.optString("plain"), it.optBoolean("none"), it.optLong("at")) }
    } catch (_: Throwable) { null }

    fun has(vid: String): Boolean = get(vid)?.let { !it.missing } ?: false

    /** True when we already know the answer (have lyrics, or looked recently and found none). */
    private fun settled(vid: String): Boolean = get(vid)?.let { !it.missing || System.currentTimeMillis() - it.at < RETRY_MS } ?: false

    private fun save(vid: String, r: Lyrics.Result?) {
        val o = JSONObject().put("at", System.currentTimeMillis())
        if (r != null && r.found) o.put("lrc", r.lrc).put("plain", r.plain) else o.put("none", true)
        file(vid).writeText(o.toString())
    }

    /** Blocking: fetches and stores. Returns the entry, or null if the network failed (nothing stored then). */
    fun fetchNow(t: Track, force: Boolean = false): Entry? {
        if (!force && settled(t.vid)) return get(t.vid)
        synchronized(inFlight) { if (!inFlight.add(t.vid)) return get(t.vid) }
        try {
            val r = try { Lyrics.fetch(Yt.client, t.title, t.artist, t.duration) } catch (_: Throwable) { return get(t.vid) }
            save(t.vid, r)
            App.main.post { listeners.forEach { it() } }
            return get(t.vid)
        } finally { synchronized(inFlight) { inFlight.remove(t.vid) } }
    }

    fun fetchAsync(t: Track) { App.io.execute { fetchNow(t) } }

    /** Downloaded songs without lyrics (and not recently checked). */
    fun missingForDownloads(): List<Track> = Downloads.tracks().filter { !settled(it.vid) }

    @Volatile private var sweeping = false
    fun sweepDownloads(onDone: ((Int) -> Unit)? = null) {
        if (sweeping) return
        sweeping = true
        App.io.execute {
            var n = 0
            try {
                for (t in missingForDownloads()) { if (fetchNow(t)?.missing == false) n++; Thread.sleep(250) }
            } finally { sweeping = false }
            if (onDone != null) App.main.post { onDone(n) }
        }
    }
}
