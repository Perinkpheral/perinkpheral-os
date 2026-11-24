// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import com.sun.net.httpserver.HttpServer
import dev.perinkpheral.core.Json
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.time.Instant
import java.net.SocketTimeoutException

object EnduranceTest {
    @JvmStatic fun main(args: Array<String>) {
        testRetryState()
        val image = "a".repeat(64)
        val baseline = mapOf("image_id" to image, "mode" to "application", "on_probation" to false,
            "uptime_s" to 50L, "storage_ok" to true, "failed_allocations" to 0L, "memory_refusals" to 0L)
        check(Endurance.statusEvents(baseline, baseline + ("uptime_s" to 51L), image).isEmpty())
        check("uptime_decreased" in Endurance.statusEvents(baseline, baseline + ("uptime_s" to 1L), image))
        check("failed_allocations" in Endurance.statusEvents(baseline, baseline + ("failed_allocations" to 1L), image))
        check("memory_refusals" in Endurance.statusEvents(baseline, baseline + ("memory_refusals" to 2L), image))
        check("image_changed" in Endurance.statusEvents(baseline, baseline + ("image_id" to "b".repeat(64)), image))
        check("panic_record" in Endurance.statusEvents(baseline, baseline + ("last_panic" to "watchdog"), image))
        check("storage_unavailable" in Endurance.statusEvents(baseline, baseline + ("storage_ok" to false), image))
        val cursor = Endurance.LogCursor("one", 100)
        check(Endurance.missingBytes(cursor, Endurance.LogCursor("one", 5100), 4096) == 904L)
        check(Endurance.missingBytes(cursor, Endurance.LogCursor("two", 5100), 4096) == 0L)
        check(Endurance.missingBytes(Endurance.LogCursor("one", 0xffff_ffffL - 2),
            Endurance.LogCursor("one", 3), 6) == 0L)

        val directory = Files.createTempDirectory("perinkpheral-soak-test").toFile()
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val calls = AtomicInteger()
        val writes = AtomicInteger()
        server.createContext("/") { exchange ->
            if (exchange.requestMethod != "GET") writes.incrementAndGet()
            val log = exchange.requestURI.path == "/api/log"
            val call = if (log) calls.incrementAndGet() else 0
            val body = if (log) "new log\n" else Json.write(baseline)
            if (log) {
                exchange.responseHeaders.set("X-Log-Boot", if (call <= 1) "old" else "new")
                exchange.responseHeaders.set("X-Log-End", if (call <= 1) "100" else "200")
            }
            val bytes = body.toByteArray()
            exchange.sendResponseHeaders(200, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
        try {
            Endurance(DeviceClient("127.0.0.1:${server.address.port}"), directory, image).run(0.0012)
            check(writes.get() == 0) { "observer modified the device" }
            val events = directory.resolve("events.jsonl").readText()
            check("device_restarted" in events) { "restart with a larger log cursor was missed" }
            val progress = Json.parse(directory.resolve("progress.json").readText()) as Map<*, *>
            check(progress["state"] == "awaiting_review") { "observation was called a pass" }
            check((progress["samples"] as Number).toLong() > 0)
            val original = directory.resolve("run.json").readText()
            Endurance(DeviceClient("127.0.0.1:${server.address.port}"), directory, image).run(72.0)
            check(directory.resolve("run.json").readText() == original) { "resume extended the run" }
        } finally {
            server.stop(0)
            directory.deleteRecursively()
        }
        testRecovery(baseline, image)
        println("Endurance observer checks passed")
    }
    private fun testRetryState() {
        val observation = Endurance.Observation()
        observation.succeeded(1_000)
        observation.failed(2_000, 5_000)
        check(observation.retryMs() == 2_000L)
        observation.failed(7_000, 10_000)
        check(observation.retryMs() == 4_000L)
        repeat(20) { observation.failed(14_000, 17_000) }
        check(observation.retryMs() == 5_000L)
        check(observation.lastSuccessAt == 1_000L)
        check(observation.failureStartedAt == 2_000L && observation.failureAt == 5_000L)
        val restored = Endurance.Observation()
        restored.restore(observation.record())
        check(restored.record() == observation.record())
        observation.succeeded(19_000)
        check(observation.recoveryAt == 19_000L && observation.lastSuccessAt == 19_000L)
        check(observation.failures == 0)
        observation.failed(20_000, 21_000)
        check(observation.retryMs() == 2_000L && observation.failureAt == 21_000L)
        check(observation.recoveryAt == null)
    }

    private fun testRecovery(baseline: Map<String, Any>, image: String) {
        val directory = Files.createTempDirectory("perinkpheral-recovery-test").toFile()
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val clock = AtomicLong(1_000_000)
        val logs = AtomicInteger()
        val statuses = AtomicInteger()
        val waits = mutableListOf<Long>()
        server.createContext("/") { exchange ->
            check(exchange.requestMethod == "GET")
            val log = exchange.requestURI.path == "/api/log"
            val call = if (log) logs.incrementAndGet() else statuses.incrementAndGet()
            val failed = call in 2..4
            if (failed) clock.addAndGet(1_000) // request latency is distinct from retry sleep
            if (log) {
                exchange.responseHeaders.set("X-Log-Boot", "one")
                exchange.responseHeaders.set("X-Log-End", "0")
            }
            val bytes = (if (log) "" else Json.write(baseline)).toByteArray()
            exchange.sendResponseHeaders(if (failed) 503 else 200, if (bytes.isEmpty()) -1 else bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.createContext("/slow") { exchange ->
            Thread.sleep(150)
            exchange.close()
        }
        server.start()
        try {
            val client = DeviceClient("127.0.0.1:${server.address.port}")
            Endurance(client, directory, image, clock::get) { delay ->
                waits += delay
                clock.addAndGet(delay)
            }.run(0.01)
            check(waits.all { it in 1..5_000 }) { "failure delayed observation: $waits" }
            check(logs.get() > 5 && statuses.get() >= 5)
            val events = directory.resolve("events.jsonl").readLines().map { Json.parse(it) as Map<*, *> }
            for (source in listOf("log", "status")) {
                val failures = events.filter { it["event"] == "observation_unavailable" &&
                    (it["detail"] as? Map<*, *>)?.get("source") == source }
                check(failures.size == 1)
                val recovery = events.single { it["event"] == "observation_restored" &&
                    (it["detail"] as? Map<*, *>)?.get("source") == source }["detail"] as Map<*, *>
                fun time(key: String) = Instant.parse(recovery[key] as String).toEpochMilli()
                check(time("last_success_at") < time("failure_at"))
                check(time("failure_request_started_at") < time("failure_at"))
                check(time("last_failure_at") < time("recovery_at"))
                check((recovery["unavailable_ms"] as Number).toLong() == time("recovery_at") - time("failure_at"))
                check((recovery["outage_upper_bound_ms"] as Number).toLong() == time("recovery_at") - time("last_success_at"))
            }
            val progress = Json.parse(directory.resolve("progress.json").readText()) as Map<*, *>
            val observations = progress["observations"] as Map<*, *>
            check((observations["log"] as Map<*, *>)["recovery_at"] != null)
            try {
                client.raw("/slow", timeoutMs = 25)
                error("raw observation ignored its read timeout")
            } catch (_: SocketTimeoutException) { }
        } finally {
            server.stop(0)
            directory.deleteRecursively()
        }
    }

}
