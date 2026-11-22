// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import java.io.File

// Runs only in-memory transport doubles and native policy/installer tests.
object NetworkRecoveryTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val tests = File(root, "firmware/test/host")
        val targets = listOf("test_wifi_plan", "test_poller_net", "test_app_install")
        fun run(command: List<String>) {
            val process = ProcessBuilder(command).directory(tests).redirectErrorStream(true).start()
            val output = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { "${command.joinToString(" ")} failed:\n$output" }
            print(output)
        }
        run(listOf("make") + targets)
        for (target in targets) run(listOf("./$target"))
        println("Network recovery: setup/rescue backoff, interrupted HTTP bodies and installer retries passed")
    }
}
