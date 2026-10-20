package dev.ytmusic

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.text.TextPaint
import android.text.TextUtils
import android.view.MotionEvent
import android.view.VelocityTracker
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import androidx.media3.common.Player
import dev.ytmusic.core.Track
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

private fun paint(color: Int = 0, style: Paint.Style = Paint.Style.FILL) = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { this.color = color; this.style = style }

private val shaderPaint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
private val shaderMatrix = Matrix()
private val fillP = paint()

/**
 * Draws [bmp] center-cropped into [r] with rounded corners; a null bitmap draws a placeholder.
 * [crop169]: the bitmap is a YouTube hqdefault (16:9 picture with letterbox bars): crop to the 16:9 middle first.
 */
internal fun drawRoundedBitmap(c: Canvas, bmp: Bitmap?, r: RectF, radius: Float, crop169: Boolean = false) {
    if (bmp == null) {
        fillP.color = C.surfaceHigh
        c.drawRoundRect(r, radius, radius, fillP)
        return
    }
    val targetAspect = r.width() / r.height()
    val sw = bmp.width.toFloat()
    var sh = bmp.height.toFloat()
    if (crop169) sh = min(sh, sw * 9f / 16f)
    val topOffset = (bmp.height - sh) / 2f
    val srcAspect = sw / sh
    val cropW: Float
    val cropH: Float
    if (srcAspect > targetAspect) { cropH = sh; cropW = sh * targetAspect } else { cropW = sw; cropH = sw / targetAspect }
    val left = (sw - cropW) / 2f
    val top = topOffset + (sh - cropH) / 2f
    val shader = BitmapShader(bmp, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
    val scale = r.width() / cropW
    shaderMatrix.reset()
    shaderMatrix.postTranslate(-left, -top)
    shaderMatrix.postScale(scale, scale)
    shaderMatrix.postTranslate(r.left, r.top)
    shader.setLocalMatrix(shaderMatrix)
    shaderPaint.shader = shader
    c.drawRoundRect(r, radius, radius, shaderPaint)
    shaderPaint.shader = null
}

/** A tinted accent derived from a cover image (average of its saturated pixels), toned for the current theme. */
internal fun accentFrom(bmp: Bitmap): Int {
    val s = Bitmap.createScaledBitmap(bmp, 12, 12, true)
    var r = 0f; var g = 0f; var b = 0f; var wsum = 0f
    val hsv = FloatArray(3)
    for (y in 0 until 12) for (x in 0 until 12) {
        val px = s.getPixel(x, y)
        Color.colorToHSV(px, hsv)
        val w = 0.05f + hsv[1] * hsv[2] * hsv[2]
        r += Color.red(px) * w; g += Color.green(px) * w; b += Color.blue(px) * w; wsum += w
    }
    if (s !== bmp) s.recycle()
    val avg = Color.rgb((r / wsum).toInt(), (g / wsum).toInt(), (b / wsum).toInt())
    Color.colorToHSV(avg, hsv)
    hsv[1] = hsv[1].coerceIn(0.30f, 0.80f)
    hsv[2] = if (C.isLight) 0.97f else 0.62f
    return Color.HSVToColor(hsv)
}

/** A round touch target with a vector icon. */
class IconBtn(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var icon = "plus"; set(v) { field = v; invalidate() }
    var tint = 0; set(v) { field = v; invalidate() }
    var filled = false
    private val bg = paint()
    private val sz = Ui.dp(44)
    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(sz.toInt(), sz.toInt())
    override fun onDraw(c: Canvas) {
        val cx = width / 2f; val cy = height / 2f
        if (isPressed) { bg.color = C.surfaceHigh; c.drawCircle(cx, cy, width / 2f, bg) }
        Icons.draw(c, icon, cx, cy, width * 0.5f, if (tint != 0) tint else C.onSurface, filled)
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { isPressed = true; return true }
            MotionEvent.ACTION_UP -> { val inside = e.x in 0f..width.toFloat() && e.y in 0f..height.toFloat(); isPressed = false; if (inside) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); performClick() } }
            MotionEvent.ACTION_CANCEL -> isPressed = false
        }
        return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }
}

/** A chip: filter / action button used for the context row and recent searches. */
class PillView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var label = ""; set(v) { field = v; requestLayout(); invalidate() }
    var icon = ""; set(v) { field = v; requestLayout(); invalidate() }
    var selectedPill = false; set(v) { field = v; invalidate() }
    private val p = paint()
    private val tp = paint().apply { typeface = Ui.medium; textSize = Ui.dp(13) }
    private val rect = RectF()
    override fun onMeasure(w: Int, h: Int) {
        val iconW = if (icon.isEmpty()) 0f else Ui.dp(22)
        setMeasuredDimension((tp.measureText(label) + iconW + Ui.dp(28)).toInt(), Ui.dp(40).toInt())
    }
    override fun onDraw(c: Canvas) {
        rect.set(Ui.dp(1), Ui.dp(4), width - Ui.dp(1), height - Ui.dp(4))
        val r = Ui.dp(10)
        if (selectedPill) { p.style = Paint.Style.FILL; p.color = C.secondaryContainer; c.drawRoundRect(rect, r, r, p) }
        else {
            if (isPressed) { p.style = Paint.Style.FILL; p.color = C.surfaceHigh; c.drawRoundRect(rect, r, r, p) }
            p.style = Paint.Style.STROKE; p.strokeWidth = Ui.dp(1); p.color = C.outline; c.drawRoundRect(rect, r, r, p)
        }
        val fg = if (selectedPill) C.onSecondaryContainer else C.onSurface
        var x = Ui.dp(14)
        if (icon.isNotEmpty()) { Icons.draw(c, icon, x + Ui.dp(7), height / 2f, Ui.dp(16), fg); x += Ui.dp(22) }
        tp.color = fg
        c.drawText(label, x, height / 2f - (tp.ascent() + tp.descent()) / 2f, tp)
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { isPressed = true; invalidate(); return true }
            MotionEvent.ACTION_UP -> { val inside = e.x in 0f..width.toFloat() && e.y in 0f..height.toFloat(); isPressed = false; invalidate(); if (inside) performClick() }
            MotionEvent.ACTION_CANCEL -> { isPressed = false; invalidate() }
        }
        return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }
}

