package dev.ytmusic

import android.content.ComponentName
import android.content.Context
import android.net.Uri
import android.os.Bundle
import androidx.core.content.ContextCompat
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import dev.ytmusic.core.Track
import java.util.concurrent.CopyOnWriteArrayList

/**
 * The UI's handle on playback: a MediaController connected to [PlaybackService]. The player's own
 * playlist *is* the queue. Lives for the whole process, so the sleep timer and queue persistence
 * keep working with the activity closed.
 */
object Playback {
    private var ctrl: MediaController? = null
    val player: Player? get() = ctrl
    val listeners = CopyOnWriteArrayList<() -> Unit>()
    private var lastRecorded = ""
    var sleepEndsAt = 0L
        private set
    val speed: Float get() = ctrl?.playbackParameters?.speed ?: 1f

    private val sleepTask = Runnable {
        ctrl?.pause()
        sleepEndsAt = 0
        fire()
    }

    fun connect(ctx: Context) {
        val token = SessionToken(ctx, ComponentName(ctx, PlaybackService::class.java))
        val future = MediaController.Builder(ctx, token).buildAsync()
        future.addListener({
            try {
                val c = future.get()
                ctrl = c
                restore(c)
                c.addListener(object : Player.Listener {
                    override fun onEvents(player: Player, events: Player.Events) {
                        if (events.contains(Player.EVENT_IS_PLAYING_CHANGED) && player.isPlaying) {
                            val t = current
                            if (t != null && t.vid != lastRecorded) { lastRecorded = t.vid; Store.addRecent(t) }
                        }
                        if (events.containsAny(Player.EVENT_TIMELINE_CHANGED, Player.EVENT_MEDIA_ITEM_TRANSITION,
                                Player.EVENT_PLAY_WHEN_READY_CHANGED, Player.EVENT_SHUFFLE_MODE_ENABLED_CHANGED, Player.EVENT_REPEAT_MODE_CHANGED)) persist()
                        if (events.contains(Player.EVENT_PLAYBACK_PARAMETERS_CHANGED)) Unit
                        fire()
                    }
                })
                fire()
            } catch (e: Throwable) { Crash.record("controller connect", e) }
        }, ContextCompat.getMainExecutor(ctx))
    }

    private fun restore(c: MediaController) {
        c.shuffleModeEnabled = Store.shuffle
        c.repeatMode = Store.repeat
        if (c.mediaItemCount == 0 && Store.queue.isNotEmpty()) {
            val idx = Store.queueIndex.coerceIn(0, Store.queue.size - 1)
            c.setMediaItems(Store.queue.map(::item), idx, Store.queuePosMs)
            // not prepared: nothing touches the network until you press play
        }
    }

    private fun persist() {
        val c = ctrl ?: return
        Store.queue = queue()
        Store.queueIndex = c.currentMediaItemIndex.coerceAtLeast(0)
        Store.queuePosMs = c.currentPosition
        Store.shuffle = c.shuffleModeEnabled
        Store.repeat = c.repeatMode
        Store.scheduleSave()
    }

    /** Remember the playback position (called when the app goes to the background). */
    fun savePosition() { persist(); Store.saveNow() }

    private fun fire() = listeners.forEach { it() }

    // ---- items ----
    fun item(t: Track): MediaItem = MediaItem.Builder()
        .setMediaId(t.vid)
        .setUri(Uri.parse("ytm:" + t.vid))
        .setMediaMetadata(
            MediaMetadata.Builder()
                .setTitle(t.title)
                .setArtist(t.artist)
                .setArtworkUri(Uri.parse(Thumbs.trackUrl(t.vid)))
                .setExtras(Bundle().apply { putLong("dur", t.duration) })
                .build()
        ).build()

    fun trackOf(mi: MediaItem) = Track(
        mi.mediaId,
        mi.mediaMetadata.title?.toString() ?: "",
        mi.mediaMetadata.artist?.toString() ?: "",
        mi.mediaMetadata.extras?.getLong("dur") ?: 0L,
    )

    fun queue(): List<Track> {
        val c = ctrl ?: return emptyList()
        return List(c.mediaItemCount) { trackOf(c.getMediaItemAt(it)) }
    }

