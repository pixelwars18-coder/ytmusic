package dev.ytmusic

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import dev.ytmusic.core.Yt
import okhttp3.Request
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit

/**
 * Self-update from a small server on the home network.
 *
 * The server hosts `version.json` ({versionCode, versionName, apk, sha256, notes}) next to the APK.
 * Android never lets an app install silently: we download, verify the SHA-256, and open the system
 * installer, where you tap Install. The new APK must be signed with the same key to replace this app.
 */
object Updater {
    class Info(val code: Int, val name: String, val apkUrl: String, val sha256: String, val notes: String)

    const val PORT = 8765
    const val DEFAULT_URL = "https://github.com/pixelwars18-coder/ytmusic/releases/latest/download/"

    @Volatile var available: Info? = null
        private set
    /** -1 idle, 0..100 downloading */
    @Volatile var progress = -1
        private set
    val listeners = CopyOnWriteArrayList<() -> Unit>()
    private val client by lazy { Yt.client.newBuilder().callTimeout(6, TimeUnit.SECONDS).build() }

    private fun prefs() = App.instance.getSharedPreferences("updater", Context.MODE_PRIVATE)
    var baseUrl: String
        get() = prefs().getString("base", DEFAULT_URL) ?: DEFAULT_URL   // (old "url" key held the home-PC address: ignored)
        set(v) { prefs().edit().putString("base", normalize(v)).apply() }

    private fun normalize(u: String): String {
        var s = u.trim()
        if (s.isEmpty()) return DEFAULT_URL
        if (!s.startsWith("http://") && !s.startsWith("https://")) s = "http://$s"
        val hostPart = s.substringAfter("://").substringBefore("/")
        if (s.startsWith("http://") && !hostPart.contains(":")) s = s.replaceFirst(hostPart, "$hostPart:$PORT")   // bare address: use the default port
        return if (s.endsWith("/")) s else "$s/"
    }

    fun currentCode(): Int = try {
        val pi = App.instance.packageManager.getPackageInfo(App.instance.packageName, 0)
        if (Build.VERSION.SDK_INT >= 28) pi.longVersionCode.toInt() else @Suppress("DEPRECATION") pi.versionCode
    } catch (_: Throwable) { 0 }

    fun currentName(): String = try {
        App.instance.packageManager.getPackageInfo(App.instance.packageName, 0).versionName ?: "?"
    } catch (_: Throwable) { "?" }

    private fun fire() = App.main.post { listeners.forEach { it() } }

    /** Quiet check at most every 6 hours (app start). */
    fun autoCheck() {
        if (!dev.ytmusic.Settings.autoUpdateCheck) return
        val last = prefs().getLong("last", 0)
        if (System.currentTimeMillis() - last < 6 * 3600_000L) return
        check(manual = false)
    }

    private fun fetchManifest(base: String): JSONObject? = dev.ytmusic.core.UpdateClient.fetchManifest(client, base)

    /** Home-network addresses change (DHCP): when the saved server doesn't answer, scan the local network for it. */
    private fun discover(): String? = dev.ytmusic.core.UpdateClient.discover(client, PORT)

    /** [onResult] gets a short human message (manual checks only). */
    fun check(manual: Boolean, onResult: ((String) -> Unit)? = null) {
        App.io.execute {
            var msg: String
            try {
                var base = baseUrl
                var o = fetchManifest(base)
                if (o == null && base.startsWith("http://")) {   // a home-network server moved: look around the network
                    val d = discover()
                    android.util.Log.i("Updater", "saved server $base did not answer; scan found: $d")
                    if (d != null) { base = d; baseUrl = d; o = fetchManifest(d) }
                }
                if (o == null) throw java.io.IOException("no update server found")
                val code = o.getInt("versionCode")
                prefs().edit().putLong("last", System.currentTimeMillis()).apply()
                if (code > currentCode()) {
                    available = Info(code, o.optString("versionName", code.toString()), base + o.getString("apk"),
                        o.optString("sha256"), o.optString("notes"))
                    msg = "Update available: v${available!!.name}"
                } else {
                    available = null
                    msg = "You're up to date (v${currentName()})"
                }
            } catch (e: Throwable) {
                msg = if (e is java.io.IOException) "Couldn't reach the update server (check your internet connection)" else "Update check failed: ${e.message}"
                if (manual) Crash.record("update check ${baseUrl}", e)
            }
            fire()
            if (manual) App.main.post { onResult?.invoke(msg) }
        }
    }