/** Wraps children onto several lines (recent-search chips). */
class FlowLayout(ctx: Context) : ViewGroup(ctx) {
    private val gap = Ui.dpi(8)
    override fun onMeasure(wSpec: Int, hSpec: Int) {
        val maxW = MeasureSpec.getSize(wSpec)
        var x = 0; var y = 0; var rowH = 0
        for (i in 0 until childCount) {
            val ch = getChildAt(i)
            ch.measure(MeasureSpec.UNSPECIFIED, MeasureSpec.UNSPECIFIED)
            if (x > 0 && x + ch.measuredWidth > maxW) { x = 0; y += rowH + gap; rowH = 0 }
            x += ch.measuredWidth + gap
            rowH = max(rowH, ch.measuredHeight)
        }
        setMeasuredDimension(maxW, y + rowH)
    }
    override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
        val maxW = r - l
        var x = 0; var y = 0; var rowH = 0
        for (i in 0 until childCount) {
            val ch = getChildAt(i)
            if (x > 0 && x + ch.measuredWidth > maxW) { x = 0; y += rowH + gap; rowH = 0 }
            ch.layout(x, y, x + ch.measuredWidth, y + ch.measuredHeight)
            x += ch.measuredWidth + gap
            rowH = max(rowH, ch.measuredHeight)
        }
    }
}

/** Bottom navigation: icon + label per destination, with a rounded indicator behind the selected icon. */
class BottomNavView(ctx: Context, private val items: List<Pair<String, String>>) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var selected = 0; set(v) { field = v; invalidate() }
    var onSelect: ((Int) -> Unit)? = null
    var badges: IntArray = IntArray(items.size)
    private val p = paint()
    private val tp = paint().apply { textSize = Ui.dp(11); textAlign = Paint.Align.CENTER }
    private val rect = RectF()
    private var down = -1

    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(MeasureSpec.getSize(w), Ui.dp(68).toInt())

    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val h = height.toFloat()
        p.style = Paint.Style.FILL; p.color = C.surfaceVariant; c.drawRect(0f, 0f, w, h, p)
        p.color = C.outline; c.drawRect(0f, 0f, w, Ui.dp(1) * 0.6f, p)
        val cell = w / items.size
        for (i in items.indices) {
            val cx = cell * i + cell / 2f
            val sel = i == selected
            val iy = Ui.dp(26)
            if (sel || down == i) {
                rect.set(cx - Ui.dp(32), iy - Ui.dp(16), cx + Ui.dp(32), iy + Ui.dp(16))
                p.color = if (sel) C.secondaryContainer else C.surfaceHigh
                c.drawRoundRect(rect, Ui.dp(16), Ui.dp(16), p)
            }
            Icons.draw(c, items[i].first, cx, iy, Ui.dp(22), if (sel) C.onSecondaryContainer else C.onSurfaceVariant, sel && items[i].first == "heart")
            if (badges.getOrElse(i) { 0 } > 0) {
                p.color = C.primary; c.drawCircle(cx + Ui.dp(14), iy - Ui.dp(10), Ui.dp(4), p)
            }
            tp.color = if (sel) C.onSurface else C.onSurfaceVariant
            tp.typeface = if (sel) Ui.medium else Typeface_NORMAL
            c.drawText(items[i].second, cx, Ui.dp(55), tp)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val cell = width.toFloat() / items.size
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { down = (e.x / cell).toInt().coerceIn(0, items.size - 1); invalidate(); return true }
            MotionEvent.ACTION_UP -> {
                val i = (e.x / cell).toInt().coerceIn(0, items.size - 1)
                val was = down; down = -1; invalidate()
                if (i == was) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); onSelect?.invoke(i) }
            }
            MotionEvent.ACTION_CANCEL -> { down = -1; invalidate() }
        }
        return true
    }

    private companion object { val Typeface_NORMAL: android.graphics.Typeface = android.graphics.Typeface.DEFAULT }
}

/**
 * One list row (track or playlist card), drawn directly on a canvas: a single View per row keeps scrolling
 * smooth on slow tablets. Buttons are named; "heart" reads live state. Download state is a badge on the cover.
 */
