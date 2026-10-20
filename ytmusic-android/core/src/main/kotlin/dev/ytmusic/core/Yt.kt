package dev.ytmusic.core

import okhttp3.OkHttpClient
import org.schabi.newpipe.extractor.InfoItem
import org.schabi.newpipe.extractor.NewPipe
import org.schabi.newpipe.extractor.ServiceList
import org.schabi.newpipe.extractor.StreamingService
import org.schabi.newpipe.extractor.localization.Localization
import org.schabi.newpipe.extractor.playlist.PlaylistInfo
import org.schabi.newpipe.extractor.playlist.PlaylistInfoItem
import org.schabi.newpipe.extractor.search.SearchInfo
import org.schabi.newpipe.extractor.services.youtube.linkHandler.YoutubeStreamLinkHandlerFactory
import org.schabi.newpipe.extractor.stream.AudioStream
import org.schabi.newpipe.extractor.stream.AudioTrackType
import org.schabi.newpipe.extractor.stream.DeliveryMethod
import org.schabi.newpipe.extractor.stream.StreamInfo
import org.schabi.newpipe.extractor.stream.StreamInfoItem
import java.util.concurrent.ConcurrentHashMap

/** YouTube access without an API key: search, playlists / links and stream URL resolution. */
object Yt {
    private val service: StreamingService get() = ServiceList.YouTube
    @Volatile private var ready = false
    private val urlCache = ConcurrentHashMap<String, Audio>()
    /** Preferred audio bitrate in kbps (0 = the highest available). */
    @Volatile var targetKbps = 128
        set(v) { if (v != field) { field = v; urlCache.clear() } }

