package dev.ytmusic

import android.content.Context
import android.content.Intent
import android.os.Build
import dev.ytmusic.core.Track
import dev.ytmusic.core.Yt
import okhttp3.Request
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Offline copies of tracks, stored as plain audio files in the app's own music folder
 * (no storage permission needed). One download at a time; a foreground service keeps the
 * process alive while the queue is busy.
 */
object Downloads {
    private class Meta(val track: Track, val file: String, val bytes: Long)

    private lateinit var dir: File
    private lateinit var index: File
    private val done = LinkedHashMap<String, Meta>()
    /** vid -> percent 0..100 (-1 = waiting in the queue) */
    private val active = ConcurrentHashMap<String, Int>()
    private val queue = java.util.concurrent.LinkedBlockingDeque<Track>()
    private val cancelled = ConcurrentHashMap.newKeySet<String>()
    private val worker = Executors.newSingleThreadExecutor()
    private val running = AtomicBoolean(false)
    val listeners = CopyOnWriteArrayList<() -> Unit>()
    @Volatile var statusLine: String = ""
        private set

    fun init(ctx: Context) {
        dir = File(ctx.getExternalFilesDir("Music") ?: File(ctx.filesDir, "music"), "").apply { mkdirs() }
        index = File(ctx.filesDir, "downloads.json")
        try {
            if (index.exists()) {
                val a = JSONArray(index.readText())
                for (i in 0 until a.length()) {
                    val o = a.getJSONObject(i)
                    val t = Track(o.getString("v"), o.optString("t"), o.optString("a"), o.optLong("d"))
                    val f = File(dir, o.getString("f"))
                    if (f.exists()) done[t.vid] = Meta(t, f.name, f.length())
                }
            }
        } catch (e: Throwable) { Crash.record("downloads load", e) }
        // stale partial files from a killed process
        dir.listFiles()?.filter { it.name.endsWith(".part") }?.forEach { it.delete() }
    }

    fun file(vid: String): File? = done[vid]?.let { File(dir, it.file) }?.takeIf { it.exists() }
    fun isDone(vid: String) = done.containsKey(vid)
    /** null = not downloading, -1 = queued, 0..100 = progress */
    fun progress(vid: String): Int? = active[vid]
    fun tracks(): List<Track> = done.values.map { it.track }.reversed()
    fun totalBytes(): Long = done.values.sumOf { it.bytes }
    fun busy() = active.isNotEmpty()
    fun pendingCount() = active.size
    fun currentPercent(): Int = active.values.firstOrNull { it >= 0 } ?: -1

    fun enqueue(ctx: Context, tracks: List<Track>) {
        if (!Settings.downloadsAllowed()) {
            android.widget.Toast.makeText(ctx, "Downloads are set to Wi-Fi only", android.widget.Toast.LENGTH_LONG).show()
            return
        }
        var added = 0
        for (t in tracks) {
            if (isDone(t.vid) || active.containsKey(t.vid)) continue
            active[t.vid] = -1
            queue.add(t)
            added++
        }
        if (added == 0) return
        notifyChanged()
        startService(ctx)
        if (running.compareAndSet(false, true)) worker.execute { loop() }
    }

    fun cancel(vid: String) {
        cancelled.add(vid)
        if (queue.removeAll { it.vid == vid }) { active.remove(vid); cancelled.remove(vid); notifyChanged() }
    }

    fun cancelAll() { queue.map { it.vid }.forEach { cancel(it) }; active.keys.forEach { cancelled.add(it) } }

    fun delete(vid: String) {
        done.remove(vid)?.let { File(dir, it.file).delete() }
        saveIndex()
        notifyChanged()
    }

    /** Deletes several downloaded songs (and cancels any of them still downloading). */
    fun deleteMany(vids: Collection<String>) {
        vids.forEach { v -> if (active.containsKey(v)) cancel(v) }
        var any = false
        for (v in vids) done.remove(v)?.let { File(dir, it.file).delete(); any = true }
        if (any) saveIndex()
        notifyChanged()
    }

    /** Deletes every download, and stops the ones in progress. Returns the bytes freed. */
    fun deleteAll(): Long {
        val freed = totalBytes()
        cancelAll()
        deleteMany(done.keys.toList())
        return freed
    }