class RowView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var title = ""
    var sub = ""
    var vid: String? = null          // track rows: liked / download / active state
    var thumbKey = ""
    var thumbUrl = ""
    var badge = ""                   // e.g. "48" on playlist covers
    var icons: Array<String> = emptyArray()
    var activeProvider: (() -> Boolean)? = null
    var onRow: (() -> Unit)? = null
    var onLongRow: (() -> Unit)? = null
    var onButton: ((Int, String) -> Unit)? = null
    /** Multi-select mode: the buttons give way to a round checkbox. */
    var selecting = false
    var checked = false

    private val tp = paint().apply { typeface = Ui.medium; textSize = Ui.dp(15) }
    private val sp = paint().apply { textSize = Ui.dp(12.5f) }
    private val bp = paint()
    private val rect = RectF()
    private val thumbRect = RectF()
    private val btn = Ui.dp(44)
    private var downTarget = -2   // -2 none, -1 row, >=0 button index
    private var downX = 0f
    private var downY = 0f
    private var longFired = false
    private val longPress = Runnable { if (downTarget == -1) { longFired = true; performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS); onLongRow?.invoke() } }

    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(MeasureSpec.getSize(w), Ui.dp(72).toInt())

    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val h = height.toFloat()
        val active = activeProvider?.invoke() == true
        if (active || downTarget == -1) {
            bp.style = Paint.Style.FILL
            bp.color = if (active) C.surfaceVariant else C.surfaceHigh
            rect.set(Ui.dp(4), Ui.dp(2), w - Ui.dp(4), h - Ui.dp(2))
            c.drawRoundRect(rect, Ui.dp(16), Ui.dp(16), bp)
        }
        val tw = if (Ui.compact) Ui.dp(84) else Ui.dp(96)
        val th = tw * 9f / 16f
        thumbRect.set(Ui.dp(12), (h - th) / 2, Ui.dp(12) + tw, (h + th) / 2)
        val bmp = if (thumbKey.isEmpty()) null else Thumbs.get(thumbKey, thumbUrl, tw.toInt())
        drawRoundedBitmap(c, bmp, thumbRect, Ui.dp(10))
        if (active) {
            bp.color = C.alpha(C.surface, 170); c.drawRoundRect(thumbRect, Ui.dp(10), Ui.dp(10), bp)
            Icons.draw(c, if (Playback.isPlaying) "music" else "play", thumbRect.centerX(), thumbRect.centerY(), Ui.dp(22), C.primary)
        }
        if (badge.isNotEmpty()) {
            sp.color = C.onSurface
            val bw = sp.measureText(badge) + Ui.dp(24)
            bp.color = C.alpha(C.surface, 215)
            rect.set(thumbRect.right - bw - Ui.dp(4), thumbRect.bottom - Ui.dp(22), thumbRect.right - Ui.dp(4), thumbRect.bottom - Ui.dp(4))
            c.drawRoundRect(rect, Ui.dp(9), Ui.dp(9), bp)
            Icons.draw(c, "list", rect.left + Ui.dp(10), rect.centerY(), Ui.dp(11), C.onSurface)
            c.drawText(badge, rect.left + Ui.dp(17), rect.centerY() - (sp.ascent() + sp.descent()) / 2, sp)
        }
        // download state badge on the cover (bottom-left)
        vid?.let { v ->
            val dp = Downloads.progress(v)
            if (Downloads.isDone(v) || dp != null) {
                val cx = thumbRect.left + Ui.dp(13); val cy = thumbRect.bottom - Ui.dp(13)
                bp.style = Paint.Style.FILL; bp.color = C.alpha(C.surface, 225); c.drawCircle(cx, cy, Ui.dp(9.5f), bp)
                if (Downloads.isDone(v)) Icons.draw(c, "check", cx, cy, Ui.dp(12), C.tertiary)
                else {
                    bp.style = Paint.Style.STROKE; bp.strokeWidth = Ui.dp(2); bp.color = C.primary
                    rect.set(cx - Ui.dp(6), cy - Ui.dp(6), cx + Ui.dp(6), cy + Ui.dp(6))
                    c.drawArc(rect, -90f, if (dp != null && dp >= 0) dp * 3.6f else 70f, false, bp)
                    bp.style = Paint.Style.FILL
                }
            }
        }

        val textX = thumbRect.right + Ui.dp(12)
        val shownButtons = if (selecting) 1 else icons.size
        val btnsLeft = w - shownButtons * btn - Ui.dp(6)
        val avail = max(Ui.dp(40), btnsLeft - textX - Ui.dp(2))
        tp.color = if (active) C.primary else C.onSurface
        c.drawText(TextUtils.ellipsize(title, tp, avail, TextUtils.TruncateAt.END).toString(), textX, h / 2f - Ui.dp(3), tp)
        sp.color = C.onSurfaceVariant
        c.drawText(TextUtils.ellipsize(sub, sp, avail, TextUtils.TruncateAt.END).toString(), textX, h / 2f + Ui.dp(17), sp)

        if (selecting) {                       // round checkbox
            val cx = w - Ui.dp(6) - btn / 2f; val cy = h / 2f
            bp.style = Paint.Style.FILL
            if (checked) { bp.color = C.primary; c.drawCircle(cx, cy, Ui.dp(12), bp); Icons.draw(c, "check", cx, cy, Ui.dp(15), C.onPrimary) }
            else { bp.style = Paint.Style.STROKE; bp.strokeWidth = Ui.dp(2); bp.color = C.onSurfaceVariant; c.drawCircle(cx, cy, Ui.dp(11), bp); bp.style = Paint.Style.FILL }
            return
        }
        for (i in icons.indices) drawButton(c, i, btnsLeft + i * btn + btn / 2f, h / 2f)
    }

    private fun drawButton(c: Canvas, i: Int, cx: Float, cy: Float) {
        if (downTarget == i) { bp.style = Paint.Style.FILL; bp.color = C.surfaceHigh; c.drawCircle(cx, cy, btn / 2 - Ui.dp(2), bp) }
        val size = Ui.dp(22)
        when (val name = icons[i]) {
            "heart" -> { val liked = vid != null && vid in Store.likedSet; Icons.draw(c, "heart", cx, cy, size, if (liked) C.primary else C.onSurfaceVariant, liked) }
            "download" -> {
                val v = vid ?: return
                val p = Downloads.progress(v)
                when {
                    Downloads.isDone(v) -> Icons.draw(c, "check", cx, cy, size, C.tertiary)
                    p != null -> {
                        bp.style = Paint.Style.STROKE; bp.strokeWidth = Ui.dp(2.5f); bp.color = C.outline
                        val r = Ui.dp(10)
                        rect.set(cx - r, cy - r, cx + r, cy + r)
                        c.drawArc(rect, 0f, 360f, false, bp)
                        bp.color = C.primary
                        c.drawArc(rect, -90f, if (p >= 0) p * 3.6f else 70f, false, bp)
                        bp.style = Paint.Style.FILL
                        Icons.draw(c, "x", cx, cy, Ui.dp(10), C.onSurfaceVariant)
                    }
                    else -> Icons.draw(c, "download", cx, cy, size, C.onSurfaceVariant)
                }
            }
            else -> Icons.draw(c, name, cx, cy, size, C.onSurfaceVariant)
        }
    }

    private fun target(x: Float): Int {
        if (selecting) return -1               // the whole row is one big tap target
        val btnsLeft = width - icons.size * btn - Ui.dp(6)
        if (x >= btnsLeft && icons.isNotEmpty()) return ((x - btnsLeft) / btn).toInt().coerceIn(0, icons.size - 1)
        return -1
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downTarget = target(e.x); downX = e.x; downY = e.y; longFired = false; invalidate()
                if (downTarget == -1 && onLongRow != null) postDelayed(longPress, 450)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (downTarget != -2 && (abs(e.x - downX) > Ui.dp(10) || abs(e.y - downY) > Ui.dp(10))) { downTarget = -2; removeCallbacks(longPress); invalidate() }
            }
            MotionEvent.ACTION_UP -> {
                removeCallbacks(longPress)
                val t = downTarget
                downTarget = -2; invalidate()
                if (longFired) return true
                if (t == -1) onRow?.invoke() else if (t >= 0) onButton?.invoke(t, icons[t])
            }
            MotionEvent.ACTION_CANCEL -> { removeCallbacks(longPress); downTarget = -2; invalidate() }
        }
        return true
    }
}

