package dev.ytmusic

import android.net.Uri
import androidx.media3.common.C
import androidx.media3.datasource.BaseDataSource
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.HttpDataSource
import dev.ytmusic.core.Yt
import okhttp3.Request
import okhttp3.Response
import java.io.IOException
import java.io.InputStream

/**
 * Reads a YouTube stream URL in 1 MB pieces using YouTube's own `range=` URL parameter.
 *
 * A single open-ended request (what ExoPlayer's stock HTTP source sends) is throttled by YouTube to
 * roughly the playback bitrate (~30 KB/s measured), which stalls playback and downloads; many small ranged
 * requests are not throttled (20+ MB/s measured).
 */
class YtChunkedDataSource : BaseDataSource(/* isNetwork = */ true) {
    private var spec: DataSpec? = null
    private var uri: Uri? = null
    private var total = -1L                 // `clen` from the URL, -1 if unknown
    private var position = 0L
    private var remaining = C.LENGTH_UNSET.toLong()
    private var response: Response? = null
    private var stream: InputStream? = null
    private var chunkLeft = 0L
    private var opened = false

    override fun open(dataSpec: DataSpec): Long {
        spec = dataSpec
        uri = dataSpec.uri
        transferInitializing(dataSpec)
        total = dataSpec.uri.getQueryParameter("clen")?.toLongOrNull() ?: -1L
        position = dataSpec.position
        remaining = when {
            dataSpec.length != C.LENGTH_UNSET.toLong() -> dataSpec.length
            total >= 0 -> maxOf(0L, total - position)
            else -> C.LENGTH_UNSET.toLong()
        }
        opened = true
        transferStarted(dataSpec)
        return remaining
    }

    private fun openChunk(): Boolean {
        val u = uri ?: return false
        if (total >= 0 && position >= total) return false
        var end = position + CHUNK - 1
        if (total >= 0) end = minOf(end, total - 1)
        val req = Request.Builder().url("$u&range=$position-$end").header("User-Agent", dev.ytmusic.core.DownloaderImpl.USER_AGENT).build()
        try {
            val r = Yt.client.newCall(req).execute()
            if (r.code == 416) { r.close(); return false }
            if (!r.isSuccessful) {
                val code = r.code; val msg = r.message; r.close()
                throw HttpDataSource.InvalidResponseCodeException(code, msg, null, emptyMap(), spec!!, ByteArray(0))
            }
            response = r
            stream = r.body!!.byteStream()
            val len = r.body!!.contentLength()
            chunkLeft = if (len >= 0) len else end - position + 1
            return true
        } catch (e: IOException) {
            if (e is HttpDataSource.HttpDataSourceException) throw e
            throw HttpDataSource.HttpDataSourceException.createForIOException(e, spec!!, HttpDataSource.HttpDataSourceException.TYPE_OPEN)
        }
    }

    private fun closeChunk() {
        try { stream?.close() } catch (_: IOException) { }
        response?.close()
        stream = null; response = null; chunkLeft = 0
    }

    override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
        if (length == 0) return 0
        if (remaining == 0L) return C.RESULT_END_OF_INPUT
        var guard = 0
        while (true) {
            if (stream == null || chunkLeft <= 0) {
                closeChunk()
                if (!openChunk()) return C.RESULT_END_OF_INPUT
            }
            val want = minOf(length.toLong(), chunkLeft, if (remaining == C.LENGTH_UNSET.toLong()) Long.MAX_VALUE else remaining).toInt()
            val n = try {
                stream!!.read(buffer, offset, want)
            } catch (e: IOException) {
                throw HttpDataSource.HttpDataSourceException.createForIOException(e, spec!!, HttpDataSource.HttpDataSourceException.TYPE_READ)
            }
            if (n < 0) { // chunk ended early (end of the file when the total size is unknown)
                closeChunk()
                if (total < 0 || ++guard > 3) { remaining = 0; return C.RESULT_END_OF_INPUT }
                continue
            }
            position += n
            chunkLeft -= n
            if (remaining != C.LENGTH_UNSET.toLong()) remaining -= n
            bytesTransferred(n)
            return n
        }
    }

    override fun getUri(): Uri? = uri
    override fun getResponseHeaders(): Map<String, List<String>> = emptyMap()

    override fun close() {
        closeChunk()
        if (opened) { opened = false; transferEnded() }
        uri = null
    }

    companion object { private const val CHUNK = 1_000_000L }
}
