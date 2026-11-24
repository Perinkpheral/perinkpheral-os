// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.simulator

import com.sun.net.httpserver.HttpServer
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.app.Applications
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** Exercises the native simulator through the Kotlin network bridge, using only loopback HTTP. */
object SimulatorMocksTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val wasi = File(System.getenv("WASI_SDK_PATH") ?: error("WASI_SDK_PATH is required"))
        val scratch = Files.createTempDirectory("perinkpheral-mocks-test").toFile()
        val hits = AtomicInteger()
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { exchange ->
            hits.incrementAndGet()
            val body = "{}".toByteArray()
            if (exchange.requestURI.path == "/picked") exchange.responseHeaders.set("Content-Type", "application/json")
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
        server.start()
        try {
            val native = File(root, "tools/simulator")
            val makeLog = File(scratch, "make.log")
            val make = ProcessBuilder("make", "test").directory(native).redirectErrorStream(true).redirectOutput(makeLog).start()
            check(make.waitFor(120, TimeUnit.SECONDS) && make.exitValue() == 0) { makeLog.readText() }
            val binary = File(native, "build/perinkpheral-sim").path
            val base = "http://127.0.0.1:${server.address.port}"
            val app = File(scratch, "probe").apply { mkdirs() }
            File(app, "manifest.json").writeText(Json.write(mapOf(
                "name" to "probe", "title" to "Probe", "author" to "Perinkpheral", "version" to "0.1.0", "abi" to 3,
                "hosts" to listOf("127.0.0.1:${server.address.port}"), "polls" to listOf(
                    mapOf("url" to "$base/picked", "every" to 60, "pick" to listOf("value")),
                    mapOf("url" to "$base/stream", "every" to 60)))))
            File(app, "probe.c").writeText("""
                /* SPDX-License-Identifier: MIT */
                #include "host_api.h"
                void app_on_load(void) {}
                void app_on_focus(void)
                {
                    static const char url[] = "$base/raw";
                    http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, url, sizeof url - 1, 0, 0, 0, 0);
                }
                void app_on_picked(int32_t request, int32_t status)
                {
                    (void)request;
                    if (status == HOST_FAILED_NO_ANSWER) log_write(HOST_LOG_INFO, "picked failed", 13);
                    if (status == 200) log_write(HOST_LOG_INFO, "picked ok", 9);
                }
                void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
                {
                    (void)data; (void)length;
                    if (!(flags & (HOST_DATA_FIRST | HOST_DATA_FAILED))) return;
                    if (request == HOST_REQUEST_FIRST) {
                        if (status == HOST_FAILED_NO_ANSWER) log_write(HOST_LOG_INFO, "raw failed", 10);
                        if (status == 200) log_write(HOST_LOG_INFO, "raw ok", 6);
                    } else {
                        if (status == HOST_FAILED_NO_ANSWER) log_write(HOST_LOG_INFO, "stream failed", 13);
                        if (status == 200) log_write(HOST_LOG_INFO, "stream ok", 9);
                    }
                }
            """.trimIndent() + "\n")
            val packed = File(scratch, "packed")
            AppBuilder(Sdk(root), wasi) {}.build(app, packed)
            var run = 0
            fun simulate(mocks: File?, failure: String? = null, replies: String? = null, relative: Boolean = false) {
                val state = File(scratch, "state-${run++}")
                Applications.install(packed, "probe", state)
                val apps = File(state, "apps")
                // A missing relative path must not be resolved again under --apps.
                if (relative) File(apps, mocks!!.name).writeText("{}")
                val log = File(state, "sim.log")
                val port = File(state, "sim.port")
                val command = mutableListOf(binary, "--apps", apps.path, "--app", "probe", "--state", state.path,
                    "--port", "0", "--port-file", port.path, "--fetcher-port", SimulatorNetwork.port().toString())
                if (mocks != null) command += listOf("--mocks", if (relative) mocks.name else mocks.path)
                val process = ProcessBuilder(command).directory(scratch).redirectErrorStream(true).redirectOutput(log).start()
                try {
                    if (failure != null) {
                        check(process.waitFor(10, TimeUnit.SECONDS)) { "invalid mocks did not stop the simulator: ${log.readText()}" }
                        check(process.exitValue() != 0 && failure in log.readText()) { log.readText() }
                        check(!port.exists()) { "invalid mocks started the simulator" }
                    } else {
                        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
                        while (true) {
                            val output = log.readText()
                            if (listOf("raw", "picked", "stream").all { "$it $replies" in output }) break
                            check(process.isAlive && System.nanoTime() < deadline) { output }
                            Thread.sleep(20)
                        }
                        if (replies == "failed") check("network disabled by --mocks" in log.readText()) { log.readText() }
                    }
                } finally {
                    process.destroy()
                    if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly().waitFor()
                }
            }
            // A live control proves that the requests reach HTTP when no mocks are supplied.
            simulate(null, replies = "ok")
            check(hits.get() == 3) { "the live control sent ${hits.get()} requests" }
            simulate(File(scratch, "absent.json"), failure = "cannot be read")
            simulate(File(scratch, "relative.json"), failure = "cannot be read", relative = true)
            simulate(scratch, failure = "mocks")
            val mocks = File(scratch, "mocks.json")
            for (invalid in listOf("", "{", "[]", "null", "{} trailing", "{}\u0000trailing", "{\"$base/raw\":false}")) {
                mocks.writeText(invalid)
                simulate(mocks, failure = "JSON object")
            }
            for (answers in listOf(emptyMap(), mapOf("$base/other" to emptyMap<String, Any>()),
                    mapOf("$base/*" to mapOf("method" to "POST")),
                    mapOf("$base/*" to mapOf("request_body" to "different")))) {
                mocks.writeText(Json.write(answers))
                simulate(mocks, replies = "failed")
            }
            mocks.writeText(Json.write(mapOf("$base/raw" to mapOf("body" to "{}"),
                "$base/picked" to mapOf("method" to "GET", "request_body" to "", "body" to "{}", "content_type" to "application/json"),
                "$base/*" to mapOf("method" to "GET", "request_body" to "", "body" to "{}"))))
            simulate(mocks, replies = "ok")
            check(hits.get() == 3) { "mocked requests leaked to HTTP: ${hits.get() - 3}" }
        } finally {
            server.stop(0)
            scratch.deleteRecursively()
        }
        println("Simulator mocks: invalid files stop startup; unmatched raw, picked and streamed requests stay offline; matching mocks and live HTTP passed.")
    }
}