/** Floating mini player card. Tap opens the full player; swipe left/right skips tracks. */
class MiniPlayerView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var onOpen: (() -> Unit)? = null
    private val tp = paint().apply { typeface = Ui.medium; textSize = Ui.dp(14) }
    private val sp = paint().apply { textSize = Ui.dp(12) }
    private val bp = paint()
    private val rect = RectF()
    private val card = RectF()
    private var down = -2
    private var downX = 0f
    private var downY = 0f

    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(MeasureSpec.getSize(w), Ui.dp(72).toInt())

    private fun playCx() = width - Ui.dp(12) - Ui.dp(58)
    private fun nextCx() = width - Ui.dp(12) - Ui.dp(22)

    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val h = height.toFloat()
        card.set(Ui.dp(8), Ui.dp(6), w - Ui.dp(8), h - Ui.dp(6))
        bp.style = Paint.Style.FILL; bp.color = C.surfaceHigh
        c.drawRoundRect(card, Ui.dp(18), Ui.dp(18), bp)
        val p = Playback.player
        val t = Playback.current
        // progress along the bottom edge of the card
        if (p != null && t != null && p.duration > 0) {
            val f = (p.currentPosition.toFloat() / p.duration).coerceIn(0f, 1f)
            rect.set(card.left + Ui.dp(16), card.bottom - Ui.dp(4), card.right - Ui.dp(16), card.bottom - Ui.dp(2))
            bp.color = C.alpha(C.onSurface, 40); c.drawRoundRect(rect, Ui.dp(1), Ui.dp(1), bp)
            rect.right = rect.left + (card.width() - Ui.dp(32)) * f
            bp.color = C.primary; c.drawRoundRect(rect, Ui.dp(1), Ui.dp(1), bp)
        }
        val th = Ui.dp(44)
        rect.set(card.left + Ui.dp(8), card.centerY() - th / 2 - Ui.dp(1), card.left + Ui.dp(8) + th, card.centerY() + th / 2 - Ui.dp(1))
        drawRoundedBitmap(c, t?.let { Thumbs.track(it.vid, rect.width().toInt() * 2) }, rect, Ui.dp(10))
        val tx = rect.right + Ui.dp(12)
        val avail = playCx() - Ui.dp(26) - tx
        tp.color = C.onSurface
        c.drawText(TextUtils.ellipsize(t?.title ?: "Nothing playing", tp, avail, TextUtils.TruncateAt.END).toString(), tx, card.centerY() - Ui.dp(3), tp)
        sp.color = if (Playback.isBuffering) C.tertiary else C.onSurfaceVariant
        c.drawText(TextUtils.ellipsize(if (Playback.isBuffering) "Loading…" else (t?.artist ?: "Search and tap a song"), sp, avail, TextUtils.TruncateAt.END).toString(), tx, card.centerY() + Ui.dp(14), sp)
        val cy = card.centerY() - Ui.dp(1)
        bp.color = C.primary
        c.drawCircle(playCx(), cy, Ui.dp(21), bp)
        Icons.draw(c, if (Playback.isPlaying) "pause" else "play", playCx(), cy, Ui.dp(19), C.onPrimary)
        if (down == 1) { bp.color = C.surfaceVariant; c.drawCircle(nextCx(), cy, Ui.dp(20), bp) }
        Icons.draw(c, "next", nextCx(), cy, Ui.dp(22), C.onSurface)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { down = hit(e.x); downX = e.x; downY = e.y; invalidate(); return true }
            MotionEvent.ACTION_UP -> {
                val t = down; down = -2; invalidate()
                val dx = e.x - downX; val dy = e.y - downY
                if (t == -1 && abs(dx) > Ui.dp(64) && abs(dx) > abs(dy) * 1.5f) { if (dx < 0) Playback.next() else Playback.prev(); return true }
                if (abs(dx) < Ui.dp(16) && abs(dy) < Ui.dp(16) && hit(e.x) == t) when (t) { 0 -> Playback.toggle(); 1 -> Playback.next(); -1 -> onOpen?.invoke() }
            }
            MotionEvent.ACTION_CANCEL -> { down = -2; invalidate() }
        }
        return true
    }

    private fun hit(x: Float) = when {
        abs(x - playCx()) < Ui.dp(26) -> 0
        abs(x - nextCx()) < Ui.dp(24) -> 1
        else -> -1
    }
}

/** Full-screen player: cover-tinted background, big cover, seek, transport and the secondary actions. */
class NowPlayingView(ctx: Context) : View(ctx) {
    var onClose: (() -> Unit)? = null
    var onSleep: (() -> Unit)? = null
    var onQueue: (() -> Unit)? = null
    var onDownload: (() -> Unit)? = null
    var onLike: (() -> Unit)? = null

    private enum class Id { CLOSE, QUEUE, HEART, SHUFFLE, PREV, REPLAY, PLAY, FORWARD, NEXT, REPEAT, SPEED, SLEEP, DOWNLOAD, SEEK }
    private class Hit(val id: Id, val r: RectF)

    private val hits = ArrayList<Hit>()
    private val art = RectF()
    private val seek = RectF()
    private val bp = paint()
    private val tp = paint().apply { typeface = Ui.bold; textSize = Ui.dp(22) }
    private val ap = paint().apply { textSize = Ui.dp(15) }
    private val sp = paint().apply { textSize = Ui.dp(12) }
    private val rect = RectF()
    private var downId: Id? = null
    private var dragging = false
    private var dragFrac = 0f
    private var accent = 0
    private var accentVid = ""
    private var shader: LinearGradient? = null
    private var shaderKey = 0L
    private var panelDragging = false
    private var downRawX = 0f
    private var downRawY = 0f
    private var tracker: VelocityTracker? = null
    private val slop = ViewConfiguration.get(context).scaledTouchSlop