    val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(15, java.util.concurrent.TimeUnit.SECONDS)
            .readTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
            .build()
    }

    @Synchronized
    fun init() {
        if (ready) return
        NewPipe.init(DownloaderImpl(client), Localization("en", "US"))
        ready = true
    }

    /** Runs [block]; on failure waits a moment and tries once more. */
    private fun <T> retried(block: () -> T): T = try { block() } catch (e: Exception) {
        if (e is IllegalArgumentException) throw e
        Thread.sleep(600)
        block()
    }

    fun watchUrl(vid: String) = "https://www.youtube.com/watch?v=$vid"

    private fun toTrack(i: InfoItem): Track? {
        if (i !is StreamInfoItem) return null
        val id = try { YoutubeStreamLinkHandlerFactory.getInstance().getId(i.url) } catch (e: Exception) { return null }
        return Track(id, i.name ?: "Unknown", i.uploaderName ?: "", maxOf(0L, i.duration))
    }

    /** Free text → search page. A YouTube video / playlist link → that content. */
    fun search(query: String): Page {
        init()
        val q = query.trim()
        if (q.startsWith("http://") || q.startsWith("https://")) return fromLink(q)
        val handler = service.searchQHFactory.fromQuery(q, listOf("videos"), "")
        val info = retried { SearchInfo.getInfo(service, handler) }
        fun page(items: List<InfoItem>, next: org.schabi.newpipe.extractor.Page?): Page =
            Page(items.mapNotNull(::toTrack), if (next == null || !org.schabi.newpipe.extractor.Page.isValid(next)) null else {
                { val p = SearchInfo.getMoreItems(service, handler, next); page(p.items, p.nextPage) }
            })
        return page(info.relatedItems, info.nextPage)
    }

    /** Search for playlists (not videos). */
    fun searchPlaylists(query: String): PlaylistPage {
        init()
        val handler = service.searchQHFactory.fromQuery(query.trim(), listOf("playlists"), "")
        val info = retried { SearchInfo.getInfo(service, handler) }
        fun page(items: List<InfoItem>, next: org.schabi.newpipe.extractor.Page?): PlaylistPage =
            PlaylistPage(items.filterIsInstance<PlaylistInfoItem>().map {
                PlaylistRef(it.url, it.name ?: "Playlist", it.uploaderName ?: "", it.streamCount, it.thumbnails.firstOrNull()?.url ?: "")
            }, if (next == null || !org.schabi.newpipe.extractor.Page.isValid(next)) null else {
                { val p = SearchInfo.getMoreItems(service, handler, next); page(p.items, p.nextPage) }
            })
        return page(info.relatedItems, info.nextPage)
    }

    /** "Radio": YouTube's related videos for a track, for building an endless-feeling queue. */
    fun related(vid: String, limit: Int = 30): List<Track> {
        init()
        val info = retried { StreamInfo.getInfo(service, watchUrl(vid)) }
        return info.relatedItems.mapNotNull(::toTrack).filter { it.vid != vid && it.duration in 30..1200 }.take(limit)
    }

    /** YouTube's own "Mix" (radio) playlist for a song: usually fuller than the related list. */
    fun mix(vid: String, limit: Int = 30): List<Track> {
        init()
        val url = "https://www.youtube.com/watch?v=$vid&list=RD$vid"
        val info = retried { PlaylistInfo.getInfo(service, url) }
        return info.relatedItems.mapNotNull(::toTrack).filter { it.vid != vid && it.duration in 30..1500 }.take(limit)
    }

    /** Search-box autocomplete suggestions. */
    fun suggestions(query: String): List<String> {
        init()
        val q = query.trim()
        if (q.isEmpty()) return emptyList()
        return service.suggestionExtractor.suggestionList(q).take(8)
    }

    /** Tracks of a playlist link, page by page. */
    fun playlistTracks(url: String): Page {
        init()
        return fromLink(url)
    }

    /** Loads up to [limit] tracks of a playlist across pages (used when saving it to the library). */
    fun allPlaylistTracks(url: String, limit: Int = 500, progress: (Int) -> Unit = {}): List<Track> {
        var page: Page? = playlistTracks(url)
        val out = ArrayList<Track>()
        while (page != null && out.size < limit) {
            out += page.items
            progress(out.size)
            page = page.more?.invoke()
        }
        return out.take(limit)
    }

    private fun fromLink(url: String): Page {
        return when (service.getLinkTypeByUrl(url)) {
            StreamingService.LinkType.PLAYLIST -> {
                val info = retried { PlaylistInfo.getInfo(service, url) }
                fun page(items: List<InfoItem>, next: org.schabi.newpipe.extractor.Page?): Page =
                    Page(items.mapNotNull(::toTrack), if (next == null || !org.schabi.newpipe.extractor.Page.isValid(next)) null else {
                        { val p = PlaylistInfo.getMoreItems(service, url, next); page(p.items, p.nextPage) }
                    })
                page(info.relatedItems, info.nextPage)
            }
            StreamingService.LinkType.STREAM -> {
                val ex = service.getStreamExtractor(url)
                ex.fetchPage()
                val id = YoutubeStreamLinkHandlerFactory.getInstance().getId(url)
                Page(listOf(Track(id, ex.name, ex.uploaderName ?: "", maxOf(0L, ex.length))), null)
            }
            else -> throw IllegalArgumentException("Unsupported link")
        }
    }

    /** Best audio-only stream: prefers AAC/M4A (hardware decoded everywhere), ~128 kbps by default. */
    fun resolve(vid: String, forceFresh: Boolean = false): Audio {
        if (!forceFresh) urlCache[vid]?.let { if (!it.expired) return it }
        init()
        val ex = service.getStreamExtractor(watchUrl(vid))
        ex.fetchPage()
        val streams = ex.audioStreams.filter { it.isUrl && it.deliveryMethod == DeliveryMethod.PROGRESSIVE_HTTP }
        if (streams.isEmpty()) throw IllegalStateException("No playable audio stream")
        // Many videos (podcasts especially) carry auto-dubbed tracks in dozens of languages: always take the
        // original recording. Only if there is none, fall back to untyped tracks, then English, then anything.
        val original = streams.filter { it.audioTrackType == AudioTrackType.ORIGINAL }
        val langPool = original
            .ifEmpty { streams.filter { it.audioTrackType == null } }
            .ifEmpty { streams.filter { it.audioTrackType != AudioTrackType.DUBBED } }
            .ifEmpty { streams.filter { it.audioLocale?.language == "en" } }
            .ifEmpty { streams }
        val m4a = langPool.filter { it.format?.suffix == "m4a" }
        val pool = m4a.ifEmpty { langPool }
        // closest to 128 kbps, preferring the higher one on ties
        val best: AudioStream = if (targetKbps <= 0) pool.maxByOrNull { it.averageBitrate }!! else pool.minByOrNull { kotlin.math.abs(it.averageBitrate - targetKbps) * 2 - if (it.averageBitrate >= targetKbps) 1 else 0 }!!
        val url = best.content
        val audio = Audio(
            vid = vid,
            url = url,
            ext = best.format?.suffix ?: "m4a",
            mime = best.format?.mimeType ?: "audio/mp4",
            bitrate = best.averageBitrate,
            expiresAtMs = parseExpiry(url),
        )
        urlCache[vid] = audio
        return audio
    }

    /**
     * Like [resolve], but proves the URL actually serves bytes. Stream URLs are bound to the
     * requesting IP and expire; on dual-stack or roaming networks an occasional 403 is normal, and a
     * fresh resolve fixes it. Returns the audio and how many attempts were needed.
     */
    fun resolveChecked(vid: String, maxAttempts: Int = 3): Audio {
        var last: Exception? = null
        for (attempt in 0 until maxAttempts) {
            try {
                val a = resolve(vid, forceFresh = attempt > 0)
                val req = okhttp3.Request.Builder().url(a.url + "&range=0-1").build()
                val code = client.newCall(req).execute().use { it.code }
                if (code == 200 || code == 206) return a
                urlCache.remove(vid)
                last = java.io.IOException("stream URL rejected (HTTP $code)")
            } catch (e: java.io.IOException) {
                urlCache.remove(vid)
                last = e
            }
        }
        throw last ?: java.io.IOException("resolve failed")
    }

    fun invalidate(vid: String) { urlCache.remove(vid) }

    fun cached(vid: String): Audio? = urlCache[vid]?.takeIf { !it.expired }

    private fun parseExpiry(url: String): Long {
        val m = Regex("[?&]expire=(\\d+)").find(url)
        val epoch = m?.groupValues?.get(1)?.toLongOrNull()
        return if (epoch != null) epoch * 1000 else System.currentTimeMillis() + 3 * 3600_000L
    }
}
