package dev.ytmusic

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import androidx.core.graphics.PathParser

/** Tabler-style 24x24 stroke icons, parsed once and drawn as vectors (no bitmaps, no icon font). */
object Icons {
    private val data = mapOf(
        "play" to "M7 4 v16 l13 -8 z",
        "pause" to "M6 5 h4 v14 h-4 z M14 5 h4 v14 h-4 z",
        "next" to "M5 5 l10 7 l-10 7 z M17 5 h2 v14 h-2 z",
        "prev" to "M19 5 l-10 7 l10 7 z M5 5 h2 v14 h-2 z",
        "search" to "M17 10 a7 7 0 1 1 -14 0 a7 7 0 1 1 14 0 M15 15 l6 6",
        "plus" to "M12 5 v14 M5 12 h14",
        "x" to "M6 6 l12 12 M18 6 l-12 12",
        "volume" to "M4 9 h4 l5 -4 v14 l-5 -4 h-4 z M16.5 8.5 a5 5 0 0 1 0 7 M19 6 a9 9 0 0 1 0 12",
        "mute" to "M4 9 h4 l5 -4 v14 l-5 -4 h-4 z M17 9 l5 6 M22 9 l-5 6",
        "shuffle" to "M3 7 h3 c5 0 5 10 10 10 h5 M18 14 l3 3 l-3 3 M3 17 h3 c2 0 3 -1.2 4 -2.5 M14 9.5 c1 -1.5 2 -2.5 4 -2.5 h3 M18 4 l3 3 l-3 3",
        "repeat" to "M4 11 v-1 a3 3 0 0 1 3 -3 h13 M17 4 l3 3 l-3 3 M20 13 v1 a3 3 0 0 1 -3 3 h-13 M7 14 l-3 3 l3 3",
        "music" to "M9 18 V5 l12 -2 v13 M9 18 a3 3 0 1 1 -6 0 a3 3 0 0 1 6 0 M21 16 a3 3 0 1 1 -6 0 a3 3 0 0 1 6 0",
        "trash" to "M4 7 h16 M10 11 v6 M14 11 v6 M5 7 l1 12 a2 2 0 0 0 2 2 h8 a2 2 0 0 0 2 -2 l1 -12 M9 7 v-3 h6 v3",
        "queueNext" to "M4 6 h10 M4 12 h10 M4 18 h6 M16 15 h6 M19 12 v6",
        "heart" to "M12 20 l-7.5 -7.5 a5 5 0 0 1 7.5 -7 a5 5 0 0 1 7.5 7 z",
        "clock" to "M21 12 a9 9 0 1 1 -18 0 a9 9 0 1 1 18 0 M12 7 v5 l3 2",
        "download" to "M12 4 v11 M7 11 l5 5 l5 -5 M5 20 h14",
        "check" to "M5 12 l5 5 l9 -10",
        "lyrics" to "M4 7 h16 M4 12 h16 M4 17 h10",
        "bookmark" to "M7 4 h10 v16 l-5 -4 l-5 4 z",
        "moon" to "M12 3 a9 9 0 1 0 9 9 a7 7 0 0 1 -9 -9 z",
        "chevronDown" to "M6 9 l6 6 l6 -6",
        "back" to "M15 5 l-7 7 l7 7",
        "replay" to "M4.05 11 a8 8 0 1 1 .5 4 M4 20 v-5 h5",
        "forward" to "M19.95 11 a8 8 0 1 0 -.5 4 M20 20 v-5 h-5",
        "share" to "M9 12 a3 3 0 1 1 -6 0 a3 3 0 1 1 6 0 M21 6 a3 3 0 1 1 -6 0 a3 3 0 1 1 6 0 M21 18 a3 3 0 1 1 -6 0 a3 3 0 1 1 6 0 M8.7 10.7 l6.6 -3.4 M8.7 13.3 l6.6 3.4",
        "radio" to "M12 13 m-2 0 a2 2 0 1 0 4 0 a2 2 0 1 0 -4 0 M8.5 9.5 a5 5 0 0 0 0 7 M15.5 9.5 a5 5 0 0 1 0 7 M5.6 6.6 a9 9 0 0 0 0 12.8 M18.4 6.6 a9 9 0 0 1 0 12.8",
        "link" to "M10 14 a3.5 3.5 0 0 0 5 0 l4 -4 a3.5 3.5 0 0 0 -5 -5 l-.5 .5 M14 10 a3.5 3.5 0 0 0 -5 0 l-4 4 a3.5 3.5 0 0 0 5 5 l.5 -.5",
        "palette" to "M12 21 a9 9 0 1 1 9 -9 c0 1.7 -1.3 3 -3 3 h-2 a2 2 0 0 0 -1 3.7 a1.7 1.7 0 0 1 -3 2.3",
        "home" to "M5 12 l-2 0 l9 -9 l9 9 l-2 0 M5 12 v7 a2 2 0 0 0 2 2 h10 a2 2 0 0 0 2 -2 v-7 M9 21 v-6 a2 2 0 0 1 2 -2 h2 a2 2 0 0 1 2 2 v6",
        "refresh" to "M20 11 a8.1 8.1 0 0 0 -15.5 -2 m-.5 -4 v4 h4 M4 13 a8.1 8.1 0 0 0 15.5 2 m.5 4 v-4 h-4",
        "list" to "M9 6 h11 M9 12 h11 M9 18 h11 M4 6 v.01 M4 12 v.01 M4 18 v.01",
    )
    private val solid = setOf("play", "pause", "next", "prev")
    private val cache = HashMap<String, Path>()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }

    fun draw(c: Canvas, name: String, cx: Float, cy: Float, size: Float, color: Int, fill: Boolean = false) {
        if (name == "more") { // three dots
            paint.style = Paint.Style.FILL
            paint.color = color
            val r = size * 0.085f
            val gap = size * 0.26f
            c.drawCircle(cx, cy - gap, r, paint); c.drawCircle(cx, cy, r, paint); c.drawCircle(cx, cy + gap, r, paint)
            return
        }
        val path = cache.getOrPut(name) { PathParser.createPathFromPathData(data[name] ?: return) }
        c.save()
        c.translate(cx - size / 2, cy - size / 2)
        val s = size / 24f
        c.scale(s, s)
        paint.color = color
        paint.strokeWidth = 2f
        paint.style = if (fill || name in solid) Paint.Style.FILL_AND_STROKE else Paint.Style.STROKE
        c.drawPath(path, paint)
        c.restore()
    }
}
