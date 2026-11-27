// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import java.io.File
import java.nio.file.Files
import java.util.Base64

object GraphicsToolsTest {
    @JvmStatic fun main(args: Array<String>) {
        val scratch = Files.createTempDirectory("perinkpheral-pictures-command").toFile()
        try {
            // Two opaque dark samples, with only the first matching the PNG transparency key.
            val fixtures = mapOf("grey" to "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAAAAADRSSBWAAAAAnRSTlMAIE397fAAAAALSURBVHicY1BgAAAAQwAhdpTEFgAAAABJRU5ErkJggg==", "rgb" to "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAIAAAB7QOjdAAAABnRSTlMAAQACAAPJS6v1AAAAD0lEQVR4nGNgZGJmZGIBAAAuAA7vvRSkAAAAAElFTkSuQmCC")
            for ((name, encoded) in fixtures) {
                File(scratch, "$name.png").writeBytes(Base64.getDecoder().decode(encoded))
            }
            val java = File(System.getProperty("java.home"), "bin/java").path
            val process = ProcessBuilder(java, "-cp", System.getProperty("java.class.path"),
                "dev.perinkpheral.cli.MainKt", "pictures", scratch.path).redirectErrorStream(true).start()
            val output = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { output }
            check(output == "grey:\n.#\n\nrgb:\n.#\n\n") { output }
        } finally {
            scratch.deleteRecursively()
        }
        println("pictures command: grey and RGB PNG transparency keys become paper")
    }
}
