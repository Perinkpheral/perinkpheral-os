// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import com.sun.net.httpserver.HttpServer
import dev.perinkpheral.core.Json
import java.io.File
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.net.URLDecoder
import java.nio.file.Files
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

object StressRunTest {
    private val baseline = mapOf("uptime_s" to 500L, "failed_allocations" to 0L, "last_panic" to "")

    @JvmStatic fun main(args: Array<String>) {
        if (args.firstOrNull() == "child") {
            val code = StressRun(DeviceClient(args[1]), File(args[2]),
                StressRun.Options("127.0.0.1", listOf('d'), quick = true), ::build, { ByteArray(31964) }).run()
            kotlin.system.exitProcess(code)
        }
        verdicts()
        runAxes()
        releaseAndFailures()
        failedUpload()
        cancellation()
        signalCleanup()
        println("Stress runner checks passed")
    }

    private fun verdicts() {
        check(StressRun.verdict(baseline, baseline + ("memory_refusals" to 100), emptyList()).isEmpty())
        check(StressRun.verdict(baseline, baseline + ("uptime_s" to 1), emptyList()).any { "restart" in it })
        check(StressRun.verdict(baseline, baseline + ("failed_allocations" to 1), emptyList()).isNotEmpty())
        check(StressRun.verdict(baseline, baseline + ("last_panic" to "watchdog"), emptyList()).isNotEmpty())
        check(StressRun.verdict(baseline, baseline + ("last_panic" to "old"), emptyList()).isNotEmpty())
        check(StressRun.verdict(baseline + ("last_panic" to "old"), baseline + ("last_panic" to "old"), emptyList()).isEmpty())
        check(StressRun.verdict(baseline, mapOf("error" to "no answer for 180 s"), emptyList()).isNotEmpty())
        for (line in listOf("Guru Meditation Error: Core  0 panic'ed (Load access fault)", "abort() was called at PC",
                "rst:0xc (SW_CPU),boot:0x7f", "Stack canary watchpoint triggered", "Interrupt wdt timeout")) {
            check(StressRun.verdict(baseline, baseline, listOf(line)).isNotEmpty()) { line }
        }
        // The task watchdog only prints; a step with it does not fail.
        check(StressRun.verdict(baseline, baseline,
            listOf("E (617989) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:")).isEmpty())
        check(StressRun.verdict(baseline, baseline + ("uptime_s" to 1), listOf("rst:0xc (SW_CPU),boot:0x7f (SPI_FAST_FLASH_BOOT)"), true).isEmpty())
        check(StressRun.verdict(baseline, baseline, listOf("rst:0x7 (TG0WDT_SYS_RESET)"), true).isNotEmpty())
    }

    private fun build(kind: String, out: File) {
        out.mkdirs()
        val manifest = mapOf("name" to kind, "title" to kind, "version" to "0.1.0", "abi" to 3)
        File(out, "manifest.json").writeText(Json.write(manifest))
        File(out, "app.wasm").writeBytes(ByteArray(if (kind == "stressbig") 42_000 else 100) { 1 })
    }

