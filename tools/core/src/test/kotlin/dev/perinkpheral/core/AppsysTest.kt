// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import java.io.File

/** Runs the application regressions in the real interpreter, without a device or network. */
object AppsysTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("perinkpheral.test.root", System.getProperty("user.dir"))).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        check(!System.getenv("WASI_SDK_PATH").isNullOrEmpty()) { "WASI_SDK_PATH is required" }
        val host = File(root, "firmware/test/host")
        run(host, "make", "test_app", "test_delivery")
        for (test in listOf("test_app", "test_delivery")) {
            val output = run(host, File(host, test).path)
            check(Regex("(?m)^\\d+ checks, 0 failed$").containsMatchIn(output)) {
                "$test did not report its assertions: $output"
            }
        }
        println("Application runtime: malformed name bounds and stream instance continuity passed.")
    }

    private fun run(directory: File, vararg command: String): String {
        val process = ProcessBuilder(*command).directory(directory).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().use { it.readText() }
        check(process.waitFor() == 0) { "${command.joinToString(" ")} failed:\n$output" }
        return output
    }
}
