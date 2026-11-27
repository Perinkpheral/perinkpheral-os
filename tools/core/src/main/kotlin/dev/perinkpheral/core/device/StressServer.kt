// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import java.io.ByteArrayOutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors
import kotlin.random.Random

/** Deliberately malformed HTTP/1.1 responses. Each connection snapshots its path's case. */
class StressServer(address: String, private val hit: (Map<String, Any?>) -> Unit = {}) : AutoCloseable {
    private val listener = ServerSocket().apply { bind(InetSocketAddress(InetAddress.getByName(address), 0)) }
    private val workers = Executors.newVirtualThreadPerTaskExecutor()
    private val sockets = ConcurrentHashMap.newKeySet<Socket>()
    private val cases = ConcurrentHashMap<String, String>()
    val hits = CopyOnWriteArrayList<Pair<String, String>>()
    val port: Int get() = listener.localPort

    init {
        workers.submit {
            while (!listener.isClosed) {
                val socket = try { listener.accept() } catch (_: java.io.IOException) { break }
                sockets += socket
                workers.submit {
                    try { socket.use { serve(it) } } catch (_: java.io.IOException) {
                        // The client may stop reading at any byte, including a chunk header.
                    } catch (_: InterruptedException) {
                        Thread.currentThread().interrupt()
                    } finally { sockets -= socket }
                }
            }
        }
    }

    fun select(path: String, case: String) { require(case in CASES); cases[path] = case }
    fun clear() { cases.clear(); sockets.toList().forEach { runCatching { it.close() } } }

    private fun serve(socket: Socket) {
        socket.soTimeout = 5_000
        val input = socket.getInputStream()
        val header = ByteArrayOutputStream()
        while (header.size() < 16_384) {
            val byte = input.read()
            if (byte < 0) return
            header.write(byte)
            if (header.size() >= 4 && header.toString(Charsets.ISO_8859_1).endsWith("\r\n\r\n")) break
        }
        val request = header.toString(Charsets.ISO_8859_1)
        val path = request.lineSequence().first().split(' ').getOrNull(1)?.substringBefore('?')?.trim('/') ?: return
        val length = Regex("(?im)^Content-Length: *([0-9]+)").find(request)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        if (length > 4096) return
        if (input.readNBytes(length).size != length) return
        val case = cases[path] ?: "ok"
        hits += path to case
        hit(mapOf("path" to path, "case" to case))
        if (case == "reset") { socket.setSoLinger(true, 0); return }
        if (case == "silent") { Thread.sleep(600_000); return }
        val body = body(path, case)
        val chunked = case in STREAMS
        val status = when (case) { "redirect" -> 302; "status" -> 599; else -> 200 }
        val declared = when (case) { "liar" -> 100_000L; "shortlen" -> 10L; "neglen" -> -5L; "hugelen" -> 99_999_999_999L; else -> body.size.toLong() }
        val head = buildString {
            append("HTTP/1.1 $status Stress\r\nContent-Type: ${if (case.startsWith("xml") || path == "xml") "application/xml" else if (case.startsWith("stream")) "text/plain" else "application/json"}\r\n")
            if (case == "redirect") append("Location: /$path\r\n")
            if (case == "headers") {
                repeat(200) { append("X-H$it: ${"v".repeat(900)}\r\n") }
                append("ETag: \"${"e".repeat(5000)}\"\r\nLink: <${"l".repeat(5000)}>; rel=next\r\n")
            }
            append(if (chunked) "Transfer-Encoding: chunked\r\n" else "Content-Length: $declared\r\n")
            append("Connection: close\r\n\r\n")
        }
        val output = socket.getOutputStream()
        output.write(head.toByteArray())
        output.flush()
        if (!chunked) { output.write(body); return }
        fun chunk(bytes: ByteArray) {
            output.write("${bytes.size.toString(16)}\r\n".toByteArray()); output.write(bytes)
            output.write("\r\n".toByteArray()); output.flush()
        }
        when (case) {
            "stream4m", "stream5m" -> repeat(if (case == "stream4m") 64 else 80) { chunk(ByteArray(65536) { 'x'.code.toByte() }) }
            "endless" -> while (!socket.isClosed) { chunk("{\"a\":".repeat(100).toByteArray()); Thread.sleep(50) }
            "trickle" -> "{\"a\":{\"b\":1},\"items\":[]}".toByteArray().forEach { Thread.sleep(2000); chunk(byteArrayOf(it)) }
            "stall" -> Thread.sleep(600_000)
        }
        output.write("0\r\n\r\n".toByteArray())
    }