    init { isClickable = true; isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }

    private fun wrap2(text: String, p: TextPaint, width: Float): List<String> {
        if (p.measureText(text) <= width) return listOf(text)
        val n = p.breakText(text, true, width, null)
        var cut = text.lastIndexOf(' ', n - 1)
        if (cut < n / 3) cut = n
        return listOf(text.substring(0, cut).trim(), TextUtils.ellipsize(text.substring(cut).trim(), p, width, TextUtils.TruncateAt.END).toString())
    }

    private fun layout() {
        hits.clear()
        val w = width.toFloat(); val h = height.toFloat()
        val pad = Ui.dp(24)
        val land = w > h * 1.15f
        val colL = if (land) w * 0.5f else 0f
        val colW = if (land) w * 0.5f else w
        val cx = colL + colW / 2f
        fun btn(id: Id, x: Float, y: Float, size: Float) = hits.add(Hit(id, RectF(x - size / 2, y - size / 2, x + size / 2, y + size / 2)))

        btn(Id.CLOSE, pad, Ui.dp(36), Ui.dp(48))
        btn(Id.QUEUE, w - pad, Ui.dp(36), Ui.dp(48))

        // bottom-up stack
        val rowB = h - Ui.dp(46)
        val rowA = rowB - Ui.dp(86)
        val seekY = rowA - Ui.dp(76)
        val gap = min(colW - 2 * pad, Ui.dp(340)) / 5f
        val bx = cx - 2 * gap
        btn(Id.SHUFFLE, bx, rowB, Ui.dp(48)); btn(Id.REPEAT, bx + gap, rowB, Ui.dp(48)); btn(Id.SPEED, bx + 2 * gap, rowB, Ui.dp(60))
        btn(Id.SLEEP, bx + 3 * gap, rowB, Ui.dp(48)); btn(Id.DOWNLOAD, bx + 4 * gap, rowB, Ui.dp(48))
        val ag = min(colW - 2 * pad, Ui.dp(330)) / 5f
        val ax = cx - 2 * ag
        btn(Id.PREV, ax, rowA, Ui.dp(52)); btn(Id.REPLAY, ax + ag, rowA, Ui.dp(52)); btn(Id.PLAY, ax + 2 * ag, rowA, Ui.dp(72))
        btn(Id.FORWARD, ax + 3 * ag, rowA, Ui.dp(52)); btn(Id.NEXT, ax + 4 * ag, rowA, Ui.dp(52))
        seek.set(colL + pad, seekY - Ui.dp(14), colL + colW - pad, seekY + Ui.dp(14))
        hits.add(Hit(Id.SEEK, RectF(seek).apply { inset(0f, -Ui.dp(12)) }))
        // heart sits with the title
        btn(Id.HEART, colL + colW - pad - Ui.dp(8), seekY - Ui.dp(84), Ui.dp(48))

        if (land) {
            val side = min(w / 2f - 2 * pad, h - Ui.dp(120))
            art.set(pad, (h - side) / 2f, pad + side, (h + side) / 2f)
        } else {
            val titleTop = seekY - Ui.dp(150)
            val availH = titleTop - Ui.dp(76) - Ui.dp(8)
            val side = min(w - 2 * Ui.dp(32), max(Ui.dp(120), availH))
            art.set((w - side) / 2f, Ui.dp(76), (w + side) / 2f, Ui.dp(76) + side)
        }
    }

    override fun onDraw(c: Canvas) {
        layout()
        val w = width.toFloat(); val h = height.toFloat()
        val pl = Playback.player
        val t = Playback.current
        val land = w > h * 1.15f
        val colL = if (land) w * 0.5f else 0f
        val colW = if (land) w * 0.5f else w
        val pad = Ui.dp(24)

        val bigBmp = t?.let { Thumbs.trackBig(it.vid, art.width().toInt()) }
        if (t != null && bigBmp != null && accentVid != t.vid) { accent = accentFrom(bigBmp); accentVid = t.vid }
        if (t == null) accent = 0
        val tint = if (accent != 0) accent else C.primary
        val key = (h.toLong() shl 32) xor tint.toLong() xor C.surface.toLong()
        if (shader == null || shaderKey != key) {
            shader = LinearGradient(0f, 0f, 0f, h * 0.85f, C.mix(C.surface, tint, if (C.isLight) 0.55f else 0.5f), C.surface, Shader.TileMode.CLAMP)
            shaderKey = key
        }
        bp.style = Paint.Style.FILL; bp.color = C.surface; c.drawRect(0f, 0f, w, h, bp)
        bp.shader = shader; c.drawRect(0f, 0f, w, h, bp); bp.shader = null

        for (hit in hits) draw(c, hit, pl, tint)

        sp.color = C.onSurfaceVariant; sp.textAlign = Paint.Align.CENTER
        c.drawText("NOW PLAYING", w / 2f, Ui.dp(40), sp)
        sp.textAlign = Paint.Align.LEFT

        // cover with a soft shadow plate
        bp.color = C.alpha(Color.BLACK, if (C.isLight) 30 else 70)
        rect.set(art.left + Ui.dp(6), art.top + Ui.dp(12), art.right - Ui.dp(6), art.bottom + Ui.dp(8))
        c.drawRoundRect(rect, Ui.dp(26), Ui.dp(26), bp)
        drawRoundedBitmap(c, bigBmp, art, Ui.dp(24), crop169 = true)
        if (t == null) Icons.draw(c, "music", art.centerX(), art.centerY(), Ui.dp(56), C.outline)

        val maxW = colW - 2 * pad - Ui.dp(56)
        val lines = wrap2(t?.title ?: "Nothing playing", tp, maxW)
        val seekY = seek.centerY()
        val artistY = seekY - Ui.dp(62)
        val lh = Ui.dp(27)
        val titleTop = artistY - Ui.dp(26) - (lines.size - 1) * lh
        tp.color = C.onSurface
        lines.forEachIndexed { i, s -> c.drawText(s, colL + pad, titleTop + i * lh, tp) }
        ap.color = C.secondary
        c.drawText(TextUtils.ellipsize(t?.artist ?: "", ap, maxW, TextUtils.TruncateAt.END).toString(), colL + pad, artistY, ap)

        // seek bar
        val dur = pl?.duration?.takeIf { it > 0 } ?: 0L
        val pos = if (dragging) (dragFrac * dur).toLong() else (pl?.currentPosition ?: 0L)
        val frac = if (dur > 0) (pos.toFloat() / dur).coerceIn(0f, 1f) else 0f
        val ty = seek.centerY()
        bp.color = C.alpha(C.onSurface, 45); c.drawRoundRect(seek.left, ty - Ui.dp(2), seek.right, ty + Ui.dp(2), Ui.dp(2), Ui.dp(2), bp)
        bp.color = C.primary; c.drawRoundRect(seek.left, ty - Ui.dp(2), seek.left + seek.width() * frac, ty + Ui.dp(2), Ui.dp(2), Ui.dp(2), bp)
        c.drawCircle(seek.left + seek.width() * frac, ty, if (dragging) Ui.dp(10) else Ui.dp(7), bp)
        sp.color = C.onSurfaceVariant
        c.drawText(Ui.fmtTime(pos / 1000), seek.left, ty + Ui.dp(26), sp)
        sp.textAlign = Paint.Align.RIGHT
        c.drawText(if (Playback.isBuffering) "Loading…" else Ui.fmtTime(dur / 1000), seek.right, ty + Ui.dp(26), sp)
        sp.textAlign = Paint.Align.LEFT
    }

