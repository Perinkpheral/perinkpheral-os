// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import java.io.ByteArrayOutputStream
import java.net.Socket
import java.net.SocketException
import java.net.SocketTimeoutException

/** Socket tests intentionally bypass an HTTP client's framing repairs. */
object StressServerTest {
    @JvmStatic fun main(args: Array<String>) {
        val accepted = java.util.concurrent.CountDownLatch(1)
        val dispatch = java.util.concurrent.CountDownLatch(1)
        StressServer("127.0.0.1").use { server ->
            server.beforeDispatch = {
                accepted.countDown()
                // Shutdown interrupts the accept worker before dispatch resumes.
                try { dispatch.await() } catch (_: InterruptedException) { dispatch.await() }
            }
            Socket("127.0.0.1", server.port).use { socket ->
                socket.soTimeout = 2000
                check(accepted.await(2, java.util.concurrent.TimeUnit.SECONDS))
                try { server.close() } finally { dispatch.countDown() }
                check(socket.getInputStream().read() == -1) { "socket accepted during shutdown stayed open" }
            }
        }
        StressServer("127.0.0.1").use { server ->
            check(StressServer.POLL_CASES.size == 51)
            for (case in StressServer.CASES) {
                server.select("pick", case)
                Socket("127.0.0.1", server.port).use { socket ->
                    socket.soTimeout = if (case in listOf("silent", "stall", "trickle")) 150 else 5000
                    socket.getOutputStream().write("GET /pick HTTP/1.1\r\nHost: localhost\r\n\r\n".toByteArray())
                    val input = socket.getInputStream()
                    if (case == "reset") {
                        try { check(input.read() < 0) } catch (_: SocketException) { }
                    } else if (case == "silent") {
                        try { input.read(); error("silent case sent bytes") } catch (_: SocketTimeoutException) { }
                    } else {
                        val header = ByteArrayOutputStream()
                        while (!header.toString(Charsets.ISO_8859_1).endsWith("\r\n\r\n")) {
                            val byte = input.read(); check(byte >= 0); header.write(byte)
                        }
                        val head = header.toString(Charsets.ISO_8859_1)
                        when (case) {
                            "stall", "trickle" -> try { input.read(); error("$case did not hold its body") } catch (_: SocketTimeoutException) { }
                            "endless" -> {
                                check(head.contains("Transfer-Encoding: chunked"))
                                check(String(input.readNBytes(5)) == "1f4\r\n")
                                check(input.readNBytes(500).toString(Charsets.UTF_8) == "{\"a\":".repeat(100))
                            }
                            "stream4m", "stream5m" -> {
                                check(head.contains("Transfer-Encoding: chunked"))
                                val bytes = input.readBytes().toString(Charsets.US_ASCII)
                                val count = if (case == "stream4m") 64 else 80
                                check(bytes == ("10000\r\n" + "x".repeat(65536) + "\r\n").repeat(count) + "0\r\n\r\n")
                            }
                            else -> {
                                val body = input.readBytes()
                                check(body.contentEquals(StressServer.body("pick", case))) { "$case body" }
                                val declared = Regex("Content-Length: (-?\\d+)").find(head)!!.groupValues[1].toLong()
                                when (case) {
                                    "liar", "hugelen" -> check(declared > body.size)
                                    "shortlen" -> check(declared == 10L && body.size > declared)
                                    "neglen" -> check(declared == -5L)
                                    else -> check(declared == body.size.toLong()) { case }
                                }
                                if (case == "headers") {
                                    check(Regex("X-H\\d+: " + "v".repeat(900) + "\r\n").findAll(head).count() == 200)
                                    check(head.contains("ETag: \"" + "e".repeat(5000)))
                                }
                                if (case == "redirect") check(head.contains("HTTP/1.1 302") && head.contains("Location: /pick"))
                                if (case == "status") check(head.contains("HTTP/1.1 599"))
                            }
                        }
                    }
                }
                server.clear()
            }
            check(StressServer.body("doc", "doc12k").size == 12288)
            check(StressServer.body("doc", "doc12k+1").size == 12289)
            // The case belongs to the path; another path keeps its ordinary answer.
            server.select("doc", "liar")
            Socket("127.0.0.1", server.port).use { socket ->
                socket.soTimeout = 2000
                socket.getOutputStream().write("GET /xml?next=1 HTTP/1.1\r\nHost: localhost\r\n\r\n".toByteArray())
                check(socket.getInputStream().readBytes().toString(Charsets.UTF_8).endsWith("<r><a b=\"1\">x</a></r>"))
            }
        }
        println("Stress hostile socket checks passed")
    }
}