    override fun close() {
        listener.close()
        clear()
        workers.shutdownNow()
    }

    companion object {
        val PICK_CASES = listOf("big255", "big300", "nest64", "nest65", "nest20000", "opennest50000", "objnest5000", "list", "manykeys", "longstr", "surrogates", "garbage", "badutf8", "liar", "shortlen", "neglen", "hugelen", "headers", "redirect", "status", "reset", "trickle", "stall", "endless")
        val POLL_CASES = PICK_CASES.map { "pick" to it } +
            listOf("doc12k", "doc13k", "nest64", "nest65", "nest20000", "objnest5000", "manykeys", "garbage", "surrogates", "liar", "headers", "endless").map { "doc" to it } +
            listOf("xmldeep", "xml65", "xmlattrs", "xmlent", "xmljunk", "xmlname", "garbage", "badutf8", "liar").map { "xml" to it } +
            listOf("stream4m", "stream5m", "endless", "garbage", "headers").map { "raw" to it }
        val CASES = (POLL_CASES.map { it.second } + listOf("ok", "silent")).toSet()
        private val STREAMS = setOf("stream4m", "stream5m", "endless", "trickle", "stall")

        internal fun body(path: String, case: String): ByteArray {
            val text = when {
                case == "ok" -> if (path == "xml") "<r><a b=\"1\">x</a></r>" else "{\"a\":{\"b\":1},\"items\":[{\"x\":1,\"y\":2}],\"deep\":3}"
                case == "big255" -> "{\"a\":{\"b\":1},\"pad\":\"${"a".repeat(255 * 1024 - 64)}\"}"
                case == "big300" -> "{\"a\":{\"b\":1},\"pad\":\"${"a".repeat(300 * 1024)}\"}"
                case.startsWith("nest") -> "[".repeat(case.drop(4).toInt()) + "]".repeat(case.drop(4).toInt())
                case.startsWith("opennest") -> "[".repeat(case.drop(8).toInt())
                case.startsWith("objnest") -> "{\"a\":".repeat(case.drop(7).toInt()) + "1" + "}".repeat(case.drop(7).toInt())
                case == "list" -> "{\"items\":[" + (0 until 3000).joinToString(",") { "{\"x\":\"${"v".repeat(60)}\",\"y\":$it}" } + "]}"
                case == "manykeys" -> (0 until 20000).joinToString(",", "{", "}") { "\"k$it\":$it" }
                case == "longstr" -> "{\"a\":{\"b\":\"${"\\u00e9".repeat(40000)}\"}}"
                case == "surrogates" -> "{\"a\":{\"b\":\"${"\\ud800".repeat(5000)}${"\\udc00\\uffff".repeat(5000)}\"}}"
                case == "garbage" -> return Random(0).nextBytes(200 * 1024)
                case == "badutf8" -> return "{\"a\":{\"b\":\"".toByteArray() + ByteArray(128 * 300) { (128 + it % 128).toByte() } + "\"}}".toByteArray()
                case == "liar" -> "{\"a\":{\"b\":1"
                case == "shortlen" -> "{\"a\":{\"b\":1},\"z\":\"${"z".repeat(50000)}\"}"
                case == "doc12k" -> "{\"p\":\"${"a".repeat(12288 - 9)}\"}"
                case == "doc13k" -> "{\"p\":\"${"a".repeat(13000)}\"}"
                case == "headers" -> "{\"a\":{\"b\":1}}"
                case == "xmldeep" -> "<a>".repeat(20000) + "</a>".repeat(20000)
                case == "xml65" -> "<r " + (0 until 65).joinToString(" ") { "a$it=\"1\"" } + "/>"
                case == "xmlattrs" -> ("<r " + (0 until 64).joinToString(" ") { "a$it=\"${"v".repeat(100)}\"" } + "/>").repeat(10)
                case == "xmlent" -> "<!DOCTYPE r [<!ENTITY a \"aaaaaaaaaa\"><!ENTITY b \"&a;&a;&a;&a;\">]><r>&b;&b;&b;</r>"
                case == "xmljunk" -> "<r><![CDATA[" + "]]".repeat(5000) + "<?pi " + "?".repeat(5000) + "<!--" + "-".repeat(5000) + "<a b=\">".repeat(2000)
                case == "xmlname" -> "<${"n".repeat(20000)}/>"
                case == "redirect" || case in STREAMS -> ""
                else -> "{}"
            }
            return text.toByteArray()
        }
    }
}
