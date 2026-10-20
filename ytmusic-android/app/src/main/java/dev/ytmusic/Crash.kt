package dev.ytmusic

import android.content.Context
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Writes uncaught exceptions to a file the user can read (in-app: ⋮ menu → "Last error"). */
object Crash {
    private lateinit var file: File

    fun install(ctx: Context) {
        file = File(ctx.getExternalFilesDir(null) ?: ctx.filesDir, "crash.log")
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e -> record("Uncaught on ${t.name}", e); prev?.uncaughtException(t, e) }
    }

    fun record(what: String, e: Throwable) {
        try {
            val sw = StringWriter()
            e.printStackTrace(PrintWriter(sw))
            val stamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
            file.writeText("$stamp  $what\n[${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}, Android ${android.os.Build.VERSION.RELEASE} (API ${android.os.Build.VERSION.SDK_INT})]\n$sw")
        } catch (_: Throwable) { }
    }

    fun last(): String? = try { if (::file.isInitialized && file.exists()) file.readText().take(6000) else null } catch (_: Throwable) { null }
    fun clear() { try { file.delete() } catch (_: Throwable) { } }
}