    /** Downloads the update and opens the installer. [onMessage] gets problems to show the user. */
    fun downloadAndInstall(onMessage: (String) -> Unit) {
        val info = available ?: return
        if (progress >= 0) return
        if (Build.VERSION.SDK_INT >= 26 && !App.instance.packageManager.canRequestPackageInstalls()) {
            onMessage("Allow “Install unknown apps” for YT Music, then tap the update again")
            val i = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + App.instance.packageName))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            try { App.instance.startActivity(i) } catch (_: Throwable) { }
            return
        }
        progress = 0
        fire()
        App.io.execute {
            try {
                val dir = updateDir()
                val apk = File(dir, "update.apk")
                apk.delete()
                val md = MessageDigest.getInstance("SHA-256")
                val dl = Yt.client.newBuilder().callTimeout(0, TimeUnit.SECONDS).build()
                dl.newCall(Request.Builder().url(info.apkUrl).build()).execute().use { r ->
                    if (!r.isSuccessful) throw java.io.IOException("HTTP ${r.code}")
                    val body = r.body!!
                    val total = body.contentLength()
                    var got = 0L
                    var last = -1
                    apk.outputStream().use { out ->
                        val src = body.byteStream()
                        val buf = ByteArray(32 * 1024)
                        while (true) {
                            val n = src.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n); md.update(buf, 0, n); got += n
                            if (total > 0) { val p = (got * 100 / total).toInt(); if (p != last) { last = p; progress = p; fire() } }
                        }
                    }
                }
                if (info.sha256.isNotEmpty()) {
                    val hex = md.digest().joinToString("") { "%02x".format(it) }
                    if (!hex.equals(info.sha256, ignoreCase = true)) { apk.delete(); throw java.io.IOException("download is corrupt (checksum mismatch)") }
                }
                progress = -1
                fire()
                App.main.post { try { openInstaller(apk, onMessage) } catch (e: Throwable) { Crash.record("update install step", e) } }
            } catch (e: Throwable) {
                progress = -1
                Crash.record("update download ${info.apkUrl}", e)
                fire()
                App.main.post { onMessage("Update download failed: ${e.message}") }
            }
        }
    }

    /**
     * Android 7+ hands the installer a content:// URI (FileProvider) to a private file. Android 5/6
     * installers only understand file:// URIs, which must point at world-readable external storage.
     */
    private fun updateDir(): File {
        val ctx = App.instance
        val base = if (Build.VERSION.SDK_INT >= 24) ctx.filesDir else (ctx.getExternalFilesDir(null) ?: ctx.filesDir)
        return File(base, "updates").apply { mkdirs() }
    }

    private fun openInstaller(apk: File, onMessage: (String) -> Unit) {
        try {
            val ctx = App.instance
            val mime = "application/vnd.android.package-archive"
            val i = Intent(Intent.ACTION_VIEW).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            if (Build.VERSION.SDK_INT >= 24) {
                i.setDataAndType(FileProvider.getUriForFile(ctx, ctx.packageName + ".files", apk), mime)
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } else {
                apk.setReadable(true, false)
                i.setDataAndType(Uri.fromFile(apk), mime)
            }
            ctx.startActivity(i)
        } catch (e: Throwable) {
            Crash.record("open installer ${apk.path} (${apk.length()} bytes)", e)
            onMessage("Couldn't open the installer (${e.javaClass.simpleName}). See \u22ee \u2192 Last error")
        }
    }
}
