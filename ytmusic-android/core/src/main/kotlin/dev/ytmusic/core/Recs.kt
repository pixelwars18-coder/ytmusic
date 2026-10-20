package dev.ytmusic.core

import java.util.concurrent.Callable
import java.util.concurrent.Executors
import kotlin.random.Random

/** One shelf on the "For you" page. */
data class Section(val id: String, val title: String, val subtitle: String, val tracks: List<Track>)

/**
 * Builds the "For you" page from what the person plays and likes (no account involved):
 *  - "Made for you": YouTube's related songs for a handful of recent / liked songs, merged and ranked by how
 *    many of those songs point at them;
 *  - "Because you liked …": related songs of individual liked songs;
 *  - "Trending": a global music chart, which also fills the page on a brand-new install.
 */
object Recs {
    const val CHARTS = "https://www.youtube.com/playlist?list=PL4fGSI1pDJn6puJdseH2Rt9sMvt9E2M4i"

    /** YouTube's Mix for a song (fuller than "related"), falling back to related if the Mix can't be loaded. */
    private fun neighbours(vid: String): List<Track> =
        runCatching { Yt.mix(vid, 25) }.getOrDefault(emptyList()).ifEmpty { runCatching { Yt.related(vid, 25) }.getOrDefault(emptyList()) }

    private fun shorten(s: String, n: Int = 34) = if (s.length <= n) s else s.take(n - 1).trimEnd() + "…"

    fun build(recent: List<Track>, liked: List<Track>, random: Random = Random.Default): List<Section> {
        val heard = recent.map { it.vid }.toHashSet()
        val pool = Executors.newFixedThreadPool(6)
        try {
            // seeds: distinct artists among the latest plays, plus a couple of random likes
            val seeds = LinkedHashMap<String, Track>()
            val artists = HashSet<String>()
            for (t in recent.take(24)) if (seeds.size < 4 && artists.add(t.artist)) seeds[t.vid] = t
            for (t in liked.shuffled(random)) { if (seeds.size >= 6) break; if (t.vid !in seeds && artists.add(t.artist)) seeds[t.vid] = t }

            val relatedJobs = seeds.keys.map { vid -> pool.submit(Callable { neighbours(vid) }) }
            // "Because you liked" shelves use up to two random liked songs (re-using lists already being fetched)
            val becauseSeeds = liked.shuffled(random).take(2)
            val seedJobs = seeds.keys.zip(relatedJobs).toMap()
            val becauseJobs = becauseSeeds.map { t -> seedJobs[t.vid] ?: pool.submit(Callable { neighbours(t.vid) }) }
            val chartsJob = pool.submit(Callable { runCatching { Yt.playlistTracks(CHARTS).items.filter { it.duration in 60..900 }.take(25) }.getOrDefault(emptyList()) })

            val related = seeds.values.zip(relatedJobs.map { it.get() })
            val out = ArrayList<Section>()
            val used = HashSet<String>()

            if (related.isNotEmpty()) {
                val score = HashMap<String, Double>()
                val byId = HashMap<String, Track>()
                for ((_, list) in related) list.forEachIndexed { rank, t ->
                    if (t.vid in seeds || t.vid in heard) return@forEachIndexed
                    score[t.vid] = (score[t.vid] ?: 0.0) + (list.size - rank).toDouble() / list.size
                    byId[t.vid] = t
                }
                val mix = score.entries.sortedWith(compareByDescending<Map.Entry<String, Double>> { it.value }.thenBy { random.nextInt() })
                    .take(30).map { byId.getValue(it.key) }
                if (mix.size >= 5) {
                    out += Section("mix", "Made for you", "Based on what you listen to", mix)
                    used += mix.map { it.vid }
                }
            }
            // one shelf per liked song, skipping what is already shown above
            for ((seed, job) in becauseSeeds.zip(becauseJobs)) {
                val tracks = job.get().filter { it.vid !in used && it.vid !in heard && it.vid != seed.vid }.take(10)
                if (tracks.size >= 4) {
                    out += Section("because-${seed.vid}", "Because you liked ${shorten(seed.title)}", seed.artist, tracks)
                    used += tracks.map { it.vid }
                }
            }
            val charts = chartsJob.get().filter { it.vid !in used }
            if (charts.isNotEmpty()) out += Section("trending", "Trending music", "What everyone is playing right now", charts)
            return out
        } finally { pool.shutdownNow() }
    }
}
