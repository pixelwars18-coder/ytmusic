package dev.ytmusic

import android.content.Context
import android.net.ConnectivityManager

/** Small user preferences (separate from the library state in [Store]). */
object Settings {
    private fun prefs() = App.instance.getSharedPreferences("settings", Context.MODE_PRIVATE)

    var wifiOnlyDownloads: Boolean
        get() = prefs().getBoolean("wifiOnly", false)
        set(v) { prefs().edit().putBoolean("wifiOnly", v).apply() }

    var autoUpdateCheck: Boolean
        get() = prefs().getBoolean("autoUpdate", true)
        set(v) { prefs().edit().putBoolean("autoUpdate", v).apply() }

    /** True when downloads are allowed on the current connection. */
    fun downloadsAllowed(): Boolean {
        if (!wifiOnlyDownloads) return true
        val cm = App.instance.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        return !cm.isActiveNetworkMetered
    }

    /** 0 = data saver (~48 kbps), 1 = normal (~128 kbps), 2 = best available. */
    var audioQuality: Int
        get() = prefs().getInt("quality", 1)
        set(v) { prefs().edit().putInt("quality", v).apply(); applyQuality() }

    fun applyQuality() { dev.ytmusic.core.Yt.targetKbps = when (audioQuality) { 0 -> 48; 2 -> 0; else -> 128 } }

    /** Tab to open on launch; "LAST" restores the one used last. */
    var startTab: String
        get() = prefs().getString("startTab", "LAST") ?: "LAST"
        set(v) { prefs().edit().putString("startTab", v).apply() }

    var keepScreenOn: Boolean
        get() = prefs().getBoolean("keepOn", false)
        set(v) { prefs().edit().putBoolean("keepOn", v).apply() }

    var thumbnails: Boolean
        get() = prefs().getBoolean("thumbs", true)
        set(v) { prefs().edit().putBoolean("thumbs", v).apply() }

    /** Fetch lyrics for every downloaded song (new downloads, and ones that don't have them yet). */
    var lyricsForDownloads: Boolean
        get() = prefs().getBoolean("lyricsDl", true)
        set(v) { prefs().edit().putBoolean("lyricsDl", v).apply() }
}
