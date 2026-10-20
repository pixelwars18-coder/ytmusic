package dev.ytmusic

import dev.ytmusic.core.Page
import dev.ytmusic.core.PlaylistPage
import dev.ytmusic.core.PlaylistRef
import dev.ytmusic.core.Track
import dev.ytmusic.core.Yt
import java.io.IOException
import java.util.concurrent.CopyOnWriteArrayList

/** Search state: songs or playlists for the current query, plus an opened (remote) playlist. */
object Library {
    enum class Mode { SONGS, PLAYLISTS }

    class Detail(val title: String, val url: String) {
        val tracks = ArrayList<Track>()
        var more: (() -> Page)? = null
        var loading = true
        var status = ""
    }

    var mode = Mode.SONGS
        private set
    var query = ""
        private set
    val songs = ArrayList<Track>()
    val lists = ArrayList<PlaylistRef>()
    var searching = false
        private set
    var status = ""
        private set
    var detail: Detail? = null
        private set
    val listeners = CopyOnWriteArrayList<() -> Unit>()

    private var songsMore: (() -> Page)? = null
    private var listsMore: (() -> PlaylistPage)? = null
    private var gen = 0

    val canLoadMore get() = !searching && when (mode) {
        Mode.SONGS -> songsMore != null && songs.isNotEmpty()
        Mode.PLAYLISTS -> listsMore != null && lists.isNotEmpty()
    }
    val hasResults get() = if (mode == Mode.SONGS) songs.isNotEmpty() else lists.isNotEmpty()

    private fun changed() = listeners.forEach { it() }

    private fun friendly(e: Throwable): String = when {
        e is java.net.UnknownHostException || e is java.net.SocketTimeoutException || e is java.net.ConnectException -> "No connection"
        e is IOException -> "Network error: ${e.message ?: ""}".trim()
        else -> (e.message ?: e.javaClass.simpleName).take(120)
    }

    fun setMode(m: Mode) {
        if (m == mode) return
        mode = m
        if (query.isNotEmpty() && !hasResults && !searching) search(query) else changed()
    }

    fun search(q: String) {
        val text = q.trim()
        if (text.isEmpty()) return
        val isLink = text.startsWith("http://") || text.startsWith("https://")
        if (isLink) mode = Mode.SONGS
        query = text
        Store.addHistory(text)
        val my = ++gen
        detail = null
        searching = true
        status = "Searching…"
        if (mode == Mode.SONGS) { songs.clear(); songsMore = null } else { lists.clear(); listsMore = null }
        changed()
        val m = mode
        App.io.execute {
            try {
                if (m == Mode.SONGS) {
                    val page = Yt.search(text)
                    App.main.post { if (my == gen) { songs += page.items; songsMore = page.more; finish(songs.isEmpty()) } }
                } else {
                    val page = Yt.searchPlaylists(text)
                    App.main.post { if (my == gen) { lists += page.items; listsMore = page.more; finish(lists.isEmpty()) } }
                }
            } catch (e: Throwable) {
                Crash.record("search \"$text\" ($m)", e)
                App.main.post { if (my == gen) { searching = false; status = friendly(e); changed() } }
            }
        }
    }

    private fun finish(empty: Boolean) {
        searching = false
        status = if (empty) "No results" else ""
        changed()
    }

    fun loadMore() {
        if (!canLoadMore) return
        val my = ++gen
        searching = true
        changed()
        val m = mode
        val sm = songsMore
        val lm = listsMore
        App.io.execute {
            try {
                if (m == Mode.SONGS && sm != null) {
                    val page = sm()
                    App.main.post { if (my == gen) { songs += page.items.filter { n -> songs.none { it.vid == n.vid } }; songsMore = page.more; finish(false) } }
                } else if (lm != null) {
                    val page = lm()
                    App.main.post { if (my == gen) { lists += page.items; listsMore = page.more; finish(false) } }
                }
            } catch (e: Throwable) {
                App.main.post { if (my == gen) { searching = false; changed(); toast(friendly(e)) } }
            }
        }
    }

    // ---- opened remote playlist ----
    fun openRemote(ref: PlaylistRef) {
        val d = Detail(ref.name, ref.url)
        detail = d
        changed()
        App.io.execute {
            try {
                val page = Yt.playlistTracks(ref.url)
                App.main.post { if (detail === d) { d.tracks += page.items; d.more = page.more; d.loading = false; if (d.tracks.isEmpty()) d.status = "Empty playlist"; changed() } }
            } catch (e: Throwable) {
                Crash.record("open playlist ${ref.name} ${ref.url}", e)
                App.main.post { if (detail === d) { d.loading = false; d.status = friendly(e); changed() } }
            }
        }
    }

    fun detailLoadMore() {
        val d = detail ?: return
        val more = d.more ?: return
        if (d.loading) return
        d.loading = true
        changed()
        App.io.execute {
            try {
                val page = more()
                App.main.post { if (detail === d) { d.tracks += page.items; d.more = page.more; d.loading = false; changed() } }
            } catch (e: Throwable) {
                App.main.post { if (detail === d) { d.loading = false; changed(); toast(friendly(e)) } }
            }
        }
    }

    fun closeDetail() { detail = null; changed() }

    /** Back to the "For you" page: forget the current search. */
    fun clear() {
        gen++
        query = ""; status = ""; searching = false; detail = null
        songs.clear(); lists.clear(); songsMore = null; listsMore = null
        changed()
    }

    /** Fetches every track (up to 500) of a remote playlist and saves it as a playlist of your own. */
    fun saveRemote(name: String, url: String, onSaved: (Int) -> Unit) {
        toast("Saving “$name”…")
        App.io.execute {
            try {
                val tracks = Yt.allPlaylistTracks(url, 500)
                App.main.post {
                    if (tracks.isEmpty()) toast("Playlist is empty")
                    else { Store.createPlaylist(name, tracks, url); onSaved(tracks.size) }
                }
            } catch (e: Throwable) {
                Crash.record("save playlist $name $url", e)
                App.main.post { toast(friendly(e)) }
            }
        }
    }

    fun toast(msg: String) {
        android.widget.Toast.makeText(App.instance, msg, android.widget.Toast.LENGTH_SHORT).show()
    }
}
