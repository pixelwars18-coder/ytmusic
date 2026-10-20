package dev.ytmusic

import android.app.PendingIntent
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.DefaultDataSource
import androidx.media3.datasource.HttpDataSource
import androidx.media3.datasource.ResolvingDataSource
import androidx.media3.exoplayer.DefaultLoadControl
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.RenderersFactory
import androidx.media3.exoplayer.audio.MediaCodecAudioRenderer
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.extractor.ExtractorsFactory
import androidx.media3.extractor.mkv.MatroskaExtractor
import androidx.media3.extractor.mp4.FragmentedMp4Extractor
import androidx.media3.extractor.mp4.Mp4Extractor
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import dev.ytmusic.core.Yt

/**
 * Background audio. Tracks are queued as `ytm:<videoId>` items and only turned into real stream
 * URLs when ExoPlayer is about to read them (see [Resolver]); ExoPlayer prepares the next item
 * while the current one plays, so track changes are gapless-ish and fast without any extra work.
 */
class PlaybackService : MediaSessionService() {
    private var session: MediaSession? = null
    private val main = Handler(Looper.getMainLooper())
    private var failuresInRow = 0
    private var retriedVid: String? = null

    private class Resolver : ResolvingDataSource.Resolver {
        override fun resolveDataSpec(dataSpec: DataSpec): DataSpec {
            val uri = dataSpec.uri
            if (uri.scheme != "ytm") return dataSpec
            val vid = uri.schemeSpecificPart
            Downloads.file(vid)?.let { return dataSpec.withUri(Uri.fromFile(it)) }
            return dataSpec.withUri(Uri.parse(Yt.resolveChecked(vid).url))
        }
    }

    override fun onCreate() {
        super.onCreate()
        // http(s) goes through the chunked YouTube reader, file: (downloads) through the default sources
        val data = ResolvingDataSource.Factory(DefaultDataSource.Factory(this) { YtChunkedDataSource() }, Resolver())
        // Audio only, and only the containers YouTube audio comes in: smaller and quicker to start.
        // YouTube audio is fragmented ("dash") MP4 or WebM.
        val extractors = ExtractorsFactory { arrayOf(FragmentedMp4Extractor(), Mp4Extractor(), MatroskaExtractor()) }
        val renderers = RenderersFactory { handler, _, audioListener, _, _ ->
            arrayOf(MediaCodecAudioRenderer(this, MediaCodecSelector.DEFAULT, handler, audioListener))
        }
        val load = DefaultLoadControl.Builder()
            .setBufferDurationsMs(20_000, 60_000, 1_500, 3_000)
            .build()
        val player = ExoPlayer.Builder(this, renderers)
            .setMediaSourceFactory(DefaultMediaSourceFactory(data, extractors))
            .setLoadControl(load)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MUSIC).build(), true)
            .setHandleAudioBecomingNoisy(true)
            .setWakeMode(C.WAKE_MODE_NETWORK)
            .build()
        player.addListener(object : Player.Listener {
            override fun onPlayerError(error: PlaybackException) = handleError(player, error)
            override fun onIsPlayingChanged(isPlaying: Boolean) { if (isPlaying) { failuresInRow = 0; retriedVid = null } }
        })
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        session = MediaSession.Builder(this, player).setSessionActivity(open).build()
    }

    private fun handleError(player: ExoPlayer, error: PlaybackException) {
        val vid = player.currentMediaItem?.mediaId
        val http = generateSequence(error as Throwable) { it.cause }.filterIsInstance<HttpDataSource.InvalidResponseCodeException>().firstOrNull()
        // An expired / rejected stream URL: resolve a fresh one and resume where we were, once per track.
        if (vid != null && retriedVid != vid && (http != null || error.errorCode == PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_FAILED)) {
            retriedVid = vid
            Yt.invalidate(vid)
            val pos = player.currentPosition
            player.seekTo(player.currentMediaItemIndex, pos)
            player.prepare()
            return
        }
        Crash.record("playback ${vid ?: ""}", error)
        failuresInRow++
        val title = player.currentMediaItem?.mediaMetadata?.title ?: "track"
        main.post { Toast.makeText(this, "Couldn't play “$title”", Toast.LENGTH_SHORT).show() }
        if (player.hasNextMediaItem() && failuresInRow < 5) {
            player.seekToNextMediaItem()
            player.prepare()
        }
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession? = session

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Swiping the app away stops music that isn't actively playing.
        val p = session?.player
        if (p == null || !p.playWhenReady || p.mediaItemCount == 0) stopSelf()
    }

    override fun onDestroy() {
        session?.run { player.release(); release() }
        session = null
        super.onDestroy()
    }
}
