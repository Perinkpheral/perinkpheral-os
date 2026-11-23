// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files

/** Invalid scenario mocks must not silently turn a test into a live-network run. */
object SimTestMocksTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-scenario-mocks").toFile()
        try {
            val cases = mapOf("missing" to "missing.json", "malformed" to "invalid.json",
                "number" to 1L, "boolean" to false, "array" to emptyList<String>(), "null" to null,
                "empty" to emptyMap<String, Any>())
            for ((name, mocks) in cases) {
                val folder = File(scratch, name).apply { mkdirs() }
                File(folder, "scenario.json").writeText(Json.write(mapOf(
                    "app" to File(root, "sdk/apps/hello").path, "mocks" to mocks, "steps" to emptyList<String>())))
                File(folder, "invalid.json").writeText("{")
            }
            val wasi = File(System.getenv("WASI_SDK_PATH") ?: error("WASI_SDK_PATH is required"))
            val failures = SimTest(root) { AppBuilder(Sdk(root), wasi) {} }.run(scratch, false, workers = 3)
            check(failures.size == cases.size - 1) { failures.joinToString("\n") }
            for (name in cases.keys - "empty") {
                val failure = failures.single { it.startsWith("$name:") }
                val expected = when (name) {
                    "missing" -> "cannot be read"
                    "malformed" -> "JSON object"
                    else -> "\"mocks\" must be an object or the name of a file"
                }
                check(expected in failure) { failure }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("sim-test mocks: invalid types and unreadable or malformed files fail; an empty object stays valid.")
    }
}
