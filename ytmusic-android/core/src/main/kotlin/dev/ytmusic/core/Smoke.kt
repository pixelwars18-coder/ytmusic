package dev.ytmusic.core

import java.io.File

val out = File("/tmp/claude-1000/mix.txt").also { it.writeText("") }
fun say(s: String) { out.appendText(s + "\n") }

fun main() {
    Yt.init()
    for (vid in listOf("a5uQMwRMHcs", "VeauURD-qLk", "K0HSD_i2DvA", "2YibAiSQxwA", "5NV6Rdv1a3I")) {
        val r = runCatching { Yt.related(vid, 30).size }.getOrElse { -1 }
        val m = runCatching { Yt.mix(vid, 30) }.getOrElse { emptyList() }
        say("$vid  related=$r  mix=${m.size}   e.g. " + m.take(2).joinToString { it.title.take(30) })
    }
}
