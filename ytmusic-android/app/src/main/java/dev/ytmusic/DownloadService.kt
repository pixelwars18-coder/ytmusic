package dev.ytmusic

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder

/** Foreground service that exists only to keep downloads alive (and show their progress) in the background. */
class DownloadService : Service() {
    private val tick = { update() }

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= 26) {
            val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(NotificationChannel(CHANNEL, "Downloads", NotificationManager.IMPORTANCE_LOW))
        }
        startForeground(ID, build(0, 0))
        Downloads.listeners.add(tick)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        update()
        return START_NOT_STICKY
    }

    private fun update() {
        if (!Downloads.busy()) { stopForeground(true); stopSelf(); return }
        val total = Downloads.pendingCount()
        (getSystemService(NOTIFICATION_SERVICE) as NotificationManager).notify(ID, build(total, Downloads.currentPercent()))
    }

    private fun build(count: Int, percent: Int): Notification {
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val b = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CHANNEL) else @Suppress("DEPRECATION") Notification.Builder(this)
        b.setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(if (count > 1) "Downloading $count songs" else "Downloading")
            .setContentText(Downloads.statusLine)
            .setOngoing(true)
            .setContentIntent(open)
        if (percent in 0..100) b.setProgress(100, percent, false) else b.setProgress(0, 0, true)
        return b.build()
    }

    override fun onDestroy() {
        Downloads.listeners.remove(tick)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        private const val CHANNEL = "downloads"
        private const val ID = 4201
    }
}
