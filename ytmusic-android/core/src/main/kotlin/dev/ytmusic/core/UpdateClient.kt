package dev.ytmusic.core

import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** Talking to the home-network update server: fetch its manifest, and find it when its address changed. */
object UpdateClient {
    /** Fetches `<base>version.json`; null if nothing valid answers there. */
    fun fetchManifest(client: OkHttpClient, base: String): JSONObject? = try {
        client.newCall(Request.Builder().url(base + "version.json").header("Cache-Control", "no-cache").build()).execute().use { r ->
            if (r.isSuccessful) JSONObject(r.body!!.string()).also { it.getInt("versionCode") } else null
        }
    } catch (_: Throwable) { null }

    /** The /24 prefixes ("192.168.1.") of this machine's private IPv4 addresses. */
    fun localPrefixes(): List<String> {
        val prefixes = LinkedHashSet<String>()
        try {
            for (ni in NetworkInterface.getNetworkInterfaces().toList()) {
                if (!ni.isUp || ni.isLoopback) continue
                for (ia in ni.inetAddresses) {
                    val a = ia as? Inet4Address ?: continue
                    if (a.isSiteLocalAddress) prefixes += a.hostAddress!!.substringBeforeLast('.') + "."
                }
            }
        } catch (_: Throwable) { }
        return prefixes.toList()
    }

    /**
     * Probes every address of each local /24 on [port] (cheap TCP connect first, then a real manifest
     * request) and returns the base URL of the first server that answers, or null.
     */
    fun discover(client: OkHttpClient, port: Int, extraHosts: List<String> = emptyList()): String? {
        val hosts = ArrayList<String>(extraHosts)
        for (prefix in localPrefixes()) for (n in 1..254) hosts += prefix + n
        if (hosts.isEmpty()) return null
        val quick = client.newBuilder().callTimeout(1500, TimeUnit.MILLISECONDS).connectTimeout(500, TimeUnit.MILLISECONDS).build()
        val pool = Executors.newFixedThreadPool(48)
        val found = AtomicReference<String?>(null)
        try {
            val jobs = ArrayList<Future<*>>()
            for (host in hosts) jobs += pool.submit {
                if (found.get() != null) return@submit
                try {
                    Socket().use { it.connect(InetSocketAddress(host, port), 350) }
                    val base = "http://$host:$port/"
                    if (fetchManifest(quick, base) != null) found.compareAndSet(null, base)
                } catch (_: Throwable) { }
            }
            for (j in jobs) { if (found.get() != null) break; try { j.get(8, TimeUnit.SECONDS) } catch (_: Throwable) { } }
        } finally { pool.shutdownNow() }
        return found.get()
    }
}