    private fun draw(c: Canvas, hit: Hit, pl: Player?, tint: Int) {
        val r = hit.r
        val cx = r.centerX(); val cy = r.centerY()
        val pressed = downId == hit.id && hit.id != Id.SEEK
        val t = Playback.current
        if (hit.id == Id.SEEK) return
        if (hit.id == Id.PLAY) {
            bp.style = Paint.Style.FILL; bp.color = C.primary
            rect.set(r); c.drawRoundRect(rect, r.width() * (if (pressed) 0.30f else 0.36f), r.width() * (if (pressed) 0.30f else 0.36f), bp)
            Icons.draw(c, if (Playback.isPlaying) "pause" else "play", cx, cy, r.width() * 0.42f, C.onPrimary)
            return
        }
        if (pressed) { bp.style = Paint.Style.FILL; bp.color = C.alpha(C.onSurface, 30); c.drawCircle(cx, cy, r.width() / 2f, bp) }
        val s = r.width() * 0.46f
        val on = C.onSurface
        when (hit.id) {
            Id.CLOSE -> Icons.draw(c, "chevronDown", cx, cy, Ui.dp(28), on)
            Id.QUEUE -> Icons.draw(c, "list", cx, cy, Ui.dp(24), on)
            Id.SLEEP -> {
                val active = Playback.sleepEndsAt > 0
                Icons.draw(c, "moon", cx, cy - (if (active) Ui.dp(4) else 0f), Ui.dp(22), if (active) C.tertiary else C.onSurfaceVariant)
                if (active) {
                    val m = max(1L, (Playback.sleepEndsAt - System.currentTimeMillis() + 59_999) / 60_000)
                    sp.color = C.tertiary; sp.textAlign = Paint.Align.CENTER; sp.textSize = Ui.dp(10)
                    c.drawText("${m}m", cx, cy + Ui.dp(16), sp); sp.textSize = Ui.dp(12); sp.textAlign = Paint.Align.LEFT
                }
            }
            Id.SHUFFLE -> Icons.draw(c, "shuffle", cx, cy, Ui.dp(22), if (pl?.shuffleModeEnabled == true) C.primary else C.onSurfaceVariant)
            Id.PREV -> Icons.draw(c, "prev", cx, cy, Ui.dp(26), on)
            Id.NEXT -> Icons.draw(c, "next", cx, cy, Ui.dp(26), on)
            Id.REPLAY, Id.FORWARD -> {
                Icons.draw(c, if (hit.id == Id.REPLAY) "replay" else "forward", cx, cy, Ui.dp(32), on)
                sp.color = on; sp.textAlign = Paint.Align.CENTER; sp.textSize = Ui.dp(9); sp.typeface = Ui.bold
                c.drawText(if (hit.id == Id.REPLAY) "10" else "30", cx, cy + Ui.dp(3.5f), sp)
                sp.textSize = Ui.dp(12); sp.textAlign = Paint.Align.LEFT; sp.typeface = android.graphics.Typeface.DEFAULT
            }
            Id.REPEAT -> {
                val m = pl?.repeatMode ?: Player.REPEAT_MODE_OFF
                Icons.draw(c, "repeat", cx, cy, Ui.dp(22), if (m != Player.REPEAT_MODE_OFF) C.primary else C.onSurfaceVariant)
                if (m == Player.REPEAT_MODE_ONE) { sp.color = C.primary; sp.textAlign = Paint.Align.CENTER; c.drawText("1", cx + Ui.dp(11), cy + Ui.dp(15), sp); sp.textAlign = Paint.Align.LEFT }
            }
            Id.SPEED -> {
                val sv = Playback.speed
                val label = (if (sv == sv.toInt().toFloat()) "%.1f" else "%.2f").format(sv).trimEnd('0').trimEnd('.').let { if (it.contains('.')) it else "$it.0" } + "×"
                rect.set(cx - Ui.dp(26), cy - Ui.dp(15), cx + Ui.dp(26), cy + Ui.dp(15))
                if (sv != 1f) { bp.style = Paint.Style.FILL; bp.color = C.secondaryContainer; c.drawRoundRect(rect, Ui.dp(10), Ui.dp(10), bp) }
                else { bp.style = Paint.Style.STROKE; bp.strokeWidth = Ui.dp(1); bp.color = C.outline; c.drawRoundRect(rect, Ui.dp(10), Ui.dp(10), bp); bp.style = Paint.Style.FILL }
                sp.color = if (sv != 1f) C.onSecondaryContainer else C.onSurfaceVariant; sp.typeface = Ui.medium; sp.textAlign = Paint.Align.CENTER
                c.drawText(label, cx, cy - (sp.ascent() + sp.descent()) / 2, sp); sp.textAlign = Paint.Align.LEFT; sp.typeface = android.graphics.Typeface.DEFAULT
            }
            Id.HEART -> { val l = t != null && t.vid in Store.likedSet; Icons.draw(c, "heart", cx, cy, Ui.dp(26), if (l) tint.let { C.primary } else C.onSurfaceVariant, l) }
            Id.DOWNLOAD -> {
                val v = t?.vid
                val p = v?.let { Downloads.progress(it) }
                when {
                    v != null && Downloads.isDone(v) -> Icons.draw(c, "check", cx, cy, Ui.dp(22), C.tertiary)
                    p != null -> {
                        bp.style = Paint.Style.STROKE; bp.strokeWidth = Ui.dp(2.5f); bp.color = C.outline
                        val rr = Ui.dp(11); rect.set(cx - rr, cy - rr, cx + rr, cy + rr)
                        c.drawArc(rect, 0f, 360f, false, bp); bp.color = C.primary
                        c.drawArc(rect, -90f, if (p >= 0) p * 3.6f else 70f, false, bp)
                        bp.style = Paint.Style.FILL
                    }
                    else -> Icons.draw(c, "download", cx, cy, Ui.dp(22), C.onSurfaceVariant)
                }
            }
            else -> {}
        }
    }

