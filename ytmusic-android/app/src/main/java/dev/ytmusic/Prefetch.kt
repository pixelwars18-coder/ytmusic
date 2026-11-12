package dev.ytmusic

import android.content.Context
import android.net.ConnectivityManager
import dev.ytmusic.core.Track
import dev.ytmusic.core.Yt
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

/**
 * Looks up the stream URL of songs that are likely to be played next (the top search results, the first
 * songs of an opened playlist, the song the app was closed on) before they are tapped. Starting a song is
 * mostly waiting for that lookup (1–2 s); with the URL already in [Yt]'s cache playback starts right away.
 *
 * Kept small on purpose: a couple of lookups at a time, one song only on metered connections, and nothing
 * at all in data-saver quality.
 */
object Prefetch {
    private val pending = HashSet<String>()
    private val exec = ThreadPoolExecutor(2, 2, 15, TimeUnit.SECONDS, LinkedBlockingQueue<Runnable>()).apply {
        allowCoreThreadTimeOut(true)
    }

    private fun metered(): Boolean = try {
        (App.instance.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).isActiveNetworkMetered
    } catch (_: Throwable) { true }

    /** The first [max] of [tracks] (just the first one on a metered connection). */
    fun warm(tracks: List<Track>, max: Int = 3) {
        if (tracks.isEmpty() || Settings.audioQuality == 0) return
        val n = if (metered()) 1 else max
        for (i in 0 until minOf(n, tracks.size)) warm(tracks[i].vid)
    }

    private fun warm(vid: String) {
        if (Yt.cached(vid) != null || Downloads.file(vid) != null) return
        synchronized(pending) { if (pending.size >= 6 || !pending.add(vid)) return }
        exec.execute {
            try { Yt.resolve(vid) } catch (_: Throwable) { } finally { synchronized(pending) { pending.remove(vid) } }
        }
    }
}
