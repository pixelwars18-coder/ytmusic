package dev.ytmusic

import android.app.ActivityManager
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import dev.ytmusic.core.Yt
import okhttp3.Request
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors

/**
 * Small image loader for thumbnails: disk cache + a few MB of RGB_565 bitmaps in memory,
 * decoded at (about) the size they are drawn at. Built for 1 GB tablets.
 */
object Thumbs {
    private lateinit var dir: File
    private lateinit var cache: LruCache<String, Bitmap>
    private val pending = HashSet<String>()
    private val failed = HashMap<String, Long>()   // key -> when it failed; retried after a while
    private val exec = Executors.newFixedThreadPool(2)
    val listeners = CopyOnWriteArrayList<() -> Unit>()
    @Volatile private var notifyQueued = false

    fun init(ctx: Context) {
        dir = File(ctx.cacheDir, "thumbs").apply { mkdirs() }
        val low = (ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).isLowRamDevice
        val bytes = if (low) 3 * 1024 * 1024 else 8 * 1024 * 1024
        cache = object : LruCache<String, Bitmap>(bytes) {
            override fun sizeOf(key: String, value: Bitmap) = value.byteCount
        }
        exec.execute { // drop thumbnails untouched for a month
            val limit = System.currentTimeMillis() - 30L * 24 * 3600 * 1000
            dir.listFiles()?.forEach { if (it.lastModified() < limit) it.delete() }
        }
    }

    fun diskBytes(): Long = dir.listFiles()?.sumOf { it.length() } ?: 0L

    fun clearDisk() {
        cache.evictAll()
        dir.listFiles()?.forEach { it.delete() }
        notifyQueued = false
        listeners.forEach { it() }
    }

    fun trackUrl(vid: String, big: Boolean = false) =
        "https://i.ytimg.com/vi/$vid/${if (big) "hqdefault" else "mqdefault"}.jpg"

    /** Returns the bitmap if ready, else null and schedules loading (listeners fire when it arrives). */
    fun get(key: String, url: String, maxWidthPx: Int): Bitmap? {
        if (!Settings.thumbnails) return null
        cache.get(key)?.let { return it }
        if (url.isEmpty()) return null
        synchronized(pending) {
            val failedAt = failed[key]
            if (failedAt != null && System.currentTimeMillis() - failedAt < 20_000) return null
            if (!pending.add(key)) return null
            failed.remove(key)
        }
        exec.execute { load(key, url, maxWidthPx) }
        return null
    }

    fun track(vid: String, maxWidthPx: Int) = get("t:$vid", trackUrl(vid), maxWidthPx)
    fun trackBig(vid: String, maxWidthPx: Int) = get("b:$vid", trackUrl(vid, true), maxWidthPx)

    private fun load(key: String, url: String, maxW: Int) {
        try {
            val f = File(dir, Integer.toHexString(key.hashCode()) + ".jpg")
            if (!f.exists() || f.length() == 0L) {
                val tmp = File(dir, f.name + ".part")
                Yt.client.newCall(Request.Builder().url(url).build()).execute().use { r ->
                    if (!r.isSuccessful) throw java.io.IOException("HTTP ${r.code}")
                    tmp.outputStream().use { out -> r.body!!.byteStream().copyTo(out) }
                }
                tmp.renameTo(f)
            } else f.setLastModified(System.currentTimeMillis())

            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(f.path, bounds)
            var sample = 1
            while (maxW > 0 && bounds.outWidth / (sample * 2) >= maxW) sample *= 2
            val opts = BitmapFactory.Options().apply {
                inSampleSize = sample
                inPreferredConfig = Bitmap.Config.RGB_565
            }
            val bmp = BitmapFactory.decodeFile(f.path, opts) ?: throw java.io.IOException("decode failed")
            cache.put(key, bmp)
            notifyChanged()
        } catch (e: Throwable) {
            android.util.Log.w("Thumbs", "failed $key: $e")
            synchronized(pending) { failed[key] = System.currentTimeMillis() }
        } finally {
            synchronized(pending) { pending.remove(key) }
        }
    }

    private fun notifyChanged() {
        if (notifyQueued) return
        notifyQueued = true
        App.main.postDelayed({
            notifyQueued = false
            listeners.forEach { it() }
        }, 80)
    }
}