    private fun startService(ctx: Context) {
        val i = Intent(ctx, DownloadService::class.java)
        if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
    }

    private fun loop() {
        try {
            while (true) {
                val t = queue.poll() ?: break
                statusLine = t.title
                try {
                    download(t)
                } catch (e: Throwable) {
                    if (e !is CancelledException) Crash.record("download ${t.vid}", e)
                } finally {
                    active.remove(t.vid)
                    cancelled.remove(t.vid)
                    notifyChanged()
                }
            }
        } finally {
            running.set(false)
            statusLine = ""
            notifyChanged()
            // a track may have been queued between poll() and running=false
            if (queue.isNotEmpty() && running.compareAndSet(false, true)) worker.execute { loop() }
        }
    }

    private const val CHUNK = 1_000_000L

    private class CancelledException : IOException("cancelled")

    private fun sanitize(s: String) = s.replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), "_").trim().take(80)

    private fun download(t: Track) {
        var lastErr: Exception? = null
        for (attempt in 0 until 3) {
            if (t.vid in cancelled) throw CancelledException()
            try {
                val audio = Yt.resolveChecked(t.vid)
                val name = sanitize(t.title).ifEmpty { "track" } + " [" + t.vid + "]." + audio.ext
                val part = File(dir, "$name.part")
                part.delete()
                // Chunked via the `range=` URL parameter: unchunked requests are throttled to ~30 KB/s.
                val total = Regex("[?&]clen=(\\d+)").find(audio.url)?.groupValues?.get(1)?.toLongOrNull() ?: -1L
                var got = 0L
                var lastPct = -1
                part.outputStream().use { out ->
                    val buf = ByteArray(32 * 1024)
                    while (total < 0 || got < total) {
                        if (t.vid in cancelled) throw CancelledException()
                        val end = if (total >= 0) minOf(got + CHUNK - 1, total - 1) else got + CHUNK - 1
                        val req = Request.Builder().url("${audio.url}&range=$got-$end").build()
                        var chunkGot = 0L
                        Yt.client.newCall(req).execute().use { r ->
                            if (r.code == 416 && total < 0) return@use
                            if (!r.isSuccessful) throw IOException("HTTP ${r.code}")
                            val src = (r.body ?: throw IOException("empty response")).byteStream()
                            while (true) {
                                if (t.vid in cancelled) throw CancelledException()
                                val n = src.read(buf)
                                if (n < 0) break
                                out.write(buf, 0, n)
                                got += n; chunkGot += n
                                if (total > 0) {
                                    val pct = (got * 100 / total).toInt()
                                    if (pct != lastPct) { lastPct = pct; active[t.vid] = pct; notifyChanged() }
                                }
                            }
                        }
                        if (chunkGot == 0L) break           // unknown total and nothing more to read
                        if (total < 0 && chunkGot < CHUNK) break
                    }
                }
                if (total > 0 && got < total) throw IOException("incomplete download")
                val target = File(dir, name)
                target.delete()
                if (!part.renameTo(target)) throw IOException("could not finalize file")
                synchronized(done) { done[t.vid] = Meta(t, target.name, target.length()) }
                saveIndex()
                return
            } catch (e: CancelledException) {
                File(dir, "").listFiles()?.filter { it.name.endsWith(".part") && it.name.contains(t.vid) }?.forEach { it.delete() }
                throw e
            } catch (e: Exception) {
                lastErr = e
                Yt.invalidate(t.vid)
            }
        }
        throw lastErr ?: IOException("download failed")
    }

    private fun saveIndex() {
        try {
            val a = JSONArray()
            synchronized(done) {
                done.values.forEach { m ->
                    a.put(JSONObject().put("v", m.track.vid).put("t", m.track.title).put("a", m.track.artist).put("d", m.track.duration).put("f", m.file))
                }
            }
            index.writeText(a.toString())
        } catch (e: Throwable) { Crash.record("downloads save", e) }
    }

    @Volatile private var notifyQueued = false
    private fun notifyChanged() {
        if (notifyQueued) return
        notifyQueued = true
        App.main.postDelayed({ notifyQueued = false; listeners.forEach { it() } }, 250)
    }
}
