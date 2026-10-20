package dev.ytmusic

import android.graphics.Color
import android.graphics.Typeface

/**
 * Colors. Three themes in a Material-3-like tonal style; switched at runtime (the activity is recreated).
 * Everything draws from these at draw time, so no view needs to know which theme is active.
 */
object C {
    var name = "midnight"
    var isLight = false
    var primary = 0; var onPrimary = 0
    var secondary = 0                 // soft accent used for artist names / links
    var tertiary = 0                  // positive / "done" accent
    var secondaryContainer = 0; var onSecondaryContainer = 0   // selected chip, nav indicator
    var surface = 0                   // app background
    var surfaceVariant = 0            // cards, search bar, mini player
    var surfaceHigh = 0               // pressed / elevated
    var onSurface = 0; var onSurfaceVariant = 0
    var outline = 0; var error = 0

    val names = listOf("midnight" to "Midnight", "amoled" to "AMOLED black", "light" to "Light")

    fun apply(theme: String) {
        name = theme
        isLight = theme == "light"
        when (theme) {
            "light" -> set(surface = "#FBF8FF", variant = "#F0EDF6", high = "#E5E1EC", outline = "#C9C5D0",
                on = "#1B1B21", onVar = "#5B5E6B", primary = "#4355B9", onPrimary = "#FFFFFF", secondary = "#5A5D72",
                tertiary = "#006B58", sc = "#DCE1FF", onSc = "#00164E", error = "#BA1A1A")
            "amoled" -> set(surface = "#000000", variant = "#0F1014", high = "#1A1C22", outline = "#2A2D36",
                on = "#E4E2E9", onVar = "#9DA0AE", primary = "#B8C4FF", onPrimary = "#16275C", secondary = "#C4C6DD",
                tertiary = "#9FD9C0", sc = "#2C3150", onSc = "#DCE1FF", error = "#FFB4AB")
            else -> set(surface = "#111318", variant = "#1B1E26", high = "#262A34", outline = "#343947",
                on = "#E4E2E9", onVar = "#9DA0AE", primary = "#B8C4FF", onPrimary = "#16275C", secondary = "#C4C6DD",
                tertiary = "#9FD9C0", sc = "#2C3150", onSc = "#DCE1FF", error = "#FFB4AB")
        }
    }

    private fun set(surface: String, variant: String, high: String, outline: String, on: String, onVar: String,
                    primary: String, onPrimary: String, secondary: String, tertiary: String, sc: String, onSc: String, error: String) {
        this.surface = Color.parseColor(surface); surfaceVariant = Color.parseColor(variant); surfaceHigh = Color.parseColor(high)
        this.outline = Color.parseColor(outline); onSurface = Color.parseColor(on); onSurfaceVariant = Color.parseColor(onVar)
        this.primary = Color.parseColor(primary); this.onPrimary = Color.parseColor(onPrimary); this.secondary = Color.parseColor(secondary)
        this.tertiary = Color.parseColor(tertiary); secondaryContainer = Color.parseColor(sc); onSecondaryContainer = Color.parseColor(onSc)
        this.error = Color.parseColor(error)
    }

    init { apply("midnight") }

    /** [color] with its alpha set to [a] (0..255). */
    fun alpha(color: Int, a: Int) = Color.argb(a, Color.red(color), Color.green(color), Color.blue(color))

    /** Linear blend of two colors, t=0 -> a, t=1 -> b. */
    fun mix(a: Int, b: Int, t: Float): Int {
        val u = 1 - t
        return Color.rgb((Color.red(a) * u + Color.red(b) * t).toInt(), (Color.green(a) * u + Color.green(b) * t).toInt(),
            (Color.blue(a) * u + Color.blue(b) * t).toInt())
    }
}

object Ui {
    var density = 1f
    /** Phones are narrow: fewer buttons per row, bigger text relative to the row. */
    var compact = false
    fun dp(v: Float) = v * density
    fun dp(v: Int) = v * density
    fun dpi(v: Int) = (v * density + 0.5f).toInt()

    val medium: Typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    val bold: Typeface = Typeface.create("sans-serif", Typeface.BOLD)

    fun fmtTime(sec: Long): String {
        val s = if (sec < 0) 0 else sec
        val h = s / 3600
        val m = (s % 3600) / 60
        val r = s % 60
        return if (h > 0) "%d:%02d:%02d".format(h, m, r) else "%d:%02d".format(m, r)
    }

    fun fmtBytes(b: Long): String = when {
        b >= 1 shl 30 -> "%.1f GB".format(b / (1 shl 30).toFloat())
        b >= 1 shl 20 -> "%.1f MB".format(b / (1 shl 20).toFloat())
        else -> "%d KB".format(b / 1024)
    }
}
