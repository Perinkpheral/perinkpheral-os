// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files
import java.awt.image.BufferedImage
import javax.imageio.ImageIO

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
            for ((name, status, body) in listOf(
                Triple("picked-html-refusal", 503, "<html>refused</html>"),
                Triple("picked-invalid-json", -2, "{\"value\":1,}"),
                Triple("picked-json-text", 200, "{\"value\":1}"))) {
                val app = File(scratch, "applications/$name/probe").apply { mkdirs() }
                File(app, "manifest.json").writeText(Json.write(mapOf(
                    "name" to "probe", "title" to "Probe", "author" to "Perinkpheral", "abi" to 3,
                    "fullscreen" to true, "polls" to listOf(mapOf("url" to "https://example.test/value",
                        "every" to 60, "pick" to listOf("value"))))))
                File(app, "probe.c").writeText("""
                    /* SPDX-License-Identifier: MIT */
                    #include "host_api.h"
                    static int correct;
                    void app_on_picked(int32_t request, int32_t status)
                    {
                        (void)request;
                        correct = status == $status;
                        screen_redraw(HOST_REDRAW_SOON);
                    }
                    void app_on_draw(void) { if (correct) gfx_draw_rect(0, 0, 400, 300, 1); }
                """.trimIndent())
                val folder = File(scratch, name).apply { mkdirs() }
                File(folder, "scenario.json").writeText(Json.write(mapOf("app" to app.path,
                    "mocks" to mapOf("https://example.test/value" to mapOf(
                        "status" to if (status == 503) 503 else 200, "body" to body, "content_type" to "text/html")),
                    "steps" to listOf("wait 1", "forward 2", "shot picked"))))
                val expected = BufferedImage(400, 300, BufferedImage.TYPE_INT_RGB)
                ImageIO.write(expected, "png", File(folder, "picked.png"))
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