    private fun find(x: Float, y: Float): Hit? = hits.lastOrNull { it.r.contains(x, y) }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val pl = Playback.player
        if (e.actionMasked == MotionEvent.ACTION_DOWN) {
            tracker?.recycle(); tracker = VelocityTracker.obtain()
            downRawX = e.rawX; downRawY = e.rawY; panelDragging = false
        }
        tracker?.addMovement(e)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val h = find(e.x, e.y)
                downId = h?.id
                if (h?.id == Id.SEEK && (pl?.duration ?: 0L) > 0) { dragging = true; dragFrac = ((e.x - seek.left) / seek.width()).coerceIn(0f, 1f) }
                invalidate(); return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (dragging) { dragFrac = ((e.x - seek.left) / seek.width()).coerceIn(0f, 1f); invalidate() }
                else {
                    val dy = e.rawY - downRawY
                    val dx = e.rawX - downRawX
                    if (!panelDragging && dy > slop * 2 && dy > abs(dx) * 1.2f) {
                        panelDragging = true
                        downId = null
                        parent?.requestDisallowInterceptTouchEvent(true)
                    }
                    if (panelDragging) { translationY = max(0f, dy); invalidate() }
                }
            }
            MotionEvent.ACTION_UP -> {
                val id = downId
                downId = null
                if (panelDragging) {
                    panelDragging = false
                    tracker?.computeCurrentVelocity(1000)
                    val vy = tracker?.yVelocity ?: 0f
                    if (translationY > height * 0.22f || vy > 1400f) onClose?.invoke()
                    else animate().translationY(0f).setDuration(160).start()
                } else if (dragging) {
                    dragging = false
                    val d = pl?.duration ?: 0L
                    if (d > 0) Playback.seekTo((dragFrac * d).toLong())
                } else if (id != null && find(e.x, e.y)?.id == id) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); act(id) }
                tracker?.recycle(); tracker = null
                invalidate()
            }
            MotionEvent.ACTION_CANCEL -> {
                downId = null; dragging = false
                if (panelDragging) { panelDragging = false; animate().translationY(0f).setDuration(160).start() }
                tracker?.recycle(); tracker = null
                invalidate()
            }
        }
        return true
    }

    private fun act(id: Id) {
        when (id) {
            Id.CLOSE -> onClose?.invoke()
            Id.QUEUE -> onQueue?.invoke()
            Id.SLEEP -> onSleep?.invoke()
            Id.SHUFFLE -> Playback.toggleShuffle()
            Id.PREV -> Playback.prev()
            Id.PLAY -> Playback.toggle()
            Id.NEXT -> Playback.next()
            Id.REPLAY -> Playback.seekBy(-10_000)
            Id.FORWARD -> Playback.seekBy(30_000)
            Id.REPEAT -> Playback.cycleRepeat()
            Id.SPEED -> Playback.cycleSpeed()
            Id.HEART -> onLike?.invoke()
            Id.DOWNLOAD -> onDownload?.invoke()
            Id.SEEK -> {}
        }
    }
}

/** One search suggestion: magnifier + text; tap to search it. */
class SuggestionView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var text = ""; set(v) { field = v; invalidate() }
    var onPick: (() -> Unit)? = null
    private val tp = paint().apply { textSize = Ui.dp(16) }
    private val bp = paint()
    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(MeasureSpec.getSize(w), Ui.dp(52).toInt())
    override fun onDraw(c: Canvas) {
        if (isPressed) { bp.color = C.surfaceHigh; c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bp) }
        Icons.draw(c, "search", Ui.dp(32), height / 2f, Ui.dp(20), C.onSurfaceVariant)
        tp.color = C.onSurface
        c.drawText(TextUtils.ellipsize(text, tp, width - Ui.dp(76), TextUtils.TruncateAt.END).toString(), Ui.dp(60), height / 2f - (tp.ascent() + tp.descent()) / 2f, tp)
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { isPressed = true; invalidate(); return true }
            MotionEvent.ACTION_UP -> { val inside = e.y in 0f..height.toFloat(); isPressed = false; invalidate(); if (inside) onPick?.invoke() }
            MotionEvent.ACTION_CANCEL -> { isPressed = false; invalidate() }
        }
        return true
    }
}