    val currentVid: String? get() = ctrl?.currentMediaItem?.mediaId
    val current: Track? get() = ctrl?.currentMediaItem?.let(::trackOf)
    val isPlaying get() = ctrl?.let { it.playWhenReady && it.playbackState != Player.STATE_ENDED && it.playbackState != Player.STATE_IDLE } ?: false
    val isBuffering get() = ctrl?.let { it.playWhenReady && it.playbackState == Player.STATE_BUFFERING } ?: false

    // ---- commands ----
    fun play(tracks: List<Track>, start: Int, shuffle: Boolean = false) {
        val c = ctrl ?: return
        if (tracks.isEmpty()) return
        if (shuffle) c.shuffleModeEnabled = true
        val idx = if (shuffle) (tracks.indices).random() else start.coerceIn(0, tracks.size - 1)
        c.setMediaItems(tracks.map(::item), idx, 0)
        c.prepare()
        c.play()
    }

    fun enqueue(tracks: List<Track>) {
        val c = ctrl ?: return
        val wasEmpty = c.mediaItemCount == 0
        c.addMediaItems(tracks.map(::item))
        if (wasEmpty && tracks.isNotEmpty()) { c.prepare(); c.play() }
    }

    fun playNext(t: Track) {
        val c = ctrl ?: return
        if (c.mediaItemCount == 0) { play(listOf(t), 0); return }
        c.addMediaItem(c.currentMediaItemIndex + 1, item(t))
    }

    fun skipTo(index: Int) {
        val c = ctrl ?: return
        if (index !in 0 until c.mediaItemCount) return
        c.seekToDefaultPosition(index)
        c.prepare()
        c.play()
    }

    fun removeAt(index: Int) { ctrl?.let { if (index in 0 until it.mediaItemCount) it.removeMediaItem(index) } }
    fun clearQueue() { ctrl?.let { it.stop(); it.clearMediaItems() } }

    fun toggle() {
        val c = ctrl ?: return
        if (c.mediaItemCount == 0) return
        when {
            c.playbackState == Player.STATE_ENDED -> { c.seekToDefaultPosition(0); c.play() }
            c.playbackState == Player.STATE_IDLE -> { c.prepare(); c.play() }
            c.playWhenReady -> c.pause()
            else -> c.play()
        }
    }

    fun next() { ctrl?.let { if (it.hasNextMediaItem()) { it.seekToNextMediaItem(); if (it.playbackState == Player.STATE_IDLE) it.prepare(); it.play() } } }
    fun prev() { ctrl?.let { it.seekToPrevious(); if (it.playbackState == Player.STATE_IDLE) it.prepare(); it.play() } }
    fun seekTo(ms: Long) { ctrl?.seekTo(ms) }
    fun seekBy(deltaMs: Long) {
        val c = ctrl ?: return
        val dur = c.duration
        val target = c.currentPosition + deltaMs
        c.seekTo(if (dur > 0) target.coerceIn(0, dur) else maxOf(0, target))
    }
    /** Podcast-friendly speeds; the player keeps the speed across tracks. */
    fun cycleSpeed() {
        val c = ctrl ?: return
        val steps = floatArrayOf(1f, 1.25f, 1.5f, 1.75f, 2f, 0.75f)
        val i = steps.indexOfFirst { Math.abs(it - speed) < 0.01f }
        c.setPlaybackSpeed(steps[(i + 1) % steps.size])
        fire()
    }
    fun move(from: Int, to: Int) { ctrl?.moveMediaItem(from, to) }
    fun toggleShuffle() { ctrl?.let { it.shuffleModeEnabled = !it.shuffleModeEnabled } }
    fun cycleRepeat() {
        ctrl?.let {
            it.repeatMode = when (it.repeatMode) {
                Player.REPEAT_MODE_OFF -> Player.REPEAT_MODE_ALL
                Player.REPEAT_MODE_ALL -> Player.REPEAT_MODE_ONE
                else -> Player.REPEAT_MODE_OFF
            }
        }
    }

    fun setSleep(minutes: Int) {
        App.main.removeCallbacks(sleepTask)
        if (minutes > 0) {
            sleepEndsAt = System.currentTimeMillis() + minutes * 60_000L
            App.main.postDelayed(sleepTask, minutes * 60_000L)
        } else sleepEndsAt = 0
        fire()
    }
}
