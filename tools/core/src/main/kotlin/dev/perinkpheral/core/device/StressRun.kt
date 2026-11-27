// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import dev.perinkpheral.core.Json
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import java.net.URI
import java.net.URLEncoder
import java.nio.file.Files
import java.time.Instant
import java.util.Collections
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Manual, bounded ramps. The only remote writes are bench packages, a bench glyph pack,
 * clock settings and restarts, and navigation without OK. */
class StressRun(
    private val client: DeviceClient,
    private val directory: File,
    private val options: Options = Options(),
    private val build: (String, File) -> Unit = { _, _ -> error("bench builder is required") },
    private val glyphs: () -> ByteArray = { error("Latin glyph pack generator is required") },
    private val say: (String) -> Unit = { println(it) },
    internal val timing: Timing = Timing(),
) {
    data class Options(val bench: String? = null, val axes: List<Char> = ('a'..'g').toList(),
                       val serial: String? = null, val quick: Boolean = false)
    /** Tests use the same ramps with a monotonic virtual clock and zero-duration holds. */
    class Timing(val scale: Double = 1.0, val now: () -> Long = { System.nanoTime() / 1_000_000 },
                 val sleep: (Long) -> Unit = { Thread.sleep(it) }, val recoveryMs: Long = 180_000,
                 val cleanupMs: Long = 300_000, val installMs: Long = 180_000, val absentMs: Long = 5000)

    private class AxisFailed : Exception()
    private data class Axis(var reached: String = "none", var outcome: String = "not run", var first: String = "",
                            val loads: MutableMap<String, String> = linkedMapOf(), val coverage: MutableList<String> = mutableListOf())
    private val results = linkedMapOf<Char, Axis>()
    private val cancelled = AtomicBoolean()
    private val done = CountDownLatch(1)
    private val sockets = ConcurrentHashMap.newKeySet<Socket>()
    private val owned = linkedSetOf<String>()
    private val packages = mutableMapOf<String, File>()
    private val cleanupErrors = mutableListOf<String>()
    private var settings: Map<*, *>? = null
    private var originalApps = emptyList<Map<*, *>>()
    private var protectedNames = emptySet<String>()
    private var timeChanged = false
    private var glyphOwned = false
    private var glyphName = ""
    private var server: StressServer? = null
    private var serial: StressSerial? = null
    private var temporary: File? = null
    private var catalog: List<Map<*, *>>? = null
    private var catalogChecked = false
    private var installEntry: Map<*, *>? = null
    private var installChecked = false
    private var glyphBytes: ByteArray? = null
    private var benchUrl = ""
    @Volatile private var changedApps = false
    private var owner: Thread? = null
    @Volatile private var exitCode = 1
    private var previous: Map<*, *>? = null
    private var previousAt = 0L
    private var sampledAt = 0L
    private var serialOffset = 0L
    private var serialTail = ""
    private var observedConsole = emptyList<String>()
    private var restartRequested = false
    private var backDown = false
    private var removedListFull = false
    private var retainedStores = false
    private var recordingFailed = false
    private var originalGlyphs: Map<String, Long>? = null
    private var cleanupDeadline: Long? = null
    private var cleanupWallDeadline = 0L
    private val cleanupRequests = Executors.newVirtualThreadPerTaskExecutor()

    init {
        require(options.axes.isNotEmpty() && options.axes.all { it in 'a'..'g' } && options.axes.distinct() == options.axes) { "axes must be distinct letters a,b,c,d,e,f,g" }
    }

    /** Returns a process exit code. Ctrl-C waits for the same cleanup as ordinary completion. */
    fun run(): Int {
        require(!directory.exists() || directory.listFiles().orEmpty().isEmpty()) { "--out must be an empty directory" }
        require(directory.isDirectory || directory.mkdirs()) { "cannot create ${directory.path}" }
        File(directory, "steps.jsonl").writeText("")
        options.axes.forEach { results[it] = Axis() }
        owner = Thread.currentThread()
        val hook = Thread({
            cancelled.set(true)
            owner?.interrupt()
            sockets.toList().forEach { runCatching { it.close() } }
            if (!done.await((timing.cleanupMs + 15_000).coerceAtLeast(1000), TimeUnit.MILLISECONDS)) {
                System.err.println("stress-run cleanup deadline expired")
            }
            Runtime.getRuntime().halt(exitCode)
        }, "stress cleanup")
        Runtime.getRuntime().addShutdownHook(hook)
        try {
            serial = options.serial?.let { StressSerial(it, File(directory, "serial.log")) }
            val initial = status()
            require(initial["mode"] != "recovery") { "stress-run requires main firmware" }
            previous = initial
            previousAt = sampledAt
            File(directory, "initial-status.json").writeText(Json.pretty(initial) + "\n")
            val screen = successful(request("/api/screen"))
            File(directory, "initial-screen.bmp").writeBytes(screen.body)
            settings = json("/api/settings") as? Map<*, *> ?: error("invalid settings")
            require(settings!!["time_auto"] is Boolean) { "settings has no time_auto" }
            originalApps = apps()
            val removed = json("/api/apps/removed") as List<*>
            removedListFull = removed.size >= REMOVED_LISTED_MAX
            retainedStores = removed.isNotEmpty()
            protectedNames = originalApps.map { it["name"].toString() }.toSet() + removed.map { it.toString() }
            originalGlyphs = glyphInventory(json("/api/glyphs") as Map<*, *>)
            note(mapOf("event" to "start", "options" to mapOf("axes" to options.axes.map { it.toString() }, "quick" to options.quick),
                "host" to client.host, "status" to initial, "settings" to settings,
                "apps" to originalApps, "glyphs" to originalGlyphs, "screen_headers" to screen.headers))
            if (initial["dev_tools"] == true && !removedListFull && options.axes.any { it in "ef" || it in "cd" && !retainedStores }) {
                val address = options.bench ?: benchAddress()
                server = StressServer(address) { note(mapOf("event" to "hostile_hit") + it) }
                benchUrl = "http://$address:${server!!.port}"
                say("Bench server: $benchUrl")
            }
            for (axis in options.axes) {
                checkCancelled()
                if (axis in "cdef" && initial["dev_tools"] != true) {
                    skip(axis, "dev_tools is absent or false; requires /api/dev")
                    continue
                }
                if (axis in "cdef" && removedListFull) {
                    skip(axis, "retained-store list is full; unlisted names may be protected")
                    continue
                }
                if (axis in "cd" && retainedStores) {
                    skip(axis, "retained stores exist; application settings and KV writes may evict them")
                    continue
                }
                try {
                    results.getValue(axis).outcome = "passed"
                    when (axis) {
                        'a' -> web()
                        'b' -> memory()
                        'c' -> hostile()
                        'd' -> misbehaving()
                        'e' -> navigation()
                        'f' -> storage()
                        'g' -> clock()
                    }
                } catch (_: AxisFailed) {
                    // Each axis stops independently; the next axis starts after its cleanup.
                } catch (error: InterruptedException) { throw error
                } catch (error: Exception) {
                    fail(axis, "setup", error.message ?: error.javaClass.simpleName)
                } finally {
                    server?.clear()
                    cleanup(::restoreClock, ::releaseBack, { if (axis in "cdef") removeBench() }, { checkCleanupObservation() })
                }
            }
        } catch (_: InterruptedException) {
            cancelled.set(true)
            Thread.interrupted()
            note(mapOf("event" to "interrupted"))
        } catch (error: Exception) {
            cleanupErrors += "Run: ${error.message}"
            note(mapOf("event" to "error", "error" to error.toString()))
        } finally {
            Thread.interrupted()
            try {
                cleanup(::restoreClock, ::releaseBack, { server?.close() }, ::removeBench, ::removeGlyph,
                    ::verifyGlyphs, ::verifyOriginalApps, { checkCleanupObservation() })
            } catch (error: Exception) { cleanupErrors += "Cleanup: ${error.message}" }
            try {
                serial?.close()
                previous?.let { cleanupErrors += verdict(it, it, readConsole(final = true)).map { line -> "Final console: $line" } }
            } catch (error: Exception) { cleanupErrors += "Serial: ${error.message}" }
            temporary?.deleteRecursively()
            try { writeReport() } finally {
                cleanupRequests.shutdownNow()
                exitCode = if (cancelled.get() || cleanupErrors.isNotEmpty() || results.values.any { it.outcome == "failed" }) 1 else 0
                done.countDown()
                try { Runtime.getRuntime().removeShutdownHook(hook) } catch (_: IllegalStateException) { }
            }
        }
        return exitCode
    }

    @Synchronized private fun note(value: Map<String, Any?>) {
        if (recordingFailed) return
        try { File(directory, "steps.jsonl").appendText(Json.write(mapOf("at" to Instant.now().toString()) + value) + "\n") }
        catch (error: Exception) {
            recordingFailed = true
            cleanupErrors += "Recording steps.jsonl: ${error.message}"
        }
    }
    private fun checkCancelled() { if (cancelled.get() || Thread.currentThread().isInterrupted) throw InterruptedException("cancelled") }
    private fun pause(ms: Long) { checkCancelled(); timing.sleep((ms * timing.scale).toLong()); checkCancelled() }
    private fun encode(value: String) = URLEncoder.encode(value, Charsets.UTF_8)
    private fun request(path: String, method: String = "GET", body: ByteArray? = null,
                        type: String = "application/json", timeout: Int = 10_000): DeviceClient.Answer {
        if (method == "POST" && (path.startsWith("/api/dev/run?") || path == "/api/restart")) restartRequested = true
        if (cleanupDeadline == null) return try { checkCancelled(); client.raw(path, method, timeout, body, type) }
        catch (error: IOException) { throw IOException("$method $path: ${error.message}", error) }
        val left = cleanupRemaining()
        val task = cleanupRequests.submit<DeviceClient.Answer> { client.raw(path, method, minOf(timeout.toLong(), left).toInt().coerceAtLeast(1), body, type) }
        return try { task.get(left, TimeUnit.MILLISECONDS) } finally { task.cancel(true) }
    }
    private fun successful(answer: DeviceClient.Answer): DeviceClient.Answer {
        if (answer.status !in 200..299) throw IOException("HTTP ${answer.status}: ${answer.body.toString(Charsets.UTF_8).take(160)}")
        return answer
    }
    private fun post(path: String, value: Any? = null) = successful(request(path, "POST", value?.let { Json.write(it).toByteArray() }))
    private fun json(path: String): Any? = Json.parse(successful(request(path)).body.toString(Charsets.UTF_8))
    private fun status(): Map<*, *> {
        val sample = json("/api/status") as? Map<*, *> ?: throw IOException("invalid status")
        sampledAt = timing.now()
        require(sample["uptime_s"] is Number && sample["failed_allocations"] is Number && sample["reset_reason"] is Number) { "status lacks uptime_s, failed_allocations or reset_reason" }
        val log = successful(request("/api/log"))
        val boot = log.headers.entries.firstOrNull { it.key.equals("X-Log-Boot", ignoreCase = true) }?.value
        require(!boot.isNullOrBlank()) { "log lacks X-Log-Boot" }
        return sample + ("log_boot" to boot)
    }
    private fun apps(): List<Map<*, *>> = (json("/api/apps") as List<*>).map { it as Map<*, *> }
    private fun waitBack(restartedFrom: String? = null): Map<*, *> {
        val deadline = timing.now() + timing.recoveryMs
        var error: Exception
        do {
            try {
                val sample = status()
                if (restartedFrom != null && sample["log_boot"] == restartedFrom)
                    throw IOException("requested restart did not change X-Log-Boot")
                successful(request("/", timeout = 3000))
                return sample
            } catch (failure: Exception) { error = failure }
            checkCancelled()
            timing.sleep(minOf(2000, (deadline - timing.now()).coerceAtLeast(0)))
        } while (timing.now() < deadline)
        throw IOException("page did not answer for ${timing.recoveryMs / 1000} s: ${error.message}")
    }
    private fun readConsole(final: Boolean = false): List<String> {
        val file = File(directory, "serial.log")
        if (!file.exists()) return emptyList()
        val text = RandomAccessFile(file, "r").use {
            it.seek(serialOffset)
            val bytes = ByteArray(minOf(it.length() - serialOffset, 4 * 1024 * 1024).toInt())
            it.readFully(bytes)
            serialOffset += bytes.size
            serialTail + String(bytes, Charsets.UTF_8)
        }
        val lines = text.split('\n')
        serialTail = if (final) "" else lines.last().takeLast(16_384)
        return if (final) lines else lines.dropLast(1)
    }
    private fun fail(axis: Char, conditions: String, reason: String) {
        val result = results.getValue(axis)
        if (result.outcome != "failed") result.first = "$conditions: $reason"
        result.outcome = "failed"
        note(mapOf("axis" to axis.toString(), "event" to "failure", "conditions" to conditions, "reason" to reason))
        say("${axis.uppercaseChar()} FAILED: $conditions: $reason")
    }
    private fun skip(axis: Char, reason: String) {
        results.getValue(axis).outcome = "skipped"
        results.getValue(axis).first = reason
        note(mapOf("axis" to axis.toString(), "event" to "skip", "reason" to reason))
        say("${axis.uppercaseChar()} skipped: $reason")
    }
    private fun coverage(axis: Char, reason: String) {
        results[axis]?.coverage?.add(reason)
        note(mapOf("axis" to axis.toString(), "event" to "coverage", "reason" to reason))
        say("${axis.uppercaseChar()}: $reason")
    }

    private fun step(axis: Char, conditions: String, restart: Boolean = false, action: () -> Any?) {
        checkCancelled()
        say("${axis.uppercaseChar()} $conditions")
        val before = waitBack()
        val gapFailures = observe(before)
        if (gapFailures.isNotEmpty()) {
            fail(axis, "before $conditions", gapFailures.joinToString("; "))
            throw AxisFailed()
        }
        restartRequested = false
        val started = timing.now()
        var detail: Any? = null
        var actionError: String? = null
        var interrupted = false
        try { detail = action() } catch (error: InterruptedException) { interrupted = true; Thread.interrupted()
        } catch (error: Exception) { actionError = error.message ?: error.toString() }
        val expectedRestart = restart && restartRequested
        val after = try { if (interrupted) status() else waitBack(if (expectedRestart) before["log_boot"] as? String else null) } catch (error: InterruptedException) { throw error
        } catch (error: Exception) { mapOf("error" to error.message) }
        val failures = observe(after, expectedRestart).toMutableList()
        val lines = observedConsole
        actionError?.let { failures += it }
        note(mapOf("axis" to axis.toString(), "conditions" to conditions, "before" to before,
            "after" to after, "serial" to lines.filter { CONSOLE_FAILURE.containsMatchIn(it) }.take(40),
            "warnings" to lines.filter { CONSOLE_WARNING.containsMatchIn(it) }.take(10),
            "requested_restart" to expectedRestart, "duration_ms" to timing.now() - started,
            "result" to detail, "failures" to failures))
        if (interrupted) {
            fail(axis, conditions, "interrupted")
            throw InterruptedException("cancelled")
        }
        if (failures.isNotEmpty()) { fail(axis, conditions, failures.joinToString("; ")); throw AxisFailed() }
        results.getValue(axis).reached = conditions
        results.getValue(axis).loads[conditions.substringBefore("=")] = conditions
    }

    private fun observe(sample: Map<*, *>, restart: Boolean = false): List<String> {
        val now = sampledAt
        val lines = readConsole()
        observedConsole = lines
        val failures = previous?.let { verdict(it, sample, lines, restart, now - previousAt) }.orEmpty()
        previous = sample
        previousAt = now
        restartRequested = false
        return failures
    }

    private fun checkCleanupObservation(restart: Boolean = false) {
        cleanupAttempt("check status and console after cleanup") {
            val failures = observe(status(), restart)
            if (failures.isNotEmpty()) {
                cleanupErrors += failures.map { "Cleanup observation: $it" }
                note(mapOf("event" to "cleanup_observation", "failures" to failures))
            }
        }
    }

    private fun parallel(count: Int, action: (Int) -> String): Map<String, Int> {
        val codes = ConcurrentHashMap<String, Int>()
        Executors.newVirtualThreadPerTaskExecutor().use { pool ->
            val tasks = (0 until count).map { index -> pool.submit {
                val result = try { action(index) } catch (error: InterruptedException) { throw error
                } catch (error: Exception) { error.javaClass.simpleName }
                codes.merge(result, 1, Int::plus)
            } }
            try { tasks.forEach { it.get() } } finally { tasks.forEach { it.cancel(true) }; pool.shutdownNow() }
        }
        return codes.toSortedMap()
    }

    private fun raw(bytes: ByteArray, delay: Long = 0, idle: Long = 0): String {
        val uri = URI("http://" + client.host.removePrefix("http://").trimEnd('/'))
        val socket = Socket()
        sockets += socket
        val closer = Executors.newSingleThreadScheduledExecutor { task -> Thread(task, "stress socket deadline").apply { isDaemon = true } }
        closer.schedule({ runCatching { socket.close() } }, maxOf(100, duration(if (options.quick) 15_000 else 65_000)), TimeUnit.MILLISECONDS)
        try {
            socket.connect(InetSocketAddress(uri.host, if (uri.port < 0) 80 else uri.port), 3000)
            socket.soTimeout = maxOf(100, duration(if (options.quick) 12_000 else 40_000).toInt())
            val output = socket.getOutputStream()
            if (delay > 0) for (byte in bytes) { output.write(byte.toInt()); output.flush(); pause(delay) }
            else output.write(bytes)
            if (idle > 0) pause(idle)
            val input = socket.getInputStream()
            val first = ByteArray(512)
            val count = input.read(first)
            // Drain with bounded storage. Hostile request answers may be large page frames.
            val line = if (count < 0) "closed" else String(first, 0, count, Charsets.ISO_8859_1).substringBefore("\r\n")
            while (input.read(first) >= 0) checkCancelled()
            return line.take(100)
        } finally { socket.close(); sockets -= socket; closer.shutdownNow() }
    }
    private fun get(path: String, extra: String = "") =
        "GET $path HTTP/1.1\r\nHost: ${client.host}\r\nConnection: close\r\n$extra\r\n".toByteArray()
    private fun ramp(axis: Char, name: String, levels: List<Int>, action: (Int) -> Any?) {
        for (level in levels) step(axis, "$name=$level") { action(level) }
    }
    private fun web() {
        val paths = listOf("/api/status", "/api/apps", "/api/log", "/", "/api/settings", "/api/screen", "/api/sources")
        ramp('a', "parallel GETs", if (options.quick) listOf(4, 12) else listOf(4, 8, 12, 16, 24, 32, 48, 64)) { n -> parallel(n) { raw(get(paths[it % paths.size])) } }
        ramp('a', "screens", if (options.quick) listOf(2) else listOf(1, 2, 4, 6, 8, 12)) { n -> parallel(n) { raw(get("/api/screen")) } }
        ramp('a', "slow sockets (500 ms/byte)", if (options.quick) listOf(4) else listOf(4, 8, 12, 16, 24)) { n -> parallel(n) { raw(get("/api/status"), delay = 500) } }
        ramp('a', "idle sockets", if (options.quick) listOf(6) else listOf(6, 12, 16, 24, 32)) { n -> parallel(n) { if (it == 0) { pause(3000); raw(get("/api/status")) } else raw(byteArrayOf(), idle = 10_000) } }
        for ((label, uri) in listOf("header" to false, "URI" to true)) {
            ramp('a', "oversized $label clients", if (options.quick) listOf(1) else listOf(1, 4, 8)) { n -> parallel(n) { index ->
                raw(if (uri) get("/api/status?" + "a".repeat(600 * (index + 1))) else get("/api/status", "X-A: ${"a".repeat(900 * (index + 1))}\r\n"))
            } }
        }
        // Invalid JSON and an absent application name cannot commit a setting or installation.
        for (path in listOf("/api/settings", "/api/apps/settings", "/api/clock", "/api/apps/install")) {
            for (declared in listOf(200_000, 1 shl 30)) ramp('a', "$path declared=$declared clients", if (options.quick) listOf(1) else if (declared == 200_000) listOf(1, 4, 8) else listOf(1, 4)) { n ->
                parallel(n) {
                    val body = "x".repeat(if (declared == 200_000) 200_000 else 100)
                    raw("POST $path HTTP/1.1\r\nHost: ${client.host}\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: $declared\r\n\r\n$body".toByteArray())
                }
            }
        }
    }

    private fun source(axis: Char = 'b'): List<Map<*, *>>? {
        if (!catalogChecked) {
            catalogChecked = true
            catalog = try {
                val answer = request("/api/catalog?source=0", timeout = 30_000)
                val parsed = Json.parse(successful(answer).body.toString(Charsets.UTF_8))
                ((if (parsed is Map<*, *>) parsed["apps"] else parsed) as List<*>).map { it as Map<*, *> }
            } catch (error: Exception) {
                coverage(axis, "source 0 unavailable; catalog install part skipped: ${error.message}")
                null
            }
        }
        return catalog
    }

    private fun own(name: String) {
        require(!removedListFull) { "retained-store list is full; unlisted names may be protected" }
        require(name !in protectedNames) { "$name is installed or has a retained store; refusing to replace it" }
        require(name in owned || (originalApps.map { it["name"].toString() }.toSet() + owned).size < INSTALLED_APPS_MAX) {
            "no remaining application slots"
        }
        owned += name
        File(directory, "cleanup.json").writeText(Json.pretty(mapOf("apps" to owned.toList(),
            "glyph" to if (glyphOwned) glyphName else null, "time_auto" to settings?.get("time_auto"))) + "\n")
    }

    private fun installer(): Map<*, *>? {
        if (installChecked) return installEntry
        installChecked = true
        if (retainedStores) {
            coverage('b', if (removedListFull) "install part skipped: retained-store list is full; unlisted names may be protected"
                else "install part skipped: retained stores exist; application handlers may evict them")
            source()
            return null
        }
        installEntry = source()?.filter { entry ->
            val name = entry["name"] as? String ?: return@filter false
            val files = entry["files"] as? List<*> ?: return@filter false
            name !in protectedNames && name !in BENCH_NAMES && !name.startsWith("sbig") &&
                entry["system"] != true && (entry["authorities"] as? Number)?.toInt().let { it == null || it == 0 } &&
                files.size == 2 && files.all { it is Map<*, *> && it["name"] in listOf("app.wasm", "manifest.json") }
        }?.minByOrNull { entry -> (entry["files"] as List<*>).sumOf { ((it as Map<*, *>)["size"] as Number).toLong() } }
        if (installEntry == null) coverage('b', "install part skipped: no unused two-file application in source 0")
        installEntry?.let { own(it["name"].toString()) }
        return installEntry
    }

    private fun prepareGlyph(): ByteArray? {
        if (glyphBytes != null) return glyphBytes
        val state = json("/api/glyphs") as Map<*, *>
        val names = (state["packs"] as List<*>).map { (it as Map<*, *>)["name"].toString() }.toSet()
        glyphName = "stressbench"
        if (glyphName in names) { coverage('b', "glyph part skipped: stressbench is already installed"); return null }
        // The current API omits face counts. Unknown packs may consume the entire table.
        val faces = (state["packs"] as List<*>).sumOf { ((it as Map<*, *>)["faces"] as? Number)?.toInt() ?: GLYPH_FACES_MAX }
        val bytes = glyphs().copyOf()
        val benchFaces = bytes.getOrNull(6)?.toInt()?.and(255) ?: GLYPH_FACES_MAX
        require(benchFaces in 1..GLYPH_FACES_MAX) { "invalid generated glyph face count" }
        if (faces + benchFaces > GLYPH_FACES_MAX) {
            coverage('b', "glyph part skipped: face table is full or its headroom is unavailable")
            return null
        }
        require(bytes.size in 24..32768) { "invalid generated Latin glyph pack size" }
        bytes.fill(0, 12, 24)
        glyphName.toByteArray().copyInto(bytes, 12)
        val space = (state["free"] as? Number)?.toLong() ?: 0
        if (space < ((bytes.size + 4095) / 4096 + 1) * 4096L) {
            coverage('b', "glyph part skipped: insufficient spare space for a separate bench pack")
            return null
        }
        glyphOwned = true
        File(directory, "cleanup.json").writeText(Json.pretty(mapOf("apps" to owned.toList(), "glyph" to glyphName,
            "time_auto" to settings?.get("time_auto"))) + "\n")
        glyphBytes = bytes
        return bytes
    }

    private fun memory() {
        val entry = installer()
        val pack = prepareGlyph()
        val levels = if (options.quick) listOf(listOf(1, 1, 1, 2), listOf(2, 2, 2, 4)) else listOf(
            listOf(1, 0, 0, 0), listOf(0, 1, 0, 0), listOf(0, 0, 1, 0), listOf(1, 1, 1, 0),
            listOf(2, 2, 2, 2), listOf(4, 4, 4, 8), listOf(8, 8, 8, 16))
        for (level in levels) step('b', "workers catalog/install/glyph/page=${level.joinToString("/")}") {
            val kinds = listOf("catalog", "install", "glyph", "page")
            val work = kinds.flatMapIndexed { index, kind -> List(level[index]) { kind } }
                .filter { (it != "install" || entry != null) && (it != "glyph" || pack != null) && (it != "catalog" || catalog != null) }
            val deadline = timing.now() + duration(if (options.quick) 5000 else 150_000)
            val fatal = ConcurrentHashMap.newKeySet<String>()
            val outcomes = parallel(work.size) { index ->
                val codes = mutableMapOf<Int, Int>()
                do {
                    checkCancelled()
                    val answer = when (work[index]) {
                        "catalog" -> request("/api/catalog?source=0", timeout = 120_000)
                        "install" -> {
                            changedApps = true
                            request("/api/apps/install?source=0&app=${encode(entry!!["name"].toString())}", "POST",
                                Json.write(mapOf("version" to entry["version"], "files" to entry["files"], "approved_authorities" to 0)).toByteArray(), timeout = 120_000)
                        }
                        "glyph" -> request("/api/glyphs", "POST", pack, "application/octet-stream", 120_000)
                        else -> request("/api/apps")
                    }
                    codes.merge(answer.status, 1, Int::plus)
                    if (answer.status >= 500 && answer.status != 503 && !(work[index] == "catalog" && answer.status == 502))
                        fatal += "${work[index]} HTTP ${answer.status}"
                    pause(200)
                } while (timing.now() < deadline)
                "${work[index]}:$codes"
            }
            if (fatal.isNotEmpty()) throw IOException(fatal.joinToString("; "))
            outcomes
        }
        coverage('b', "Existing application polls continue unchanged; this axis does not create TLS endpoints")
    }

    private fun duration(ms: Long): Long = (ms * timing.scale).toLong()
    private fun packed(kind: String): File = packages.getOrPut(kind) {
        val root = temporary ?: Files.createTempDirectory("perinkpheral-stress").toFile().also { temporary = it }
        File(root, kind).also { build(kind, it) }
    }
    private fun upload(name: String, kind: String) {
        own(name)
        val packed = packed(kind)
        changedApps = true
        val manifest = Json.parse(File(packed, "manifest.json").readText()) as Map<*, *>
        // A manifest is written first so cleanup can make even a partially uploaded package listable.
        successful(request("/api/dev/app?name=$name&file=manifest.json", "POST",
            Json.write(manifest + mapOf("name" to name)).toByteArray(), "application/octet-stream", 120_000))
        successful(request("/api/dev/app?name=$name&file=app.wasm", "POST",
            File(packed, "app.wasm").readBytes(), "application/octet-stream", 120_000))
        changedApps = true
    }
    private fun restartInto(name: String) {
        post("/api/dev/run?app=${encode(name)}")
        pause(3000)
        waitBack()
    }
    private fun prepare(axis: Char, kind: String) {
        if (kind in owned) return
        packed(kind)
        step(axis, "install $kind", restart = true) { upload(kind, kind); restartInto(kind); "installed" }
    }
    private fun configure(name: String, mode: String = "idle", url: String = benchUrl) {
        require(!retainedStores) { "retained stores exist; refusing application settings writes" }
        post("/api/apps/settings?app=${encode(name)}", mapOf("server" to url, "mode" to mode))
    }
    private fun open(name: String) { post("/api/dev/open?app=${encode(name)}") }
    private fun press(button: Int, kind: String = "short") {
        require(button in listOf(1, 3, 4)) { "stress-run never injects OK" }
        post("/api/dev/button?button=$button&press=$kind")
    }
    private fun served(path: String, case: String, since: Int, timeout: Long) {
        val deadline = timing.now() + duration(timeout)
        do {
            if (server!!.hits.drop(since).any { it == path to case }) return
            pause(100)
        } while (timing.now() < deadline)
        throw IOException("bench received no $path/$case request")
    }
    private fun hostile() {
        val cases = if (options.quick) listOf("pick" to "big300", "pick" to "nest65", "pick" to "liar",
            "doc" to "doc13k", "doc" to "endless", "xml" to "xml65", "raw" to "stream5m", "raw" to "endless") else StressServer.POLL_CASES
        for ((path, case) in cases) {
            val kind = if (path in listOf("doc", "xml")) "stressdoc" else "stress"
            prepare('c', kind)
            step('c', "$path/$case") {
                for (other in owned.filter { it in BENCH_NAMES && it != kind }) configure(other, url = "")
                server!!.clear()
                server!!.select(path, case)
                val mark = server!!.hits.size
                try {
                    configure(kind)
                    open(kind)
                    served(path, case, mark, if (options.quick) 20_000 else 90_000)
                    val hold = if (options.quick) 1500L else when (case) {
                        "trickle", "stall" -> 70_000L; "endless" -> 90_000L
                        "stream4m", "stream5m" -> 120_000L; else -> 30_000L
                    }
                    pause(hold)
                    mapOf("served" to server!!.hits.drop(mark).count { it == path to case })
                } finally { server!!.clear() }
            }
        }
        if (!options.quick) releaseChecks()
        else coverage('c', "quick omits sustained holds and the two connection-release timing checks")
    }
    private fun releaseChecks() {
        prepare('c', "stressdoc")
        prepare('c', "stress")
        for (path in listOf("doc", "raw")) step('c', "$path endless connection release") {
            val kind = if (path == "doc") "stressdoc" else "stress"
            owned.filter { it in BENCH_NAMES && it != kind }.forEach { configure(it, url = "") }
            server!!.clear()
            val mark = server!!.hits.size
            server!!.select(path, "endless")
            configure(kind)
            open(kind)
            served(path, "endless", mark, 90_000)
            server!!.select(path, "ok")
            if (path == "raw") { pause(8000); press(4, "long") }
            val releasedAt = timing.now()
            val next = server!!.hits.size
            served(path, "ok", next, CONNECTION_RELEASE_MS)
            val gap = timing.now() - releasedAt
            server!!.clear()
            mapOf("next_request_after_ms" to gap)
        }
    }
    private fun misbehaving() {
        val modes = listOf(Triple("stress", "http", "ok"), Triple("stress", "http", "stall")) +
            listOf("files", "kv", "timers", "redraw", "recurse", "trap", "loop", "dataloop").map { Triple("stress", it, "ok") } +
            listOf("arena", "loop").map { Triple("stressfat", it, "ok") }
        for ((kind, mode, answer) in modes) {
            prepare('d', kind)
            step('d', "$kind mode=$mode q=$answer") {
                owned.filter { it in BENCH_NAMES && it != kind }.forEach { configure(it, url = "") }
                server!!.clear(); server!!.select("q", answer)
                try {
                    configure(kind, mode)
                    open(kind)
                    // Settings and focus exercise the handler; no OK can reach a crash screen or menu.
                    if (mode == "dataloop") press(4, "long")
                    pause(if (options.quick) 1500 else 60_000)
                    status()
                } finally { server!!.clear(); configure(kind, "idle"); press(4, "long") }
            }
        }
    }
    private fun navigation() {
        prepare('e', "stress")
        val names = (originalApps.filter { it["system"] != true }.map { it["name"].toString() } + "stress").distinct()
        var index = 0
        val random = java.util.Random(7)
        fun chord() {
            backDown = true
            try { press(4, "down"); pause(50); press(if (random.nextBoolean()) 1 else 3); pause(50) }
            finally { post("/api/dev/button?button=4&press=up"); backDown = false }
        }
        for (mode in listOf("open", "updown", "held", "mixed")) {
            val gaps = if (options.quick) listOf(200L, 0L) else listOf(2000L, 1000L, 500L, 200L, 50L, 0L)
            for (gap in gaps) step('e', "$mode gap=${gap}ms") {
                open("stress")
                val deadline = timing.now() + duration(if (options.quick) 1500 else 120_000)
                var count = 0
                do {
                    checkCancelled()
                    val action = if (mode == "mixed") listOf("open", "held", "updown")[random.nextInt(3)] else mode
                    when (action) {
                        "open" -> open(names[index++ % names.size])
                        "held" -> chord()
                        else -> { open("stress"); press(if (random.nextBoolean()) 1 else 3) }
                    }
                    count++; pause(gap)
                } while (timing.now() < deadline)
                mapOf("actions" to count)
            }
        }
    }
    private fun storage() {
        packed("stress")
        packed("stressbig")
        var refused = false
        val slots = (INSTALLED_APPS_MAX - (apps().map { it["name"].toString() }.toSet() + owned).size).coerceAtLeast(0)
        val limit = minOf(if (options.quick) 3 else INSTALLED_APPS_MAX, slots)
        for (index in 1..limit) {
            val name = "sbig%02d".format(index)
            require(name !in protectedNames) { "$name is installed or has a retained store" }
            step('f', "42 KB module $index", restart = true) {
                try { upload(name, "stressbig") } catch (error: IOException) {
                    val sample = status()
                    val total = (sample["storage_total"] as? Number)?.toLong() ?: 0L
                    val used = (sample["storage_used"] as? Number)?.toLong() ?: -1L
                    val message = error.message.orEmpty()
                    val storageRefused = message == "HTTP 500: the file could not be stored" ||
                        message == "HTTP 400: not an application's name, or the storage refused"
                    if (!storageRefused || sample["storage_ok"] != true || used !in 0..total ||
                        total <= 0 || total - used >= File(packed("stressbig"), "app.wasm").length()) throw error
                    refused = true
                    note(mapOf("axis" to "f", "event" to "storage_refusal", "app" to name, "response" to error.message,
                        "storage_total" to total, "storage_used" to used))
                }
                if (!refused) { restartInto(name); open(name) }
                mapOf("refused" to refused, "status" to status())
            }
            if (refused) break
        }
        if (!refused) coverage('f', "${if (limit == slots) "application slot cap" else "quick cap"} reached without an upload refusal")
    }
    private fun clock() {
        val unix = Instant.now().epochSecond
        val jumps = if (options.quick) listOf(unix + 86400, unix - 86400, 4102444700L) else
            listOf(unix + 86400, unix - 86400, unix + 365 * 86400, 4102444700L, 1767225600L, unix + 5 * 3600, unix)
        timeChanged = true
        try {
            post("/api/settings", mapOf("time_auto" to false))
            for (jump in jumps) step('g', "clock unix=$jump") {
                post("/api/clock", mapOf("unix" to jump)); pause(if (options.quick) 500 else 20_000); "saved"
            }
        } finally { cleanup(::restoreClock) }
        repeat(if (options.quick) 2 else 8) { index ->
            step('g', "restart ${index + 1}", restart = true) { post("/api/restart"); pause(3000); waitBack() }
        }
        step('g', "two restarts", restart = true) {
            val codes = parallel(2) { request("/api/restart", "POST").status.toString() }
            pause(3000); waitBack(); codes
        }
        if (source('g') != null) step('g', "restart during catalog", restart = true) {
            parallel(2) { index ->
                if (index == 0) request("/api/catalog?source=0", timeout = 60_000).status.toString()
                else { pause(1500); post("/api/restart"); "restart requested" }
            }.also { pause(3000); waitBack() }
        } else coverage('g', "restart during catalog skipped: source 0 unavailable")
    }

    private fun cleanup(vararg actions: () -> Unit) {
        if (Thread.interrupted()) cancelled.set(true)
        if (cleanupDeadline == null) {
            cleanupDeadline = timing.now() + timing.cleanupMs
            cleanupWallDeadline = System.nanoTime() / 1_000_000 + timing.cleanupMs
        }
        try { for (action in actions) try { action() } catch (error: Exception) { cleanupErrors += "Cleanup: ${error.message}" } }
        finally { if (!cancelled.get()) cleanupDeadline = null }
    }
    private fun cleanupRemaining(): Long {
        val left = minOf((cleanupDeadline ?: Long.MAX_VALUE) - timing.now(),
            cleanupWallDeadline - System.nanoTime() / 1_000_000)
        if (left <= 0) throw IOException("cleanup deadline expired")
        return left
    }
    private fun cleanupSleep(ms: Long) {
        timing.sleep(if (cleanupDeadline == null) ms else minOf(ms, cleanupRemaining()))
    }
    private fun releaseBack() {
        if (backDown) cleanupAttempt("release BACK") {
            post("/api/dev/button?button=4&press=up")
            backDown = false
        }
    }
    private fun glyphInventory(state: Map<*, *>): Map<String, Long> =
        (state["packs"] as List<*>).associate {
            val pack = it as Map<*, *>
            pack["name"].toString() to (pack["bytes"] as Number).toLong()
        }

    private fun restoreClock() {
        if (!timeChanged) return
        cleanupAttempt("restore clock") {
            // Restore elapsed real time as well as the automatic-time preference.
            post("/api/settings", mapOf("time_auto" to false))
            try { post("/api/clock", mapOf("unix" to Instant.now().epochSecond)) }
            finally { post("/api/settings", mapOf("time_auto" to settings!!["time_auto"])) }
            timeChanged = false
        }
    }
    private fun cleanupAttempt(label: String, action: () -> Unit): Boolean {
        var last: Exception? = null
        repeat(3) {
            try { action(); note(mapOf("event" to "cleanup", "action" to label, "outcome" to "done")); return true }
            catch (error: Exception) {
                last = error
                Thread.interrupted()
                if (cleanupDeadline != null && runCatching { cleanupRemaining() }.isFailure) return@repeat
                runCatching { cleanupSleep(1000) }
            }
        }
        cleanupErrors += "$label: ${last?.message}"
        note(mapOf("event" to "cleanup", "action" to label, "error" to last.toString()))
        return false
    }
    private fun removeBench() {
        if (owned.isEmpty()) return
        server?.clear()
        val listed = try { apps().map { it["name"].toString() }.toSet() } catch (_: Exception) { emptySet() }
        // Free complete packages before repairing an unlisted, interrupted upload.
        for (name in owned.toList().sortedBy { if (it in listed) 0 else 1 }) {
            val deadline = timing.now() + timing.installMs
            if (cleanupAttempt("remove $name with forget=1") {
                val catalogApp = name == installEntry?.get("name")
                val answer = removeWhenIdle(name, catalogApp, deadline)
                if (answer.status == 404 && changedApps && !catalogApp) {
                    // Development uploads do not reload the list. A valid small module makes
                    // a partial upload visible after restart so the public removal can erase it.
                    val packed = packed("stress")
                    val manifest = Json.parse(File(packed, "manifest.json").readText()) as Map<*, *>
                    successful(request("/api/dev/app?name=$name&file=manifest.json", "POST", Json.write(manifest + mapOf("name" to name)).toByteArray()))
                    successful(request("/api/dev/app?name=$name&file=app.wasm", "POST", File(packed, "app.wasm").readBytes(), "application/octet-stream"))
                    // Cleanup must proceed after cancellation; it does not use pause().
                    checkCleanupObservation()
                    post("/api/dev/run?app=$name"); cleanupSleep(duration(3000)); cleanupBack()
                    checkCleanupObservation(restart = true)
                    successful(removeWhenIdle(name, false, deadline))
                } else if (answer.status != 404) successful(answer)
            }) owned -= name
        }
    }
    private fun removeWhenIdle(name: String, catalogApp: Boolean, deadline: Long): DeviceClient.Answer {
        if (timing.now() >= deadline) throw IOException("$name removal deadline expired")
        if (catalogApp) {
            // An absent name makes remove return 404 even while its install holds the writer.
            // An invalid install cannot write; its route reports 503 while the waiting slot is held.
            do {
                val probe = request("/api/apps/install", "POST")
                if (probe.status != 503) {
                    require(probe.status == 400) { "install-idle probe returned HTTP ${probe.status}" }
                    break
                }
                cleanupSleep(1000)
                if (timing.now() >= deadline) throw IOException("install still busy after ${timing.installMs / 1000} s")
            } while (true)
        }
        var absentSince: Long? = null
        var last: DeviceClient.Answer? = null
        do {
            val answer = request("/api/apps/remove?app=${encode(name)}&forget=1", "POST")
            last = answer
            if (answer.status == 503) absentSince = null
            else {
                if (answer.status != 404) successful(answer)
                if (!catalogApp) return answer
                // asked_app can return 404 before an in-flight install commits its manifest.
                if (apps().any { it["name"] == name }) absentSince = null
                else {
                    if (absentSince == null) absentSince = timing.now()
                    if (timing.now() - absentSince >= timing.absentMs) return answer
                }
            }
            cleanupSleep(1000)
        } while (timing.now() < deadline)
        throw IOException("$name did not settle after removal (HTTP ${last.status})")
    }

    private fun cleanupBack() {
        val deadline = timing.now() + timing.recoveryMs
        do {
            try { status(); return } catch (_: Exception) { cleanupSleep(1000) }
        } while (timing.now() < deadline)
        throw IOException("device unavailable during cleanup")
    }
    private fun removeGlyph() {
        if (!glyphOwned) return
        cleanupAttempt("remove bench glyph pack") {
            val answer = request("/api/glyphs/remove?name=${encode(glyphName)}", "POST")
            if (answer.status != 404) successful(answer)
            glyphOwned = false
        }
    }
    private fun verifyGlyphs() {
        if (originalGlyphs == null) return
        cleanupAttempt("verify original glyph packs") {
            val current = glyphInventory(json("/api/glyphs") as Map<*, *>).filterKeys { !glyphOwned || it != glyphName }
            require(current == originalGlyphs) { "installed glyph packs changed" }
        }
    }
    private fun verifyOriginalApps() {
        if (settings == null) return
        cleanupAttempt("verify original applications and settings") {
            val current = apps()
            val expected = originalApps.associateBy { it["name"] }
            require(current.map { it["name"] }.toSet() == expected.keys) { "installed application list changed" }
            for (app in current) {
                val before = expected.getValue(app["name"])
                require(app["version"] == before["version"] && app["values"] == before["values"]) { "${app["name"]}: version or settings changed" }
            }
            val after = json("/api/settings") as Map<*, *>
            require(after == settings) { "device settings changed" }
        }
    }
    private fun writeReport() {
        val text = buildString {
            append("# Stress run\n\nHost: `${client.host}`. Mode: ${if (options.quick) "quick" else "full"}.\n\n")
            for ((axis, result) in results) {
                append("## Axis ${axis.uppercaseChar()}\n\n| Highest load reached | Outcome | First failure or skip |\n|---|---|---|\n")
                fun cell(value: String) = value.replace("|", "\\|").replace('\n', ' ')
                append("| ${cell(result.loads.values.joinToString("; ").ifEmpty { result.reached })} | ${result.outcome} | ${cell(result.first)} |\n\n")
                result.coverage.forEach { append("${cell(it)}\n\n") }
            }
            if (cancelled.get()) append("Interrupted.\n\n")
            if (cleanupErrors.isNotEmpty()) append("## Cleanup and run errors\n\n" + cleanupErrors.joinToString("\n") { "- $it" } + "\n\n")
            append("Steps and HTTP outcomes: `steps.jsonl`. Cleanup inventory: `cleanup.json`.\n")
            append(if (options.serial == null) "USB console was not observed.\n" else "${serial?.diagnostic ?: "console not captured"}\n")
            append("Initial status: `initial-status.json`. Initial panel frame: `initial-screen.bmp`.\n")
            if (changedApps)
                append("The API exposes neither the open nor the remembered application. The device will start into the menu or the default application. The owner should open their application once.\n")
            append("This run does not qualify endurance, power loss, physical buttons, panel timing or TLS with two roots.\n")
        }
        File(directory, "report.md").writeText(text)
    }
    private fun benchAddress(): String {
        val candidates = Collections.list(NetworkInterface.getNetworkInterfaces()).filter { it.isUp && !it.isLoopback }
            .flatMap { Collections.list(it.inetAddresses) }.filter { it is java.net.Inet4Address && it.isSiteLocalAddress }.distinct()
        require(candidates.size == 1) { "give --bench with this computer's LAN address; cannot select one unambiguously" }
        return candidates.single().hostAddress
    }

    companion object {
        private const val REMOVED_LISTED_MAX = 16
        private const val GLYPH_FACES_MAX = 12
        private const val INSTALLED_APPS_MAX = 24 // LAUNCHER_INSTALLED_MAX
        private const val CONNECTION_RELEASE_MS = 120_000L // Below the streamed response's 300 s timeout.
        private const val UPTIME_SLACK_S = 2L
        private val BENCH_NAMES = setOf("stress", "stressfat", "stressdoc", "stressbig")
        /** A reset, a panic or an abort fails a step. The task watchdog only prints in both builds
         * (CONFIG_ESP_TASK_WDT_PANIC is off): it is recorded as a warning, not a failure. */
        private val CONSOLE_FAILURE = Regex("Guru Meditation|panic'ed|abort\\(\\)|\\brst:0x|Stack canary|Interrupt wdt|\\bWDT\\b reset", RegexOption.IGNORE_CASE)
        private val CONSOLE_WARNING = Regex("task_wdt: Task watchdog got triggered", RegexOption.IGNORE_CASE)
        /** Expected software restart banners are allowed only in explicitly marked steps. */
        fun verdict(before: Map<*, *>, after: Map<*, *>, console: List<String>, requestedRestart: Boolean = false, elapsedMs: Long = 0): List<String> {
            val failures = mutableListOf<String>()
            fun number(sample: Map<*, *>, key: String) = (sample[key] as? Number)?.toLong() ?: 0L
            if (after["error"] != null) failures += after["error"].toString()
            val reset = number(after, "reset_reason")
            val oldBoot = before["log_boot"] as? String
            val newBoot = after["log_boot"] as? String
            if (requestedRestart && (oldBoot.isNullOrEmpty() || newBoot.isNullOrEmpty() || oldBoot == newBoot))
                failures += "requested restart did not change X-Log-Boot"
            if (!requestedRestart && oldBoot != null && newBoot != null && oldBoot != newBoot)
                failures += "X-Log-Boot changed: restart"
            if (requestedRestart && reset != 3L) failures += "requested restart has reset_reason $reset, expected 3"
            if (reset != number(before, "reset_reason")) {
                if (!requestedRestart || reset in 4L..7L || reset == 9L) failures += "reset_reason changed to $reset: restart"
            }
            if (!requestedRestart && (number(after, "uptime_s") < number(before, "uptime_s") ||
                    number(after, "uptime_s") < number(before, "uptime_s") + elapsedMs / 1000 - UPTIME_SLACK_S))
                failures += "uptime fell behind elapsed time: restart"
            if (number(after, "failed_allocations") > number(before, "failed_allocations")) failures += "failed_allocations increased"
            fun panic(sample: Map<*, *>) = (sample["last_panic"] as? String).orEmpty()
            val oldPanic = panic(before)
            val newPanic = panic(after)
            val counter = Regex("^(\\d+)\\s+(\".*)$")
            val oldRecord = counter.matchEntire(oldPanic)
            val newRecord = counter.matchEntire(newPanic)
            if (newPanic.isNotEmpty() && ((newRecord?.groupValues?.get(2) ?: newPanic) !=
                    (oldRecord?.groupValues?.get(2) ?: oldPanic) ||
                    (requestedRestart && oldRecord != null && newRecord != null &&
                        newRecord.groupValues[1].toLong() <= oldRecord.groupValues[1].toLong())))
                failures += "last_panic changed or counter did not rise: $newPanic"
            console.filter { line -> CONSOLE_FAILURE.containsMatchIn(line) &&
                !(requestedRestart && Regex("\\brst:0x", RegexOption.IGNORE_CASE).containsMatchIn(line) && Regex("SW_CPU|SW_SYS|SW_RESET|software", RegexOption.IGNORE_CASE).containsMatchIn(line))
            }.take(40).forEach { failures += "console: $it" }
            return failures
        }
    }
}