    private fun runAxes() {
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-axes").toFile()
            try {
                val code = StressRun(DeviceClient(fake.host), out,
                    StressRun.Options("127.0.0.1", quick = true), ::build, { ByteArray(31964) }, {},
                    StressRun.Timing(scale = 0.01, recoveryMs = 1000)).run()
                check(code == 0) { out.resolve("report.md").readText() }
                fake.assertRestored()
                val steps = out.resolve("steps.jsonl").readLines().map { Json.parse(it) as Map<*, *> }
                for (axis in 'a'..'g') check(steps.any { it["axis"] == axis.toString() && it["before"] != null }) { "missing $axis" }
                check(steps.any { it["conditions"] == "parallel GETs=12" })
                check(steps.any { it["conditions"] == "stress mode=dataloop q=ok" })
                check(steps.any { it["event"] == "storage_refusal" })
                check(steps.any { it["conditions"] == "restart during catalog" })
                check(fake.calls.none { "button=2" in it || "/api/reset" in it || "/api/wifi" in it })
                check(fake.calls.any { "press=down" in it } && fake.calls.any { "press=up" in it })
                check(fake.calls.filter { "POST /api/apps/remove?" in it }.all { "forget=1" in it })
                check(fake.calls.none { "/api/apps/install" in it && "app=keeper" in it })
            } finally { out.deleteRecursively() }
        }
        // Full ramps use the production action paths with shortened durations.
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-full").toFile()
            try {
                val code = StressRun(DeviceClient(fake.host), out,
                    StressRun.Options("127.0.0.1", axes = listOf('a', 'b', 'd', 'e', 'f', 'g')),
                    ::build, { ByteArray(31964) }, {}, StressRun.Timing(scale = 0.0001, recoveryMs = 1000)).run()
                check(code == 0) { out.resolve("report.md").readText() }
                val steps = out.resolve("steps.jsonl").readText()
                check("parallel GETs=64" in steps && "workers catalog/install/glyph/page=8/8/8/16" in steps)
                check("mixed gap=0ms" in steps && "restart 8" in steps)
                fake.assertRestored()
            } finally { out.deleteRecursively() }
        }
    }

    private fun releaseAndFailures() {
        FakeDevice(dev = false, catalogAvailable = false).use { fake ->
            val out = Files.createTempDirectory("stress-release").toFile()
            try {
                val code = StressRun(DeviceClient(fake.host), out, StressRun.Options(quick = true), ::build,
                    { ByteArray(31964) }, {}, StressRun.Timing(scale = 0.0001, recoveryMs = 1000)).run()
                check(code == 0) { out.resolve("report.md").readText() }
                check(fake.calls.none { "/api/dev/" in it || "/api/apps/install?" in it })
                check(out.resolve("report.md").readText().split("requires /api/dev").size == 5)
                fake.assertRestored()
            } finally { out.deleteRecursively() }
        }
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-failure").toFile()
            fake.failAllocations = true
            try {
                val code = StressRun(DeviceClient(fake.host), out, StressRun.Options(axes = listOf('a'), quick = true),
                    say = {}, timing = StressRun.Timing(scale = 0.0001, recoveryMs = 1000)).run()
                check(code == 1)
                val text = out.resolve("steps.jsonl").readText()
                check("failed_allocations increased" in text)
                check("parallel GETs=12" !in text) { "axis continued after first failure" }
            } finally { out.deleteRecursively() }
        }
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-restore-false").toFile()
            fake.timeAuto = false
            try {
                check(StressRun(DeviceClient(fake.host), out, StressRun.Options(axes = listOf('g'), quick = true),
                    say = {}, timing = StressRun.Timing(scale = 0.0001, recoveryMs = 1000)).run() == 0)
                check(!fake.timeAuto)
            } finally { out.deleteRecursively() }
        }
    }
    private fun failedUpload() {
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-upload-failure").toFile()
            fake.failUpload = true
            try {
                check(StressRun(DeviceClient(fake.host), out, StressRun.Options("127.0.0.1", listOf('d'), quick = true),
                    ::build, say = {}, timing = StressRun.Timing(scale = 0.0001, recoveryMs = 1000)).run() == 1)
                fake.assertRestored()
                check(fake.calls.any { "/api/dev/run" in it }) { "unlisted partial package was not repaired" }
                check(fake.staged.isEmpty())
            } finally { out.deleteRecursively() }
        }
    }
    private fun cancellation() {
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-cancel").toFile()
            var sleeps = 0
            try {
                val code = StressRun(DeviceClient(fake.host), out, StressRun.Options(axes = listOf('g'), quick = true), say = {},
                    timing = StressRun.Timing(scale = 0.0001, sleep = { if (++sleeps == 1) throw InterruptedException() }, recoveryMs = 1000)).run()
                check(code == 1 && fake.timeAuto)
                check("Interrupted" in out.resolve("report.md").readText())
            } finally { out.deleteRecursively() }
        }
    }
    private fun signalCleanup() {
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-signal").toFile()
            val log = File(out.parentFile, out.name + ".log")
            val process = ProcessBuilder(File(System.getProperty("java.home"), "bin/java").path,
                "-cp", System.getProperty("java.class.path"), StressRunTest::class.java.name,
                "child", fake.host, out.path).redirectErrorStream(true).redirectOutput(log).start()
            try {
                val deadline = System.nanoTime() + 15_000_000_000L
                while (fake.opened.get() == 0 && process.isAlive && System.nanoTime() < deadline) Thread.sleep(20)
                check(fake.opened.get() > 0) { log.readText() }
                ProcessBuilder("kill", "-INT", process.pid().toString()).start().waitFor()
                check(process.waitFor(20, java.util.concurrent.TimeUnit.SECONDS)) { "SIGINT cleanup did not finish" }
                fake.assertRestored()
                check(out.resolve("report.md").isFile)
            } finally {
                process.destroyForcibly()
                out.deleteRecursively(); log.delete()
            }
        }
    }

    private class FakeDevice(private val dev: Boolean = true, private val catalogAvailable: Boolean = true) : AutoCloseable {
        val calls = CopyOnWriteArrayList<String>()
        val staged = ConcurrentHashMap<String, MutableMap<String, ByteArray>>()
        private val installed = ConcurrentHashMap<String, Map<String, Any?>>()
        private val values = ConcurrentHashMap<String, Map<String, String>>()
        private val workers = Executors.newVirtualThreadPerTaskExecutor()
        private val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        private val statusCalls = AtomicInteger()
        private val uptime = AtomicInteger(500)
        val opened = AtomicInteger()
        @Volatile var failAllocations = false
        @Volatile var failUpload = false
        @Volatile var timeAuto = true
        private var glyphInstalled = false
        private val keeper = mapOf("name" to "keeper", "version" to "1.0.0", "values" to mapOf("secret" to true, "city" to "", "url" to "http://saved"))
        val host get() = "127.0.0.1:${server.address.port}"

        init {
            installed["keeper"] = keeper
            server.executor = workers
            server.createContext("/") { exchange ->
                val uri = exchange.requestURI
                val path = uri.path
                calls += "${exchange.requestMethod} $uri"
                val query = uri.rawQuery.orEmpty().split('&').mapNotNull { part ->
                    val pair = part.split('=', limit = 2)
                    if (pair.size == 2) pair[0] to URLDecoder.decode(pair[1], Charsets.UTF_8) else null
                }.toMap()
                val bytes = exchange.requestBody.readBytes()
                val body = runCatching { Json.parse(bytes.toString(Charsets.UTF_8)) as? Map<*, *> }.getOrNull()
                var code = 200
                var response: Any? = mapOf("ok" to true)
                val name = query["app"] ?: query["name"].orEmpty()
                when {
                    path == "/api/status" -> {
                        val count = statusCalls.incrementAndGet()
                        response = baseline + mapOf("dev_tools" to dev, "uptime_s" to uptime.incrementAndGet(),
                            "failed_allocations" to if (failAllocations && count > 2) 1L else 0L)
                    }
                    path == "/api/settings" -> {
                        if (exchange.requestMethod == "POST") {
                            if (body == null) code = 400
                            else timeAuto = body["time_auto"] as? Boolean ?: timeAuto
                        }
                        response = mapOf("time_auto" to timeAuto, "wifi_ssid" to "saved", "menu_order" to listOf("keeper"))
                    }
                    path == "/api/apps" -> response = installed.values.toList()
                    path == "/api/apps/removed" -> response = listOf("retained")
                    path == "/api/glyphs" -> if (exchange.requestMethod == "GET") response = mapOf("packs" to emptyList<Any>(), "free" to 65536) else {
                        check(bytes.copyOfRange(12, 24).toString(Charsets.US_ASCII).trimEnd('\u0000') == "stressbench")
                        glyphInstalled = true
                    }
                    path == "/api/glyphs/remove" -> glyphInstalled = false
                    path == "/api/catalog" -> {
                        if (!catalogAvailable) { code = 502; response = mapOf("error" to "offline") }
                        else response = mapOf("apps" to listOf(keeper + mapOf("files" to files()), mapOf("name" to "sample", "version" to "1", "files" to files())))
                    }
                    path == "/api/apps/install" -> if (body == null || name.isEmpty()) code = 400 else {
                        check(name == "sample")
                        installed[name] = mapOf("name" to name, "version" to "1", "values" to emptyMap<String, Any>())
                    }
                    path == "/api/dev/app" -> {
                        check(name != "keeper" && name != "retained")
                        if (failUpload && query["file"] == "app.wasm") { failUpload = false; code = 500 }
                        else if (name == "sbig03" && bytes.size > 1000) code = 400
                        else staged.computeIfAbsent(name) { ConcurrentHashMap() }[query.getValue("file")] = bytes
                    }
                    path == "/api/dev/run" -> {
                        for ((app, packageFiles) in staged) if ("app.wasm" in packageFiles && "manifest.json" in packageFiles) {
                            installed[app] = mapOf("name" to app, "version" to "0.1.0", "values" to (values[app] ?: emptyMap<String, String>()))
                        }
                        uptime.set(0)
                    }
                    path == "/api/apps/settings" -> {
                        if (name.isEmpty() || body == null) code = 400 else {
                            check(name != "keeper")
                            val saved = values.getOrDefault(name, emptyMap()) + body.entries.associate { it.key.toString() to it.value.toString() }
                            values[name] = saved
                            installed.computeIfPresent(name) { _, old -> old + ("values" to saved) }
                            saved["server"]?.takeIf { it.isNotEmpty() }?.let { poll(it) }
                        }
                    }
                    path == "/api/dev/open" -> opened.incrementAndGet()
                    path == "/api/apps/remove" -> {
                        check(query["forget"] == "1" && name != "keeper")
                        if (installed.remove(name) == null) code = 404
                        else { staged.remove(name); values.remove(name) }
                    }
                    path == "/api/restart" -> uptime.set(0)
                    path == "/api/clock" -> if (body == null) code = 400
                }
                val reply = Json.write(response).toByteArray()
                try {
                    exchange.sendResponseHeaders(code, reply.size.toLong())
                    exchange.responseBody.use { it.write(reply) }
                } catch (_: java.io.IOException) { } finally { exchange.close() }
            }
            server.start()
        }
        private fun files() = listOf(mapOf("name" to "app.wasm", "size" to 100), mapOf("name" to "manifest.json", "size" to 100))
        private fun poll(url: String) {
            val uri = URI(url)
            val requests = listOf("pick", "doc", "xml", "raw").map { path -> workers.submit {
                runCatching {
                    Socket(uri.host, uri.port).use { socket ->
                        socket.soTimeout = 1000
                        socket.getOutputStream().write("GET /$path HTTP/1.1\r\nHost: bench\r\n\r\n".toByteArray())
                        socket.getInputStream().read(ByteArray(256))
                    }
                }
            } }
            requests.forEach { it.get() }
        }
        fun assertRestored() {
            check(installed == mapOf("keeper" to keeper)) { "applications changed: $installed" }
            check(!glyphInstalled)
            check(timeAuto)
        }
        override fun close() { server.stop(0); workers.shutdownNow() }
    }
}
