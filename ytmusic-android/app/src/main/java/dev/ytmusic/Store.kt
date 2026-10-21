package dev.ytmusic

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import dev.ytmusic.core.Track
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList

class SavedPlaylist(val id: String, var name: String, val tracks: ArrayList<Track>, val source: String?)

/** Everything the user owns: likes, saved playlists, search history, the last queue. One JSON file. */
object Store {
    private lateinit var file: File
    val liked = ArrayList<Track>()
    val likedSet = HashSet<String>()
    val history = ArrayList<String>()
    val playlists = ArrayList<SavedPlaylist>()
    val recent = ArrayList<Track>()      // recently played, newest first
    var plays = 0                        // total tracks started (drives "refresh recommendations")
    var queue: List<Track> = emptyList()
    var queueIndex = 0
    var queuePosMs = 0L
    var shuffle = false
    var repeat = 0

    val listeners = CopyOnWriteArrayList<() -> Unit>()
    private val writer = HandlerThread("store-writer").apply { start() }
    private val writerHandler = Handler(writer.looper)
    private val saveRunnable = Runnable { snapshotAndWrite() }

    fun init(ctx: Context) {
        file = File(ctx.filesDir, "state.json")
        try { if (file.exists()) load(JSONObject(file.readText())) } catch (e: Throwable) { Crash.record("state load", e) }
    }

    private fun tj(t: Track) = JSONObject().put("v", t.vid).put("t", t.title).put("a", t.artist).put("d", t.duration)
    private fun jt(o: JSONObject) = Track(o.getString("v"), o.optString("t"), o.optString("a"), o.optLong("d"))
    private fun tracks(a: JSONArray?): List<Track> {
        val out = ArrayList<Track>()
        if (a != null) for (i in 0 until a.length()) try { out += jt(a.getJSONObject(i)) } catch (_: Throwable) { }
        return out
    }

    private fun load(o: JSONObject) {
        liked += tracks(o.optJSONArray("liked")); likedSet += liked.map { it.vid }
        o.optJSONArray("history")?.let { for (i in 0 until it.length()) history += it.getString(i) }
        o.optJSONArray("playlists")?.let { a ->
            for (i in 0 until a.length()) {
                val p = a.getJSONObject(i)
                playlists += SavedPlaylist(p.getString("id"), p.optString("name", "Playlist"), ArrayList(tracks(p.optJSONArray("tracks"))), p.optString("source").ifEmpty { null })
            }
        }
        recent += tracks(o.optJSONArray("recent")); plays = o.optInt("plays")
        queue = tracks(o.optJSONArray("queue"))
        queueIndex = o.optInt("qi"); queuePosMs = o.optLong("qp")
        shuffle = o.optBoolean("shuffle"); repeat = o.optInt("repeat")
    }

    /** Called on the main thread; JSON is built there (so no concurrent modification) and written on a worker. */
    private fun snapshotAndWrite() {
        val root = JSONObject()
        root.put("liked", JSONArray().also { a -> liked.forEach { a.put(tj(it)) } })
        root.put("history", JSONArray(history))
        root.put("playlists", JSONArray().also { a ->
            playlists.forEach { p ->
                a.put(JSONObject().put("id", p.id).put("name", p.name).put("source", p.source ?: "")
                    .put("tracks", JSONArray().also { ta -> p.tracks.forEach { ta.put(tj(it)) } }))
            }
        })
        root.put("recent", JSONArray().also { a -> recent.forEach { a.put(tj(it)) } }).put("plays", plays)
        root.put("queue", JSONArray().also { a -> queue.forEach { a.put(tj(it)) } })
        root.put("qi", queueIndex).put("qp", queuePosMs).put("shuffle", shuffle).put("repeat", repeat)
        val text = root.toString()
        writerHandler.post {
            try {
                val tmp = File(file.parentFile, "state.json.tmp")
                tmp.writeText(text)
                tmp.renameTo(file)
            } catch (e: Throwable) { Crash.record("state save", e) }
        }
    }

    // ---- backup / restore: one JSON format shared with the desktop app (vid/title/artist/duration) ----
    private fun bj(t: Track) = JSONObject().put("vid", t.vid).put("title", t.title).put("artist", t.artist).put("duration", t.duration)
    private fun jb(o: JSONObject) = Track(o.getString("vid"), o.optString("title"), o.optString("artist"), o.optDouble("duration", 0.0).toLong())
    private fun bTracks(a: JSONArray?): List<Track> {
        val out = ArrayList<Track>()
        if (a != null) for (i in 0 until a.length()) try { out += jb(a.getJSONObject(i)) } catch (_: Throwable) { }
        return out
    }

    fun exportBackup(): String {
        val root = JSONObject().put("app", "ytmusic").put("version", 1)
        root.put("liked", JSONArray().also { a -> liked.forEach { a.put(bj(it)) } })
        root.put("history", JSONArray(history))
        root.put("playlists", JSONArray().also { a ->
            playlists.forEach { p ->
                a.put(JSONObject().put("id", p.id).put("name", p.name).put("source", p.source ?: "")
                    .put("tracks", JSONArray().also { ta -> p.tracks.forEach { ta.put(bj(it)) } }))
            }
        })
        return root.toString(2)
    }

