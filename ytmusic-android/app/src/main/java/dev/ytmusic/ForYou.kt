package dev.ytmusic

import dev.ytmusic.core.Recs
import dev.ytmusic.core.Section
import dev.ytmusic.core.Track
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList

/**
 * The "For you" page: shelves built from your plays and likes (see [Recs]). The last result is cached on disk
 * so the page appears instantly, and it is rebuilt in the background when it is old or you have played
 * a handful of new songs since.
 */
object ForYou {
    var sections: List<Section> = emptyList()
        private set
    @Volatile var building = false
        private set
    private var builtAt = 0L
    private var playsAtBuild = 0
    val listeners = CopyOnWriteArrayList<() -> Unit>()
    private lateinit var file: File

    fun init(ctx: android.content.Context) {
        file = File(ctx.filesDir, "foryou.json")
        try {
            if (file.exists()) {
                val o = JSONObject(file.readText())
                builtAt = o.optLong("at"); playsAtBuild = o.optInt("plays")
                val a = o.getJSONArray("sections")
                sections = List(a.length()) { i ->
                    val so = a.getJSONObject(i)
                    val ts = so.getJSONArray("tracks")
                    Section(so.getString("id"), so.getString("title"), so.optString("subtitle"),
                        List(ts.length()) { j -> ts.getJSONObject(j).let { Track(it.getString("v"), it.optString("t"), it.optString("a"), it.optLong("d")) } })
                }
            }
        } catch (e: Throwable) { Crash.record("for-you cache", e) }
    }

    private fun fire() = App.main.post { listeners.forEach { it() } }

    fun refreshIfStale() {
        val stale = sections.isEmpty() || System.currentTimeMillis() - builtAt > 4 * 3600_000L || Store.plays - playsAtBuild >= 5
        if (stale) refresh()
    }

    fun refresh() {
        if (building) return
        building = true
        fire()
        val recent = ArrayList(Store.recent)
        val liked = ArrayList(Store.liked)
        val plays = Store.plays
        App.io.execute {
            try {
                val built = Recs.build(recent, liked)
                android.util.Log.i("ForYou", "built ${built.size} sections from ${recent.size} recent / ${liked.size} liked: " + built.joinToString { it.id + "(" + it.tracks.size + ")" })
                App.main.post {
                    if (built.isNotEmpty()) { sections = built; builtAt = System.currentTimeMillis(); playsAtBuild = plays; save() }
                    building = false
                    listeners.forEach { it() }
                }
            } catch (e: Throwable) {
                Crash.record("for-you build", e)
                App.main.post { building = false; listeners.forEach { it() } }
            }
        }
    }

    private fun save() {
        try {
            val a = JSONArray()
            sections.forEach { s ->
                a.put(JSONObject().put("id", s.id).put("title", s.title).put("subtitle", s.subtitle).put("tracks", JSONArray().also { ta ->
                    s.tracks.forEach { ta.put(JSONObject().put("v", it.vid).put("t", it.title).put("a", it.artist).put("d", it.duration)) }
                }))
            }
            file.writeText(JSONObject().put("at", builtAt).put("plays", playsAtBuild).put("sections", a).toString())
        } catch (e: Throwable) { Crash.record("for-you save", e) }
    }
}