/** Shelf title with optional play / shuffle buttons. */
class SectionHeaderView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var title = ""; set(v) { field = v; invalidate() }
    var subtitle = ""; set(v) { field = v; invalidate() }
    var showButtons = true
    var onPlay: (() -> Unit)? = null
    var onShuffle: (() -> Unit)? = null
    private val tp = paint().apply { typeface = Ui.bold; textSize = Ui.dp(19) }
    private val sp = paint().apply { textSize = Ui.dp(12.5f) }
    private val bp = paint()
    private var down = -1
    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(MeasureSpec.getSize(w), Ui.dp(64).toInt())
    private fun playCx() = width - Ui.dp(16) - Ui.dp(40) - Ui.dp(8) - Ui.dp(20)
    private fun shufCx() = width - Ui.dp(16) - Ui.dp(20)
    override fun onDraw(c: Canvas) {
        val avail = (if (showButtons) playCx() - Ui.dp(28) else width - Ui.dp(32)) - Ui.dp(16)
        tp.color = C.onSurface
        c.drawText(TextUtils.ellipsize(title, tp, avail, TextUtils.TruncateAt.END).toString(), Ui.dp(16), Ui.dp(30), tp)
        sp.color = C.onSurfaceVariant
        c.drawText(TextUtils.ellipsize(subtitle, sp, avail, TextUtils.TruncateAt.END).toString(), Ui.dp(16), Ui.dp(49), sp)
        if (!showButtons) return
        val cy = Ui.dp(32)
        for ((i, cx) in listOf(playCx(), shufCx()).withIndex()) {
            bp.style = Paint.Style.FILL; bp.color = if (down == i) C.surfaceHigh else C.secondaryContainer
            c.drawCircle(cx, cy, Ui.dp(20), bp)
            Icons.draw(c, if (i == 0) "play" else "shuffle", cx, cy, Ui.dp(18), C.onSecondaryContainer)
        }
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        fun hit() = when { !showButtons -> -1; abs(e.x - playCx()) < Ui.dp(26) -> 0; abs(e.x - shufCx()) < Ui.dp(26) -> 1; else -> -1 }
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { down = hit(); invalidate(); return down >= 0 }
            MotionEvent.ACTION_UP -> { val t = down; down = -1; invalidate(); if (t >= 0 && hit() == t) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); if (t == 0) onPlay?.invoke() else onShuffle?.invoke() } }
            MotionEvent.ACTION_CANCEL -> { down = -1; invalidate() }
        }
        return true
    }
}

/** Top of the "For you" page: big title and a refresh button. */
class HeroView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var building = false; set(v) { field = v; invalidate() }
    var onRefresh: (() -> Unit)? = null
    private val tp = paint().apply { typeface = Ui.bold; textSize = Ui.dp(30) }
    private val sp = paint().apply { textSize = Ui.dp(13) }
    private val bp = paint()
    private var down = false
    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension(MeasureSpec.getSize(w), Ui.dp(78).toInt())
    private fun cx() = width - Ui.dp(16) - Ui.dp(22)
    override fun onDraw(c: Canvas) {
        tp.color = C.onSurface
        c.drawText("For you", Ui.dp(16), Ui.dp(44), tp)
        sp.color = if (building) C.tertiary else C.onSurfaceVariant
        c.drawText(if (building) "Updating your picks…" else "Picks from what you play and like", Ui.dp(16), Ui.dp(66), sp)
        bp.style = Paint.Style.FILL; bp.color = if (down) C.surfaceHigh else C.secondaryContainer
        c.drawCircle(cx(), Ui.dp(36), Ui.dp(22), bp)
        Icons.draw(c, "refresh", cx(), Ui.dp(36), Ui.dp(20), C.onSecondaryContainer)
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { down = abs(e.x - cx()) < Ui.dp(30); invalidate(); return down }
            MotionEvent.ACTION_UP -> { val d = down; down = false; invalidate(); if (d && abs(e.x - cx()) < Ui.dp(30)) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); onRefresh?.invoke() } }
            MotionEvent.ACTION_CANCEL -> { down = false; invalidate() }
        }
        return true
    }
}

/** A cover card for the horizontal "Jump back in" strip. */
class CardView(ctx: Context) : View(ctx) {
    init { isFocusable = false; if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false }
    var track: Track? = null; set(v) { field = v; invalidate() }
    var onClick: (() -> Unit)? = null
    private val tp = paint().apply { typeface = Ui.medium; textSize = Ui.dp(13) }
    private val sp = paint().apply { textSize = Ui.dp(12) }
    private val bp = paint()
    private val rect = RectF()
    private var down = false
    private val side = Ui.dp(132)
    override fun onMeasure(w: Int, h: Int) = setMeasuredDimension((side + Ui.dp(12)).toInt(), Ui.dp(188).toInt())
    override fun onDraw(c: Canvas) {
        val t = track ?: return
        rect.set(Ui.dp(6), Ui.dp(4), Ui.dp(6) + side, Ui.dp(4) + side)
        drawRoundedBitmap(c, Thumbs.trackBig(t.vid, side.toInt()), rect, Ui.dp(16), crop169 = true)
        if (down) { bp.style = Paint.Style.FILL; bp.color = C.alpha(C.surface, 90); c.drawRoundRect(rect, Ui.dp(16), Ui.dp(16), bp) }
        if (Playback.currentVid == t.vid) {
            bp.style = Paint.Style.FILL; bp.color = C.alpha(C.surface, 150); c.drawRoundRect(rect, Ui.dp(16), Ui.dp(16), bp)
            Icons.draw(c, if (Playback.isPlaying) "music" else "play", rect.centerX(), rect.centerY(), Ui.dp(30), C.primary)
        }
        tp.color = C.onSurface; sp.color = C.onSurfaceVariant
        val w = side - Ui.dp(2)
        c.drawText(TextUtils.ellipsize(t.title, tp, w, TextUtils.TruncateAt.END).toString(), Ui.dp(8), Ui.dp(4) + side + Ui.dp(20), tp)
        c.drawText(TextUtils.ellipsize(t.artist, sp, w, TextUtils.TruncateAt.END).toString(), Ui.dp(8), Ui.dp(4) + side + Ui.dp(38), sp)
    }
    private var downX = 0f
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { down = true; downX = e.x; invalidate(); return true }
            MotionEvent.ACTION_MOVE -> if (abs(e.x - downX) > Ui.dp(10)) { down = false; invalidate() }
            MotionEvent.ACTION_UP -> { val d = down; down = false; invalidate(); if (d) onClick?.invoke() }
            MotionEvent.ACTION_CANCEL -> { down = false; invalidate() }
        }
        return true
    }
}