    /** Merges a backup into the library (nothing is removed). Returns a short summary. */
    fun importBackup(text: String): String {
        val o = JSONObject(text)
        if (o.optString("app") != "ytmusic") throw IllegalArgumentException("Not a YT Music backup")
        var nLiked = 0; var nPl = 0; var nTracks = 0
        for (t in bTracks(o.optJSONArray("liked"))) if (likedSet.add(t.vid)) { liked += t; nLiked++ }
        o.optJSONArray("history")?.let { a -> for (i in 0 until a.length()) { val q = a.getString(i); if (q !in history) history += q } }
        o.optJSONArray("playlists")?.let { a ->
            for (i in 0 until a.length()) {
                val po = a.getJSONObject(i)
                val id = po.getString("id")
                val tr = bTracks(po.optJSONArray("tracks"))
                val have = playlists.firstOrNull { it.id == id }
                if (have == null) {
                    playlists += SavedPlaylist(id, po.optString("name", "Playlist"), ArrayList(tr), po.optString("source").ifEmpty { null }); nPl++; nTracks += tr.size
                } else {
                    val known = have.tracks.map { it.vid }.toHashSet()
                    for (t in tr) if (known.add(t.vid)) { have.tracks += t; nTracks++ }
                }
            }
        }
        changed()
        return "Restored $nLiked liked songs, $nPl playlists, $nTracks playlist songs"
    }

    fun scheduleSave() {
        App.main.removeCallbacks(saveRunnable)
        App.main.postDelayed(saveRunnable, 1000)
    }

    fun saveNow() {
        App.main.removeCallbacks(saveRunnable)
        snapshotAndWrite()
    }

    fun changed() {
        listeners.forEach { it() }
        scheduleSave()
    }

    // ---- listening history ----
    fun addRecent(t: Track) {
        recent.removeAll { it.vid == t.vid }
        recent.add(0, t)
        while (recent.size > 60) recent.removeAt(recent.size - 1)
        plays++
        scheduleSave()
    }

    // ---- likes ----
    fun toggleLike(t: Track): Boolean {
        val now = if (t.vid in likedSet) {
            likedSet -= t.vid; liked.removeAll { it.vid == t.vid }; false
        } else {
            likedSet += t.vid; liked.add(0, t); true
        }
        changed()
        return now
    }

    /** Removes the given songs from Liked in one go. */
    fun unlike(vids: Set<String>) {
        liked.removeAll { it.vid in vids }
        likedSet.removeAll(vids)
        changed()
    }

    fun clearLikes() { liked.clear(); likedSet.clear(); changed() }

    fun deletePlaylists(ids: Set<String>) { playlists.removeAll { it.id in ids }; changed() }
    fun clearAllPlaylists() { playlists.clear(); changed() }
    fun removeAllFromPlaylist(id: String) { playlists.firstOrNull { it.id == id }?.tracks?.clear(); changed() }
    /** Removes the songs at [indices] (positions in the playlist). */
    fun removeFromPlaylist(id: String, indices: Collection<Int>) {
        val p = playlists.firstOrNull { it.id == id } ?: return
        indices.sortedDescending().forEach { if (it in p.tracks.indices) p.tracks.removeAt(it) }
        changed()
    }

    // ---- history ----
    fun addHistory(q: String) {
        history.removeAll { it.equals(q, ignoreCase = true) }
        history.add(0, q)
        while (history.size > 12) history.removeAt(history.size - 1)
        scheduleSave()
    }
    fun clearRecent() { recent.clear(); changed() }
    fun clearHistory() { history.clear(); changed() }

    // ---- playlists ----
    fun createPlaylist(name: String, tracks: List<Track> = emptyList(), source: String? = null): SavedPlaylist {
        val p = SavedPlaylist(java.lang.Long.toString(System.currentTimeMillis(), 36), name.trim().ifEmpty { "New playlist" }, ArrayList(tracks), source)
        playlists.add(0, p)
        changed()
        return p
    }
    /** Adds the tracks that aren't already in the playlist; returns how many were added. */
    fun addToPlaylist(id: String, add: List<Track>): Int {
        val p = playlists.firstOrNull { it.id == id } ?: return 0
        val have = p.tracks.map { it.vid }.toHashSet()
        var n = 0
        for (t in add) if (have.add(t.vid)) { p.tracks += t; n++ }
        changed()
        return n
    }
    fun removeFromPlaylist(id: String, index: Int) {
        playlists.firstOrNull { it.id == id }?.let { if (index in it.tracks.indices) it.tracks.removeAt(index) }
        changed()
    }
    fun renamePlaylist(id: String, name: String) {
        playlists.firstOrNull { it.id == id }?.name = name.trim().ifEmpty { "Playlist" }
        changed()
    }
    fun deletePlaylist(id: String) { playlists.removeAll { it.id == id }; changed() }
    fun playlist(id: String) = playlists.firstOrNull { it.id == id }
}
