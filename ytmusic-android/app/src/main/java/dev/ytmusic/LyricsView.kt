package dev.ytmusic

import android.content.Context
import android.graphics.Typeface
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import dev.ytmusic.core.Lyrics
import dev.ytmusic.core.Track

/** Full-screen lyrics: time-synced lines follow the song (tap a line to jump there), or plain text. */
class LyricsView(ctx: Context) : FrameLayout(ctx) {
    var onClose: (() -> Unit)? = null

    private val title = TextView(ctx).apply { setTextColor(C.onSurface); textSize = 17f; typeface = Ui.medium; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }
    private val artist = TextView(ctx).apply { setTextColor(C.onSurfaceVariant); textSize = 13f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }
    private val status = TextView(ctx).apply { setTextColor(C.onSurfaceVariant); textSize = 15f; gravity = Gravity.CENTER; setPadding(Ui.dpi(32), 0, Ui.dpi(32), 0) }
    private val scroll = ScrollView(ctx).apply { isVerticalScrollBarEnabled = false; overScrollMode = View.OVER_SCROLL_NEVER }
    private val body = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL; setPadding(Ui.dpi(24), Ui.dpi(24), Ui.dpi(24), Ui.dpi(200)) }

    private var track: Track? = null
    private var lines: List<Lyrics.Line> = emptyList()
    private var views: List<TextView> = emptyList()
    private var current = -1
    private var lastTouch = 0L
    private val tick = object : Runnable {
        override fun run() { update(); if (visibility == View.VISIBLE) postDelayed(this, 250) }
    }
    private val storeListener = { track?.let { load(it) } ; Unit }

    init {
        setBackgroundColor(C.surface)
        isClickable = true
        val col = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        val head = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(Ui.dpi(8), Ui.dpi(30), Ui.dpi(16), Ui.dpi(8)) }
        head.addView(IconBtn(ctx).apply { icon = "chevronDown"; tint = C.onSurface; setOnClickListener { onClose?.invoke() } }, LinearLayout.LayoutParams(Ui.dpi(44), Ui.dpi(44)))
        val tx = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL; setPadding(Ui.dpi(8), 0, 0, 0) }
        tx.addView(title); tx.addView(artist)
        head.addView(tx, LinearLayout.LayoutParams(0, -2, 1f))
        col.addView(head)
        scroll.addView(body)
        val area = FrameLayout(ctx)
        area.addView(scroll, LayoutParams(-1, -1))
        area.addView(status, LayoutParams(-1, -1))
        col.addView(area, LinearLayout.LayoutParams(-1, 0, 1f))
        addView(col, LayoutParams(-1, -1))
        scroll.setOnTouchListener { _, e -> if (e.action == MotionEvent.ACTION_DOWN || e.action == MotionEvent.ACTION_MOVE) lastTouch = System.currentTimeMillis(); false }
    }

    fun open(t: Track) {
        track = t
        title.text = t.title; artist.text = t.artist
        load(t)
        visibility = View.VISIBLE
        LyricsStore.listeners.add(storeListener)
        removeCallbacks(tick); post(tick)
    }

    fun close() { visibility = View.GONE; LyricsStore.listeners.remove(storeListener); removeCallbacks(tick) }

    private fun load(t: Track) {
        val e = LyricsStore.get(t.vid)
        if (e == null || (e.missing && System.currentTimeMillis() - e.at > 3600_000)) {
            show(null, "Looking for lyrics…")
            App.io.execute {
                val r = LyricsStore.fetchNow(t, force = true)
                App.main.post { if (track?.vid == t.vid) { if (r == null) show(null, "Couldn't reach the lyrics service.\nCheck your connection.") else render(r) } }
            }
        } else render(e)
    }

    private fun render(e: LyricsStore.Entry) {
        when {
            e.lrc.isNotBlank() -> show(Lyrics.parseLrc(e.lrc).filter { it.text.isNotBlank() }, null)
            e.plain.isNotBlank() -> showPlain(e.plain)
            else -> show(null, "No lyrics found for this song")
        }
    }

    private fun show(l: List<Lyrics.Line>?, msg: String?) {
        body.removeAllViews(); views = emptyList(); lines = l ?: emptyList(); current = -1
        status.text = msg ?: ""; status.visibility = if (msg != null) View.VISIBLE else View.GONE
        if (l == null) return
        val vs = ArrayList<TextView>()
        for (line in l) {
            val tv = TextView(context).apply {
                text = line.text; textSize = 22f; typeface = Typeface.DEFAULT_BOLD; setTextColor(C.onSurfaceVariant); alpha = 0.55f
                setPadding(0, Ui.dpi(10), 0, Ui.dpi(10))
                setOnClickListener { Playback.seekTo(line.ms) }
            }
            vs += tv; body.addView(tv)
        }
        views = vs
        update()
    }

    private fun showPlain(text: String) {
        body.removeAllViews(); views = emptyList(); lines = emptyList(); status.visibility = View.GONE
        body.addView(TextView(context).apply { this.text = text; textSize = 18f; setTextColor(C.onSurface); setLineSpacing(0f, 1.25f) })
    }

    private fun update() {
        val cur = Playback.current
        if (cur != null && cur.vid != track?.vid) { open(cur); return }     // the song changed
        if (lines.isEmpty()) return
        val pos = Playback.player?.currentPosition ?: return
        var idx = -1
        for (i in lines.indices) { if (lines[i].ms <= pos + 150) idx = i else break }
        if (idx == current) return
        current = idx
        views.forEachIndexed { i, tv ->
            val on = i == idx
            tv.setTextColor(if (on) C.primary else C.onSurfaceVariant); tv.alpha = if (on) 1f else 0.55f
        }
        if (idx >= 0 && System.currentTimeMillis() - lastTouch > 3000) {
            val tv = views[idx]
            scroll.smoothScrollTo(0, (tv.top + tv.height / 2 - scroll.height / 3).coerceAtLeast(0))
        }
    }
}
