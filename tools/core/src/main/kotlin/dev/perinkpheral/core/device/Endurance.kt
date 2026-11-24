// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import dev.perinkpheral.core.Json
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.time.Instant

/** Read-only observation of a fixed firmware image. Never repairs or restarts the device. */
class Endurance(private val client: DeviceClient, private val directory: File, private val image: String,
                private val now: () -> Long = System::currentTimeMillis,
                private val sleep: (Long) -> Unit = Thread::sleep) {
    data class LogCursor(val boot: String, val end: Long)

    companion object {
        private const val STATUS_MS = 60_000L
        private const val LOG_MS = 2_000L
        private const val SLEEP_MS = 30_000L
        private const val REQUEST_MS = 3_000
        private const val COUNTER_MASK = 0xffff_ffffL

        fun missingBytes(previous: LogCursor?, current: LogCursor, received: Int): Long =
            if (previous == null || previous.boot != current.boot) 0L
            else (((current.end - previous.end) and COUNTER_MASK) - received).coerceAtLeast(0L)

        fun statusEvents(previous: Map<*, *>?, current: Map<*, *>, image: String): List<String> {
            val events = mutableListOf<String>()
            fun number(map: Map<*, *>?, key: String) = (map?.get(key) as? Number)?.toLong() ?: 0L
            if (current["image_id"] != image && previous?.get("image_id") != current["image_id"])
                events += "image_changed"
            if (current["storage_ok"] == false && previous?.get("storage_ok") != false)
                events += "storage_unavailable"
            if (current["last_panic"] != null && current["last_panic"] != previous?.get("last_panic"))
                events += "panic_record"
            if (previous != null && number(current, "uptime_s") < number(previous, "uptime_s"))
                events += "uptime_decreased"
            for (key in listOf("failed_allocations", "memory_refusals")) {
                if (number(current, key) > number(previous, key)) events += key
            }
            return events
        }
    }

    /** Each endpoint has its own outage: a working status route does not prove log delivery. */
    internal class Observation {
        var lastSuccessAt: Long? = null
        var failureStartedAt: Long? = null
        var failureAt: Long? = null
        var lastFailureAt: Long? = null
        var recoveryAt: Long? = null
        var failures = 0

        fun failed(started: Long, at: Long) {
            if (failures == 0) {
                failureStartedAt = started
                failureAt = at
                recoveryAt = null
            }
            lastFailureAt = at
            if (failures < Int.MAX_VALUE) failures++
        }

        fun succeeded(at: Long) {
            if (failures > 0) recoveryAt = at
            failures = 0
            lastSuccessAt = at
        }

        fun retryMs(): Long = when (failures) { 1 -> 2_000L; 2 -> 4_000L; else -> 5_000L }

        fun record(): Map<String, Any?> {
            fun instant(at: Long?) = at?.let { Instant.ofEpochMilli(it).toString() }
            return mapOf("last_success_at" to instant(lastSuccessAt),
                "failure_request_started_at" to instant(failureStartedAt),
                "failure_at" to instant(failureAt), "last_failure_at" to instant(lastFailureAt),
                "recovery_at" to instant(recoveryAt), "failures" to failures)
        }

        fun restore(record: Map<*, *>) {
            fun time(key: String) = (record[key] as? String)?.let { Instant.parse(it).toEpochMilli() }
            lastSuccessAt = time("last_success_at")
            failureStartedAt = time("failure_request_started_at")
            failureAt = time("failure_at")
            lastFailureAt = time("last_failure_at")
            recoveryAt = time("recovery_at")
            failures = (record["failures"] as? Number)?.toInt() ?: 0
        }
    }

    private val observations = mapOf("log" to Observation(), "status" to Observation())
    private fun instant() = Instant.ofEpochMilli(now()).toString()

    private val progressFile = File(directory, "progress.json")
    private var cursor: LogCursor? = null
    private var previousStatus: Map<*, *>? = null
    private var lastStatusAt: String? = null
    private var samples = 0L
    private var logBytes = 0L
    private var missingLogBytes = 0L
    private var lastLoopAt = 0L
    private var phase = "powered"
    private var deadline = 0L
    private var phaseDue = 0L
    private var phaseReminderSent = false

    private fun append(name: String, value: Map<String, Any?>) {
        File(directory, name).appendText(Json.write(mapOf("at" to instant(), "phase" to phase) + value) + "\n")
    }

    private fun event(kind: String, detail: Any? = null) {
        append("events.jsonl", mapOf("event" to kind, "detail" to detail))
        println("${instant()} $kind${if (detail == null) "" else ": $detail"}")
    }

    private fun atomic(file: File, value: Any?) {
        val temporary = File(file.parentFile, file.name + ".tmp")
        temporary.writeText(Json.pretty(value) + "\n")
        Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
    }

    private fun json(file: File): Map<*, *> = Json.parse(file.readText()) as Map<*, *>

    private fun save(state: String = "observing") {
        atomic(progressFile, mapOf("state" to state, "updated_at" to instant(),
            "host" to client.host, "image_id" to image, "deadline" to Instant.ofEpochMilli(deadline).toString(),
            "phase" to phase, "samples" to samples, "log_bytes" to logBytes,
            "missing_log_bytes" to missingLogBytes, "last_loop_ms" to lastLoopAt,
            "last_status_at" to lastStatusAt, "last_status" to previousStatus,
            "log_boot" to cursor?.boot, "log_end" to cursor?.end,
            "observations" to observations.mapValues { it.value.record() }))
    }

    private fun request(path: String): DeviceClient.Answer {
        val answer = client.raw(path, timeoutMs = REQUEST_MS)
        if (answer.status !in 200..299) throw IOException("HTTP ${answer.status} from $path")
        return answer
    }

    private fun status() {
        val answer = request("/api/status")
        val current = Json.parse(answer.body.toString(Charsets.UTF_8)) as? Map<*, *>
            ?: throw IOException("invalid status")
        require(current["uptime_s"] is Number) { "status has no uptime" }
        append("status.jsonl", mapOf("status" to current))
        statusEvents(previousStatus, current, image).forEach { event(it, current) }
        previousStatus = current
        lastStatusAt = instant()
        samples++
    }

    private fun log() {
        var answer = request("/api/log" + (cursor?.let { "?since=${it.end}" } ?: ""))
        fun header(name: String) = answer.headers.entries.firstOrNull { it.key.equals(name, true) }?.value
        val boot = header("X-Log-Boot") ?: throw IOException("log has no boot identifier")
        if (cursor != null && cursor!!.boot != boot) {
            event("device_restarted", mapOf("before" to cursor!!.boot, "after" to boot))
            answer = request("/api/log")
        }
        val next = LogCursor(header("X-Log-Boot") ?: throw IOException("log has no boot identifier"),
            header("X-Log-End")?.toLongOrNull() ?: throw IOException("log has no cursor"))
        val missing = missingBytes(cursor, next, answer.body.size)
        if (missing > 0) {
            missingLogBytes += missing
            event("log_gap", mapOf("missing_bytes" to missing, "boot" to next.boot))
        }
        if (answer.body.isNotEmpty()) {
            append("log.jsonl", mapOf("boot" to next.boot, "end" to next.end,
                "text" to answer.body.toString(Charsets.UTF_8)))
            logBytes += answer.body.size
        }
        cursor = next
    }

    private fun observe(name: String, action: () -> Unit): Boolean {
        val observation = observations.getValue(name)
        val started = now()
        return try {
            action()
            val recovered = now()
            if (observation.failures > 0) {
                observation.recoveryAt = recovered
                event("observation_restored", observation.record() + mapOf("source" to name,
                    "unavailable_ms" to (recovered - observation.failureAt!!),
                    "outage_upper_bound_ms" to observation.lastSuccessAt?.let { recovered - it }))
            }
            observation.succeeded(recovered)
            true
        } catch (error: Exception) {
            if (error is InterruptedException) throw error
            val first = observation.failures == 0
            observation.failed(started, now())
            if (first) event("observation_unavailable", observation.record() +
                mapOf("source" to name, "error" to error.toString()))
            false
        }
    }

    /** The existing run's deadline is retained on resume; gaps are evidence, not covered time. */
    fun run(hours: Double) {
        require(hours.isFinite() && hours > 0 && hours <= 720) { "hours must be between 0 and 720" }
        require(image.matches(Regex("[0-9a-f]{64}"))) { "expected image must be a 64-digit lowercase hash" }
        directory.mkdirs()
        java.io.RandomAccessFile(File(directory, "observer.lock"), "rw").use { lockFile ->
            val lock = lockFile.channel.tryLock() ?: error("another observer owns this directory")
            lock.use {
                val runFile = File(directory, "run.json")
                if (runFile.exists()) {
                    val run = json(runFile)
                    require(run["image_id"] == image && run["host"] == client.host) { "run identity does not match" }
                    deadline = Instant.parse(run["deadline"] as String).toEpochMilli()
                    phaseDue = Instant.parse(run["started_at"] as String).toEpochMilli() + 86_400_000L
                    if (progressFile.exists()) {
                        val progress = json(progressFile)
                        samples = (progress["samples"] as Number).toLong()
                        logBytes = (progress["log_bytes"] as Number).toLong()
                        missingLogBytes = (progress["missing_log_bytes"] as Number).toLong()
                        lastLoopAt = (progress["last_loop_ms"] as Number).toLong()
                        previousStatus = progress["last_status"] as? Map<*, *>
                        lastStatusAt = progress["last_status_at"] as? String
                        val boot = progress["log_boot"] as? String
                        val end = (progress["log_end"] as? Number)?.toLong()
                        if (boot != null && end != null) cursor = LogCursor(boot, end)
                        val records = progress["observations"] as? Map<*, *>
                        observations.forEach { (name, observation) ->
                            (records?.get(name) as? Map<*, *>)?.let { observation.restore(it) }
                        }
                    }
                    event("observer_resumed")
                } else {
                    val status = Json.parse(request("/api/status").body.toString(Charsets.UTF_8)) as Map<*, *>
                    require(status["image_id"] == image && status["mode"] == "application" && status["on_probation"] == false) {
                        "the expected firmware must be running with completed probation"
                    }
                    require(status["dev_tools"] != true) { "use the published release image" }
                    deadline = now() + (hours * 3_600_000).toLong()
                    phaseDue = now() + 86_400_000L
                    atomic(runFile, mapOf("host" to client.host, "image_id" to image, "hours" to hours,
                        "started_at" to instant(), "deadline" to Instant.ofEpochMilli(deadline).toString(),
                        "status_interval_s" to STATUS_MS / 1000, "log_interval_s" to LOG_MS / 1000,
                        "baseline" to status))
                    previousStatus = status
                    observations.getValue("status").succeeded(now())
                    event("observer_started")
                }
                var nextStatus = 0L
                var nextLog = 0L
                try {
                    while (now() < deadline) {
                        val loopAt = now()
                        if (lastLoopAt != 0L && loopAt - lastLoopAt > 90_000L)
                            event("observer_gap", mapOf("from_ms" to lastLoopAt, "to_ms" to loopAt))
                        lastLoopAt = loopAt
                        val phaseFile = File(directory, "phase.txt")
                        val selected = if (phaseFile.exists()) phaseFile.readText().trim() else "powered"
                        if (selected != phase) { phase = selected; event("phase_changed") }
                        if (phase == "powered" && loopAt >= phaseDue && !phaseReminderSent) {
                            event("phase_action_due", "24 hours reached: arrange the physical sleep test; observation alone does not change power mode.")
                            phaseReminderSent = true
                        }
                        val logObservation = observations.getValue("log")
                        val wasAway = logObservation.failures > 0
                        if (now() >= nextLog) {
                            val logOk = observe("log") { log() }
                            nextLog = now() + if (!logOk) logObservation.retryMs()
                                else if (phase == "sleep") SLEEP_MS else LOG_MS
                            if (wasAway && logOk) nextStatus = 0L
                        }
                        if (now() >= nextStatus) {
                            val statusOk = observe("status") { status() }
                            nextStatus = now() + if (statusOk) STATUS_MS
                                else observations.getValue("status").retryMs()
                        }
                        save()
                        val remaining = minOf(nextLog, nextStatus, deadline) - now()
                        if (remaining > 0) sleep(remaining)
                    }
                    event("observation_window_ended", "Manual checks and recorded gaps still require review; this is not a pass.")
                    save("awaiting_review")
                } finally {
                    save(if (now() >= deadline) "awaiting_review" else "interrupted")
                }
            }
        }
    }
}
