// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import java.io.File
import java.io.FileInputStream
import java.io.IOException

/** Read-only USB capture. No port configuration, output descriptor or modem-control calls. */
internal class StressSerial(private val port: String, private val file: File) : AutoCloseable {
    @Volatile private var running = true
    @Volatile private var input: FileInputStream? = null
    private val reader = Thread.ofPlatform().daemon().name("stress serial").start {
        file.outputStream().use { output ->
            val buffer = ByteArray(4096)
            while (running) {
                try {
                    FileInputStream(port).use { stream ->
                        input = stream
                        while (running) {
                            if (stream.available() == 0) { Thread.sleep(50); continue }
                            val count = stream.read(buffer)
                            if (count < 0) break
                            output.write(buffer, 0, count)
                            output.flush()
                        }
                    }
                } catch (_: IOException) {
                    // USB may disappear during a requested or unexpected restart.
                } catch (_: InterruptedException) { break }
                input = null
                if (running) try { Thread.sleep(1000) } catch (_: InterruptedException) { break }
            }
        }
    }
    override fun close() {
        running = false
        input?.close()
        reader.interrupt()
        reader.join(2000)
    }
}
