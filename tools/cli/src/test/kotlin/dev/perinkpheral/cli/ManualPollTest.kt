// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.Json
import dev.perinkpheral.core.app.Applications
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.device.DeviceClient
import java.io.File
import java.nio.file.Files
import java.util.concurrent.TimeUnit

/** Runs a rebuilt application against local mocks, with no device or live HTTP source. */
object ManualPollTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val wasi = File(System.getenv("WASI_SDK_PATH") ?: error("WASI_SDK_PATH is required"))
        val scratch = Files.createTempDirectory("perinkpheral-manual-polls").toFile()
        try {
            val makeLog = File(scratch, "make.log")
            val make = ProcessBuilder("make").directory(File(root, "tools/simulator"))
                .redirectErrorStream(true).redirectOutput(makeLog).start()
            check(make.waitFor(120, TimeUnit.SECONDS) && make.exitValue() == 0) { makeLog.readText() }
            val app = File(scratch, "probe").apply { mkdirs() }
            File(app, "manifest.json").writeText(Json.write(mapOf(
                "name" to "probe", "title" to "Probe", "author" to "Perinkpheral", "version" to "0.1.0", "abi" to 3,
                "hosts" to listOf("example.test"), "intents" to listOf("network_up", "time_changed"),
                "polls" to listOf(
                    mapOf("url" to "https://example.test/picked", "manual" to true, "format" to "json", "pick" to listOf("value")),
                    mapOf("url" to "https://example.test/stream", "manual" to true)))))
            File(app, "probe.c").writeText("""
                /* SPDX-License-Identifier: MIT */
                #include "host_api.h"
                void app_on_load(void) {
                    if (poll_set_interval(0, 60) != HOST_ERR_ARGUMENT) log_write(HOST_LOG_ERROR, "interval accepted", 17);
                }
                void app_on_intent(int32_t kind) { (void)kind; }
                void app_on_button(int32_t event) {
                    if (event == BTN_UP_PRESSED) poll_run(0);
                    if (event == BTN_OK_PRESSED) poll_run(1);
                    if (event == BTN_DOWN_PRESSED) {
                        static const char url[] = "https://example.test/raw";
                        http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, url, sizeof url - 1, 0, 0, 0, 0);
                    }
                }
                void app_on_picked(int32_t request, int32_t status) {
                    (void)request;
                    if (status == 429) log_write(HOST_LOG_INFO, "picked response", 15);
                }
                void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status) {
                    (void)data; (void)length; (void)status;
                    if (!(flags & HOST_DATA_FIRST)) return;
                    if (request == 1) log_write(HOST_LOG_INFO, "stream response", 15);
                    if (request == HOST_REQUEST_FIRST) log_write(HOST_LOG_INFO, "raw response", 12);
                }
            """.trimIndent() + "\n")
            val packed = File(scratch, "packed")
            val sdk = Sdk(root)
            AppBuilder(sdk, wasi) {}.build(app, packed)
            val manifest = Json.parse(File(packed, "manifest.json").readText()) as Map<*, *>
            check(manifest["firmware"] == sdk.firmwareFirst)
            val state = File(scratch, "state")
            Applications.install(packed, "probe", state)
            val mocks = File(scratch, "mocks.json")
            mocks.writeText(Json.write(mapOf(
                "https://example.test/picked" to mapOf("status" to 429, "body" to "{}", "headers" to mapOf("Retry-After" to "300")),
                "https://example.test/stream" to mapOf("body" to "ok"),
                "https://example.test/raw" to mapOf("status" to 429, "headers" to mapOf("Retry-After" to "900")))))
            val port = File(scratch, "port")
            val log = File(scratch, "sim.log")
            val process = ProcessBuilder(File(root, "tools/simulator/build/perinkpheral-sim").path,
                "--apps", File(state, "apps").path, "--app", "probe", "--state", state.path,
                "--port", "0", "--port-file", port.path, "--mocks", mocks.path)
                .redirectErrorStream(true).redirectOutput(log).start()
            fun await(condition: () -> Boolean) {
                val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
                while (!condition()) {
                    check(process.isAlive && System.nanoTime() < deadline) { log.readText() }
                    Thread.sleep(10)
                }
            }
            try {
                await { port.isFile && port.readText().trim().toIntOrNull() != null }
                val client = DeviceClient("127.0.0.1:${port.readText().trim()}")
                fun post(path: String) { client.raw(path, "POST") }
                fun count(text: String) = log.readText().split(text).size - 1
                post("/api/sim/forward?seconds=3600")
                post("/api/dev/kv?key=unused")
                post("/api/sim/intent?kind=network_up")
                Thread.sleep(100)
                check(count("picked response") == 0 && count("stream response") == 0) { log.readText() }
                post("/api/dev/button?button=1")
                await { count("picked response") == 1 }
                repeat(3) { post("/api/dev/button?button=2") }
                post("/api/sim/forward?seconds=2")
                post("/api/dev/kv?key=unused")
                post("/api/sim/intent?kind=network_up")
                post("/api/sim/intent?kind=time_changed")
                post("/api/dev/button?button=3")
                await { count("raw response") == 1 }
                check(count("stream response") == 0) { log.readText() }
                post("/api/sim/forward?seconds=301")
                await { count("stream response") == 1 }
                post("/api/sim/forward?seconds=86400")
                Thread.sleep(100)
                check(count("picked response") == 1 && count("stream response") == 1) { log.readText() }
                check("interval accepted" !in log.readText()) { log.readText() }
            } finally {
                process.destroy()
                if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly().waitFor()
            }
        } finally { scratch.deleteRecursively() }
        println("Manual polls: packaged minimum, absent automatic triggers, deferred/coalesced dispatch and raw cooldown isolation passed.")
    }
}
