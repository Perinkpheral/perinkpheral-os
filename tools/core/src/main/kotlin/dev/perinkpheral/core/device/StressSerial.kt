// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.util.concurrent.TimeUnit

/** USB capture never writes bytes. Tty echo must be disabled before reading. */
internal class StressSerial(
    private val port: String,
    private val file: File,
    private val command: (List<String>) -> Boolean = ::configure,
    private val silenceMs: Long = 10_000,
) : AutoCloseable {
    @Volatile private var running = true
    @Volatile private var input: FileInputStream? = null
    @Volatile private var problem = "console not captured: no bytes received"
    @Volatile private var received = false
    @Volatile private var silentOpen = false
    val diagnostic: String get() = problem.ifEmpty { if (received) "USB console: serial.log (raw, no echo)." else "console not captured: no bytes received" }

    private val reader = Thread.ofPlatform().daemon().name("stress serial").start {
        try {
            file.outputStream().use { output ->
                val buffer = ByteArray(4096)
                val flag = if (System.getProperty("os.name").lowercase().contains("mac")) "-f" else "-F"
                val arguments = listOf("stty", flag, port, "raw", "-echo", "clocal")
                while (running) {
                    try {
                        // Configure before opening, then again while our descriptor holds the tty open.
                        if (!File(port).exists()) throw IOException("port unavailable")
                        if (!command(arguments)) {
                            if (!File(port).exists()) throw IOException("port unavailable")
                            problem = "console not captured: cannot disable tty echo"
                            break
                        }
                        FileInputStream(port).use { stream ->
                            input = stream
                            if (!command(arguments)) {
                                if (!File(port).exists()) throw IOException("port unavailable")
                                problem = "console not captured: cannot disable tty echo"
                                return@use
                            }
                            val opened = System.nanoTime()
                            var bytesThisOpen = false
                            while (running) {
                                if (stream.available() == 0) {
                                    if (!File(port).exists()) throw IOException("port unavailable")
                                    if (!bytesThisOpen && (System.nanoTime() - opened) / 1_000_000 >= silenceMs) {
                                        silentOpen = true
                                        problem = "console not captured: no bytes received after opening $port"
                                    }
                                    Thread.sleep(50)
                                    continue
                                }
                                val count = stream.read(buffer)
                                if (count < 0) break
                                output.write(buffer, 0, count)
                                output.flush()
                                received = true
                                bytesThisOpen = true
                                if (!silentOpen && problem.startsWith("console not captured: no bytes")) problem = ""
                            }
                        }
                        if (problem.contains("cannot disable")) break
                    } catch (_: IOException) {
                        if (running) problem = "console not captured: port unavailable ($port)"
                    }
                    input = null
                    if (running) Thread.sleep(1000)
                }
            }
        } catch (_: InterruptedException) {
            // Closing interrupts the polling and reconnect pauses.
        } catch (error: Exception) {
            problem = "console not captured: ${error.message}"
        }
    }

    override fun close() {
        running = false
        try { input?.close() } finally {
            reader.interrupt()
            reader.join(2000)
        }
    }

    companion object {
        private fun configure(arguments: List<String>): Boolean {
            val process = ProcessBuilder(arguments).redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start()
            if (!process.waitFor(3, TimeUnit.SECONDS)) { process.destroyForcibly(); return false }
            return process.exitValue() == 0
        }
    }
}
