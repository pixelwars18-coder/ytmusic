package dev.ytmusic

import android.app.Application
import android.os.Handler
import android.os.Looper
import dev.ytmusic.core.Yt
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ThreadPoolExecutor

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
        Ui.density = resources.displayMetrics.density
        Ui.compact = resources.configuration.smallestScreenWidthDp < 480
        C.apply(getSharedPreferences("ui", MODE_PRIVATE).getString("theme", "midnight") ?: "midnight")
        Crash.install(this)
        Settings.applyQuality()
        Store.init(this)
        Thumbs.init(this)
        ForYou.init(this)
        Downloads.init(this)
        Playback.connect(this)
        io.execute { try { Yt.init() } catch (_: Throwable) { } }
    }

    companion object {
        lateinit var instance: App
        val main = Handler(Looper.getMainLooper())
        /** Network / disk work. Threads exit when idle so an idle app holds no extra stacks. */
        val io: ExecutorService = (Executors.newFixedThreadPool(3) as ThreadPoolExecutor).apply {
            setKeepAliveTime(20, java.util.concurrent.TimeUnit.SECONDS)
            allowCoreThreadTimeOut(true)
        }
    }
}
