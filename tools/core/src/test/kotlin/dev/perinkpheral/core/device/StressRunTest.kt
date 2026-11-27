// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.device

import com.sun.net.httpserver.HttpServer
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.build.Wasm
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
    private val baseline = mapOf("uptime_s" to 500L, "failed_allocations" to 0L, "last_panic" to "", "reset_reason" to 3L, "log_boot" to "boot1")

    @JvmStatic fun main(args: Array<String>) {
        if (args.firstOrNull() == "child") {
            val code = StressRun(DeviceClient(args[1]), File(args[2]),
                StressRun.Options("127.0.0.1", listOf('d'), quick = true), ::build, { glyphPack() }).run()
            kotlin.system.exitProcess(code)
        }
        serialCapture()
        benchPackages()
        requestDeadlines()
        verdicts()
        regressions()
        chordSemantics()
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
        val oldPanic = baseline + ("last_panic" to "5 \"fault\" main pc 1234")
        val agedPanic = baseline + ("last_panic" to "6 \"fault\" main pc 1234")
        check(StressRun.verdict(oldPanic, agedPanic + ("log_boot" to "boot2"), emptyList(), true).isEmpty())
        check(StressRun.verdict(oldPanic, oldPanic, emptyList(), true).isNotEmpty())
        check(StressRun.verdict(agedPanic, oldPanic, emptyList(), true).isNotEmpty())
        check(StressRun.verdict(baseline, baseline + ("uptime_s" to 501L), emptyList(), elapsedMs = 10_000).isNotEmpty())
        check(StressRun.verdict(baseline, baseline + ("uptime_s" to 508L), emptyList(), elapsedMs = 10_000).isEmpty())
        check(StressRun.verdict(baseline, baseline + ("reset_reason" to 1), emptyList()).isNotEmpty())
        check(StressRun.verdict(baseline, baseline, listOf("first: worst: thirst:0x7")).isEmpty())
        // The task watchdog only prints; a step with it does not fail.
        check(StressRun.verdict(baseline, baseline,
            listOf("E (617989) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:")).isEmpty())
        check(StressRun.verdict(baseline, baseline + mapOf("uptime_s" to 1, "log_boot" to "boot2"), listOf("rst:0xc (SW_CPU),boot:0x7f (SPI_FAST_FLASH_BOOT)"), true).isEmpty())
        check(StressRun.verdict(baseline, baseline, emptyList(), true).any { "X-Log-Boot" in it })
        check(StressRun.verdict(baseline, baseline + ("log_boot" to "boot2"), emptyList()).isNotEmpty())
        check(StressRun.verdict(baseline, baseline, listOf("rst:0x7 (TG0WDT_SYS_RESET)"), true).isNotEmpty())
    }

    private fun glyphPack() = ByteArray(31964).apply { this[6] = 4 }

    private fun benchPackages() {
        val wasi = System.getenv("WASI_SDK_PATH")
        if (wasi.isNullOrBlank()) {
            println("Bench package checks skipped: WASI_SDK_PATH is absent")
            return
        }
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val sdk = Sdk(root)
        val scratch = Files.createTempDirectory("stress-packages").toFile()
        try {
            val builder = AppBuilder(sdk, File(wasi)) {}
            val handlers = setOf("app_on_load", "app_on_focus", "app_on_settings", "app_on_intent", "app_on_button",
                "app_on_timer", "app_on_picked", "app_on_document", "app_on_draw", "app_data_buffer")
            val calls = setOf("kv_get", "kv_set", "file_write", "timer_set", "document_buffer_set", "screen_redraw",
                "answer_value", "answer_count", "gfx_font_height", "gfx_draw_text")
            for (kind in listOf("stress", "stressdoc", "stressfat", "stressbig")) {
                val out = File(scratch, kind)
                builder.build(File(root, "tools/stress/apps/$kind"), out)
                val module = Wasm(File(out, "app.wasm"))
                module.checkTypes(sdk.contract)
                val raw = kind != "stressdoc"
                check(module.exports.toSet() == handlers + if (raw) setOf("app_on_data") else emptySet()) { "$kind exports: ${module.exports}" }
                check(module.imports.toSet() == (calls + if (raw) setOf("http_send") else emptySet()).map { "host" to it }.toSet()) {
                    "$kind imports: ${module.imports}"
                }
                check(module.memoryBytes!! <= 65536 && module.memory!!.first == module.memory!!.second)
                if (kind == "stressfat") check(module.memoryBytes!! > 60000)
                if (kind == "stressbig") check(File(out, "app.wasm").length() in 40000..49152)
            }
            val host = File(root, "firmware/test/host")
            fun execute(vararg command: String): String {
                val log = File(scratch, "runtime.log")
                val process = ProcessBuilder(*command).directory(host).redirectErrorStream(true).redirectOutput(log).start()
                try {
                    check(process.waitFor(120, java.util.concurrent.TimeUnit.SECONDS) && process.exitValue() == 0) { log.readText() }
                    return log.readText()
                } finally { if (process.isAlive) process.destroyForcibly().waitFor() }
            }
            execute("make", "test_app")
            val output = execute(File(host, "test_app").path, File(scratch, "stress/app.wasm").path,
                File(scratch, "stressfat/app.wasm").path)
            check(Regex("(?m)^\\d+ checks, 0 failed$").containsMatchIn(output)) { output }
        } finally { scratch.deleteRecursively() }
    }

    private fun requestDeadlines() {
        for (upload in listOf(false, true)) for (interrupt in listOf(false, true)) {
            java.net.ServerSocket(0, 1, java.net.InetAddress.getLoopbackAddress()).use { listener ->
                val accepted = java.util.concurrent.CountDownLatch(1)
                val drain = java.util.concurrent.CountDownLatch(1)
                val received = java.util.concurrent.atomic.AtomicLong()
                val closed = java.util.concurrent.atomic.AtomicBoolean()
                val failure = java.util.concurrent.atomic.AtomicReference<Throwable>()
                val bytes = ByteArray(if (upload) 16 * 1024 * 1024 else 0)
                val server = Thread.startVirtualThread {
                    listener.accept().use { socket ->
                        socket.soTimeout = 2000
                        val input = socket.getInputStream()
                        val header = StringBuilder()
                        while (!header.endsWith("\r\n\r\n")) {
                            val byte = input.read()
                            if (byte < 0) return@startVirtualThread
                            header.append(byte.toChar())
                        }
                        accepted.countDown()
                        try {
                            if (upload) {
                                drain.await()
                                val buffer = ByteArray(8192)
                                while (true) {
                                    val count = input.read(buffer)
                                    if (count < 0) break
                                    received.addAndGet(count.toLong())
                                }
                            } else {
                                val output = socket.getOutputStream()
                                output.write("HTTP/1.1 200 OK\r\nContent-Length: 10000\r\n\r\n".toByteArray())
                                repeat(10000) { output.write('x'.code); output.flush(); Thread.sleep(20) }
                            }
                            closed.set(true)
                        } catch (_: java.net.SocketTimeoutException) {
                        } catch (_: java.io.IOException) { closed.set(true) }
                    }
                }
                val requester = Thread.startVirtualThread {
                    try {
                        DeviceClient("127.0.0.1:${listener.localPort}").raw("/stall", if (upload) "POST" else "GET",
                            if (interrupt) 60000 else 300, if (upload) bytes else null)
                    } catch (error: Throwable) { failure.set(error) }
                }
                try {
                    check(accepted.await(2, java.util.concurrent.TimeUnit.SECONDS))
                    if (interrupt) requester.interrupt()
                    requester.join(2000)
                    check(!requester.isAlive) { "request did not stop: upload=$upload interrupt=$interrupt" }
                    check(if (interrupt) failure.get() is InterruptedException else failure.get() is java.io.IOException) { failure.get().toString() }
                    drain.countDown()
                    server.join(3000)
                    check(!server.isAlive && closed.get()) { "cancelled connection stayed open" }
                    if (upload) check(received.get() < bytes.size) { "stalled upload completed after cancellation" }
                } finally { drain.countDown(); requester.interrupt(); server.interrupt() }
            }
        }
    }

    private fun serialCapture() {
        val source = Files.createTempFile("stress-console", ".txt").toFile().apply { writeText("first: 1234 qwer p\n") }
        val output = Files.createTempFile("stress-capture", ".txt").toFile()
        try {
            val commands = CopyOnWriteArrayList<List<String>>()
            StressSerial(source.path, output, { commands += it; true }).use { capture ->
                val deadline = System.nanoTime() + 2_000_000_000
                while (output.length() == 0L && System.nanoTime() < deadline) Thread.sleep(10)
                check(output.readText() == source.readText())
                check(!capture.diagnostic.contains("not captured"))
            }
            check(commands.size == 2)
            val flag = if (System.getProperty("os.name").lowercase().contains("mac")) "-f" else "-F"
            check(commands.all { it == listOf("stty", flag, source.path, "raw", "-echo", "clocal") })
            check(source.readText() == "first: 1234 qwer p\n")
            for (failure in 1..2) {
                var calls = 0
                StressSerial(source.path, output, { ++calls != failure }).use { capture ->
                    val deadline = System.nanoTime() + 2_000_000_000
                    while (!capture.diagnostic.contains("cannot disable") && System.nanoTime() < deadline) Thread.sleep(10)
                    check(capture.diagnostic.contains("console not captured: cannot disable"))
                    check(output.length() == 0L)
                    check(calls == failure)
                }
            }
            source.delete()
            commands.clear()
            StressSerial(source.path, output, { commands += it; true }).use { capture ->
                val deadline = System.nanoTime() + 3_000_000_000
                while (!capture.diagnostic.contains("unavailable") && System.nanoTime() < deadline) Thread.sleep(10)
                source.writeText("reconnected\n")
                while (output.length() == 0L && System.nanoTime() < deadline) Thread.sleep(10)
                check(output.readText() == "reconnected\n")
                check(commands.size == 2)
            }
            source.writeText("")
            StressSerial(source.path, output, { true }, silenceMs = 20).use { capture ->
                Thread.sleep(100)
                check(capture.diagnostic.contains("console not captured: no bytes received after opening"))
            }
        } finally { source.delete(); output.delete() }
    }

    private fun regressions() {
        fun exercise(axes: List<Char> = listOf('b'), expected: Int = 1, recoveryMs: Long = 1000,
                     allowRememberedChange: Boolean = false,
                     setup: (FakeDevice, File) -> Unit = { _, _ -> },
                     say: (FakeDevice, File, String) -> Unit = { _, _, _ -> },
                     verify: (FakeDevice, File) -> Unit) {
            FakeDevice().use { fake ->
                val out = Files.createTempDirectory("stress-regression").toFile()
                val clock = java.util.concurrent.atomic.AtomicLong(0)
                fake.now = { clock.get() }
                fake.resetClock()
                fake.advanceTime = { clock.addAndGet(it) }
                try {
                    setup(fake, out)
                    val code = StressRun(DeviceClient(fake.host), out, StressRun.Options("127.0.0.1", axes, quick = true, allowRememberedChange = allowRememberedChange),
                        ::build, ::glyphPack, { say(fake, out, it) },
                        StressRun.Timing(scale = 0.0001, now = { clock.get() }, sleep = { clock.addAndGet(it.coerceAtLeast(1)) },
                            recoveryMs = recoveryMs, cleanupMs = 30_000, installMs = 20_000, absentMs = 2000)).run()
                    check(code == expected) { out.resolve("report.md").readText() + fake.calls.takeLast(20).joinToString("\n") }
                    verify(fake, out)
                } finally { out.deleteRecursively() }
            }
        }
        for (reason in listOf(1, 4, 5, 6, 7, 9)) exercise(listOf('g'), setup = { fake, _ -> fake.restartReason = reason }) { _, out ->
            check("expected 3" in out.resolve("report.md").readText()) { out.resolve("report.md").readText() }
        }
        exercise(listOf('g'), setup = { fake, _ -> fake.freezePanicCounter = true }) { _, out ->
            check("counter did not rise" in out.resolve("report.md").readText())
        }
        exercise(listOf('g'), setup = { fake, _ ->
            fake.noopRestart = true; fake.panicRecord = false; fake.resetReason = 3
        }) { _, out -> check("requested restart did not change X-Log-Boot" in out.resolve("report.md").readText()) }
        exercise(listOf('g'), expected = 0, recoveryMs = 5000,
            setup = { fake, _ -> fake.restartDelay = 3; fake.panicRecord = false }) { fake, _ -> fake.assertRestored() }
        exercise(setup = { fake, _ -> fake.statusChange = { count, sample ->
            if (count >= 3) sample + ("reset_reason" to 9) else sample
        } }) { _, out -> check("reset_reason changed" in out.resolve("report.md").readText()) }
        exercise(setup = { fake, _ -> fake.statusChange = { count, sample ->
            // The second before-sample follows the first after-sample with no step covering the gap.
            if (count >= 4) sample + ("last_panic" to "1 \"new fault\" main pc 5678") else sample
        } }) { _, out -> check("before workers" in out.resolve("report.md").readText()) }
        exercise(say = { _, out, line ->
            if (line.contains("=2/2/2/4")) out.resolve("serial.log").appendText("rst:0x7 (TG0WDT_SYS_RESET)\n")
        }) { _, out -> check("console: rst:0x7" in out.resolve("report.md").readText()) }
        exercise(setup = { fake, out -> fake.onCall = { call ->
            if (call.startsWith("POST /api/glyphs/remove")) out.resolve("serial.log").appendText("abort() called during cleanup\n")
        } }) { _, out -> check("abort() called during cleanup" in out.resolve("report.md").readText()) }
        exercise(expected = 0, say = { _, out, line ->
            if (line.contains("=2/2/2/4")) out.resolve("serial.log").appendText("first: 123 worst:456\n")
        }) { fake, _ -> fake.assertRestored() }
        exercise(listOf('e'), expected = 0, setup = { fake, _ -> fake.releaseFailures = 2 }) { fake, _ ->
            check(fake.calls.count { "button=4&press=up" in it } >= 3)
            check(!fake.backHeld)
        }
        for (button in listOf("1", "3", "4")) exercise(listOf('e'), expected = 0,
            setup = { fake, _ -> fake.releaseFailures = 2; fake.releaseFailureButton = button }) { fake, _ ->
            fake.assertRestored()
        }
        exercise(listOf('e'), setup = { fake, _ -> fake.failDirectionDown = true }) { fake, _ ->
            fake.assertRestored()
            check(fake.calls.any { "button=4&press=up" in it })
        }
        exercise(listOf('e'), setup = { fake, _ -> fake.interruptOnDirectionDown = Thread.currentThread() }) { fake, out ->
            fake.assertRestored()
            check("Interrupted" in out.resolve("report.md").readText())
        }
        for (cancel in listOf(false, true)) exercise(expected = if (cancel) 1 else 0,
            setup = { fake, _ -> fake.dropInstall = true; fake.removeBusy = 4 },
            say = { fake, _, line -> if (cancel && fake.pendingInstall && line.contains("=2/2/2/4")) throw InterruptedException() }
        ) { fake, _ ->
            check(fake.busyProbes == 4)
            check(fake.calls.count { it == "POST /api/apps/install" } >= 5)
            fake.assertRestored()
        }
        exercise(expected = 0, setup = { fake, _ -> fake.reappear = true }) { fake, _ ->
            check(fake.calls.count { "POST /api/apps/remove?app=sample" in it } >= 4)
            fake.assertRestored()
        }
        exercise(listOf('b', 'g'), say = { _, _, line -> if (line.startsWith("G clock")) throw InterruptedException() }) { fake, _ ->
            val removedAt = fake.calls.indexOfFirst { it.startsWith("POST /api/apps/remove") }
            val restoredAt = fake.calls.indexOfLast { it == "POST /api/settings" }
            check(restoredAt in 0 until removedAt)
            fake.assertRestored()
        }
        exercise(setup = { fake, _ -> fake.onCall = { call ->
            if (call.startsWith("POST /api/glyphs/remove")) fake.advanceTime(40_000)
        } }) { _, out -> check("cleanup deadline expired" in out.resolve("report.md").readText()) }
        exercise(expected = 0, setup = { fake, _ ->
            fake.packs["greek"] = mapOf("name" to "greek", "bytes" to 100L, "faces" to 4)
            fake.packs["cyrillic"] = mapOf("name" to "cyrillic", "bytes" to 100L, "faces" to 4)
        }) { fake, out ->
            check(fake.calls.none { it == "POST /api/glyphs" })
            check("face table" in out.resolve("report.md").readText())
        }
        exercise(expected = 0, setup = { fake, _ -> fake.exposeFaces = false }) { fake, _ ->
            check(fake.calls.none { it == "POST /api/glyphs" })
        }
        exercise(setup = { fake, _ -> fake.onCall = { call ->
            if (call.startsWith("POST /api/glyphs/remove")) fake.packs["latin"] = mapOf("name" to "latin", "bytes" to 999L, "faces" to 4)
        } }) { _, out -> check("installed glyph packs changed" in out.resolve("report.md").readText()) }
        exercise(expected = 0, setup = { fake, _ -> fake.removed = List(16) { "retained$it" } }) { fake, out ->
            check(fake.calls.none { "/api/apps/install?" in it })
            check("retained-store list is full" in out.resolve("report.md").readText())
        }
        for (axis in 'c'..'f') exercise(listOf(axis), expected = 0,
            setup = { fake, _ -> fake.removed = List(16) { "retained$it" } }) { fake, out ->
            check(fake.calls.none { "/api/dev/app?" in it || "/api/apps/remove?" in it || "/api/apps/settings?" in it })
            check("retained-store list is full" in out.resolve("report.md").readText())
        }
        exercise(listOf('b', 'c', 'd'), expected = 0, setup = { fake, _ -> fake.removed = listOf("retained") }) { fake, out ->
            check(fake.calls.none { "/api/apps/settings?" in it || "/api/apps/install?" in it || "/api/dev/" in it })
            check(fake.removed == listOf("retained"))
            check("retained stores exist" in out.resolve("report.md").readText())
        }
        exercise(listOf('d'), expected = 0) { fake, out ->
            val report = out.resolve("report.md").readText()
            check("Remembered application preserved: `keeper`" in report)
            check(fake.remembered == "keeper" && fake.currentApp == "keeper")
            check(out.resolve("initial-screen.bmp").readBytes().contentEquals(fake.initialFrame))
            val initial = Json.parse(out.resolve("initial-status.json").readText()) as Map<*, *>
            check(initial["dev_tools"] == true && initial["uptime_s"] is Number)
            check(initial["last_app"] == "keeper")
            check(fake.calls.indexOf("GET /api/screen") < fake.calls.indexOfFirst { it.startsWith("POST ") })
        }
        exercise(listOf('e'), expected = 0) { fake, _ ->
            check(fake.ownerButtonWrites == 0)
            check(fake.siblingSwitches > 0)
        }
        exercise(('c'..'f').toList(), expected = 0, setup = { fake, _ -> fake.exposeRemembered = false }) { fake, out ->
            check(fake.calls.none { it.startsWith("POST ") })
            check(out.resolve("report.md").readText().split("remembered application cannot be determined").size == 5)
            fake.assertRestored()
        }
        exercise(listOf('d'), expected = 0, allowRememberedChange = true,
            setup = { fake, _ -> fake.exposeRemembered = false }) { fake, out ->
            check(fake.calls.any { it.startsWith("POST /api/dev/app?") })
            check(fake.remembered.isEmpty())
            check("remembered application could not be restored" in out.resolve("report.md").readText())
        }
        exercise(listOf('b'), expected = 0, setup = { fake, _ -> fake.exposeRemembered = false }) { fake, out ->
            check(fake.calls.none { it.startsWith("POST /api/apps/install?") })
            check("install part skipped: remembered application" in out.resolve("report.md").readText())
            fake.assertRestored()
        }
        exercise(listOf('e'), expected = 0, setup = { fake, _ ->
            fake.exposeRemembered = false
            fake.statusChange = { _, sample -> sample + ("current_app" to fake.currentApp) }
        }) { fake, _ -> fake.assertRestored() }
        exercise(listOf('b'), expected = 0, setup = { fake, _ -> fake.fillApplications(24) }) { fake, out ->
            check(fake.calls.none { it.startsWith("POST /api/apps/install?") })
            check(fake.calls.count { it == "GET /api/catalog?source=0" } > 1)
            check(fake.calls.any { it == "POST /api/glyphs" })
            check(fake.calls.count { it == "GET /api/apps" } > 2)
            check(fake.installed.size == 24)
            check("install part skipped: no remaining application slots" in out.resolve("report.md").readText())
        }
        for (count in listOf(23, 24)) exercise(listOf('f'), expected = 0,
            setup = { fake, _ -> fake.fillApplications(count) }) { fake, out ->
            val uploads = fake.calls.filter { "POST /api/dev/app?" in it }.map { it.substringAfter("name=").substringBefore('&') }.toSet()
            check(uploads.size == 24 - count) { uploads }
            check(fake.maxInstalled <= 24)
            check("application slot cap" in out.resolve("report.md").readText())
        }
        for ((code, text, free, expected) in listOf(
            listOf(500, "the file could not be stored", 4096, 0),
            listOf(400, "not an application's name, or the storage refused", 4096, 0),
            listOf(500, "the file could not be stored", 200000, 1),
            listOf(500, "other failure", 4096, 1),
            listOf(400, "the file is empty or too large for an application", 4096, 1),
            listOf(409, "conflict", 4096, 1))) {
            exercise(listOf('f'), expected = expected as Int, setup = { fake, _ ->
                fake.storageCode = code as Int; fake.storageText = text as String; fake.storageFree = (free as Int).toLong()
            }) { _, out ->
                check(("storage_refusal" in out.resolve("steps.jsonl").readText()) == (expected == 0))
            }
        }
        for (duringCleanup in listOf(false, true)) exercise(listOf('b'), setup = { fake, out ->
            fake.onCall = { call ->
                if (call.startsWith(if (duringCleanup) "POST /api/apps/remove" else "POST /api/glyphs")) {
                    out.resolve("steps.jsonl").delete(); out.resolve("steps.jsonl").mkdir()
                }
            }
        }) { fake, out ->
            fake.assertRestored()
            check(fake.calls.any { it.startsWith("POST /api/glyphs/remove") })
            check("Recording steps.jsonl" in out.resolve("report.md").readText())
        }
        exercise(expected = 0, setup = { fake, _ -> fake.onCall = { call ->
            if (call == "GET /") fake.advanceTime(10_000)
        } }) { fake, _ -> fake.assertRestored() }
        exercise(setup = { fake, _ -> fake.statusChange = { count, sample ->
            if (count == 3) fake.advanceTime(10_000)
            if (count >= 3) sample + ("uptime_s" to 500L + count) else sample
        } }) { _, out -> check("uptime fell behind elapsed time" in out.resolve("report.md").readText()) }
        exercise(setup = { fake, _ -> fake.statusChange = { count, sample ->
            if (count >= 4) sample + ("uptime_s" to 500L + count) else sample
        } }, say = { fake, _, line -> if (line.contains("=2/2/2/4")) fake.advanceTime(10_000) }) { _, out ->
            check("uptime fell behind elapsed time" in out.resolve("report.md").readText())
        }
        // A console marker may be split between two observations.
        exercise(setup = { fake, out -> fake.onCall = { call ->
            if (call == "GET /api/apps" && fake.calls.count { it == "GET /api/apps" } == 2)
                out.resolve("serial.log").appendText("partial rst:")
        } }, say = { _, out, line -> if (line.contains("=2/2/2/4")) out.resolve("serial.log").appendText("0x7 (TG0WDT_SYS_RESET)\n") }) { _, out ->
            check("console: partial rst:0x7" in out.resolve("report.md").readText())
        }
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-full-c").toFile()
            try {
                check(StressRun(DeviceClient(fake.host), out, StressRun.Options("127.0.0.1", listOf('c')),
                    ::build, ::glyphPack, {},
                    StressRun.Timing(scale = 0.002, recoveryMs = 1000)).run() == 0) {
                    out.resolve("report.md").readText()
                }
                val steps = out.resolve("steps.jsonl").readText()
                check("doc endless connection release" in steps && "raw endless connection release" in steps)
                for ((path, case) in StressServer.POLL_CASES) check("$path/$case" in steps)
                fake.assertRestored()
            } finally { out.deleteRecursively() }
        }
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-delayed-release").toFile()
            try {
                val code = StressRun(DeviceClient(fake.host), out, StressRun.Options("127.0.0.1", listOf('c')),
                    ::build, ::glyphPack, { if (it.contains("doc endless connection release")) {
                        fake.holdReleasePath = "doc"; fake.releaseDelayMs = 650
                    } }, StressRun.Timing(scale = 0.002, recoveryMs = 1000)).run()
                check(code == 1)
                check("doc/endless did not close" in out.resolve("report.md").readText())
                fake.assertRestored()
            } finally { out.deleteRecursively() }
        }
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-wrong-release").toFile()
            try {
                val code = StressRun(DeviceClient(fake.host), out, StressRun.Options("127.0.0.1", listOf('c')),
                    ::build, ::glyphPack, { if (it.contains("doc endless connection release")) { fake.holdReleasePath = "doc"; fake.releaseDelayMs = 650; fake.wrongReleasePath = true; fake.repeatPolls = true } },
                    StressRun.Timing(scale = 0.002, recoveryMs = 1000)).run()
                check(code == 1)
                check("doc/endless did not close" in out.resolve("report.md").readText()) { out.resolve("report.md").readText() }
                fake.assertRestored()
            } finally { out.deleteRecursively() }
        }
    }

    private fun build(kind: String, out: File) {
        out.mkdirs()
        val manifest = mapOf("name" to kind, "title" to kind, "version" to "0.1.0", "abi" to 3)
        File(out, "manifest.json").writeText(Json.write(manifest))
        File(out, "app.wasm").writeBytes(ByteArray(if (kind == "stressbig") 42_000 else 100) { 1 })
    }

    private fun chordSemantics() {
        FakeDevice().use { fake ->
            val client = DeviceClient(fake.host)
            fun press(button: Int, kind: String) {
                check(client.raw("/api/dev/button?button=$button&press=$kind", "POST").status == 200)
            }
            press(4, "down")
            press(1, "short")
            check(fake.siblingSwitches == 0 && fake.ownerButtonWrites == 1)
            press(1, "down")
            check(fake.siblingSwitches == 1)
            press(1, "up")
            press(1, "short")
            press(4, "up")
            press(4, "short")
            check(fake.ownerButtonWrites == 1 && fake.currentApp == "keeper")
            fake.assertRestored()
        }
    }

    private fun runAxes() {
        FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-axes").toFile()
            try {
                val code = StressRun(DeviceClient(fake.host), out,
                    StressRun.Options("127.0.0.1", quick = true), ::build, { glyphPack() }, {},
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
                    ::build, { glyphPack() }, {}, StressRun.Timing(scale = 0.0001, recoveryMs = 1000, absentMs = 20)).run()
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
                    { glyphPack() }, {}, StressRun.Timing(scale = 0.0001, recoveryMs = 1000)).run()
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
        for (stalled in listOf(false, true)) FakeDevice().use { fake ->
            val out = Files.createTempDirectory("stress-signal").toFile()
            val log = File(out.parentFile, out.name + ".log")
            val uploadReached = java.util.concurrent.CountDownLatch(1)
            if (stalled) fake.onCall = { call ->
                if (call.startsWith("POST /api/dev/app?") && call.endsWith("file=app.wasm") && uploadReached.count > 0) {
                    uploadReached.countDown()
                    Thread.sleep(60000)
                }
            }
            val process = ProcessBuilder(File(System.getProperty("java.home"), "bin/java").path,
                "-cp", System.getProperty("java.class.path"), StressRunTest::class.java.name,
                "child", fake.host, out.path).redirectErrorStream(true).redirectOutput(log).start()
            try {
                val deadline = System.nanoTime() + 15_000_000_000L
                fun reached() = if (stalled) uploadReached.count == 0L else fake.opened.get() > 0
                while (!reached() && process.isAlive && System.nanoTime() < deadline) Thread.sleep(20)
                check(reached()) { log.readText() }
                ProcessBuilder("kill", "-INT", process.pid().toString()).start().waitFor()
                check(process.waitFor(20, java.util.concurrent.TimeUnit.SECONDS)) { "SIGINT cleanup did not finish" }
                fake.assertRestored()
                check(process.exitValue() == 1) { "SIGINT exit was ${process.exitValue()}" }
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
        val installed = ConcurrentHashMap<String, Map<String, Any?>>()
        private val values = ConcurrentHashMap<String, Map<String, String>>()
        private val workers = Executors.newVirtualThreadPerTaskExecutor()
        private val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        private val statusCalls = AtomicInteger()
        private val uptime = AtomicInteger(500)
        val opened = AtomicInteger()
        @Volatile var failAllocations = false
        @Volatile var failUpload = false
        @Volatile var timeAuto = true
        @Volatile var resetReason = 1
        @Volatile var restartReason = 3
        @Volatile var noopRestart = false
        var restartDelay = 0
        private var restartOnStatus = 0
        var panicRecord = true
        @Volatile var freezePanicCounter = false
        @Volatile var statusChange: (Int, Map<String, Any?>) -> Map<String, Any?> = { _, sample -> sample }
        @Volatile var onCall: (String) -> Unit = {}
        var advanceTime: (Long) -> Unit = {}
        @Volatile var now: () -> Long = { System.nanoTime() / 1_000_000 }
        private var bootAt = now()
        fun resetClock() { bootAt = now() }
        private val panicCounter = AtomicInteger(5)
        private val bootCounter = AtomicInteger(1)
        var removed = emptyList<String>()
        val packs = ConcurrentHashMap<String, Map<String, Any?>>()
        var exposeFaces = true
        var exposeRemembered = true
        var remembered = "keeper"
        var currentApp = "keeper"
        var ownerButtonWrites = 0
        var siblingSwitches = 0
        var maxInstalled = 1
        var storageCode = 500
        var storageText = "the file could not be stored"
        var storageFree = 4096L
        var dropInstall = false
        @Volatile var pendingInstall = false
        var busyProbes = 0
        var removeBusy = 0
        var reappear = false
        private var absentPolls = 0
        var releaseFailures = 0
        var releaseFailureButton: String? = null
        var failDirectionDown = false
        var interruptOnDirectionDown: Thread? = null
        var backHeld = false
        private var backChorded = false
        private var backLong = false
        private val heldButtons = mutableSetOf<String>()
        private val swallowed = mutableSetOf<String>()
        @Volatile var wrongReleasePath = false
        @Volatile var repeatPolls = false
        @Volatile var releaseDelayMs = 0L
        @Volatile var holdReleasePath = ""
        private val pollSockets = ConcurrentHashMap.newKeySet<Socket>()
        private val polling = ConcurrentHashMap<String, Int>()
        private val generations = AtomicInteger()
        private val keeper = mapOf("name" to "keeper", "version" to "1.0.0", "values" to mapOf("secret" to true, "city" to "", "url" to "http://saved"))
        val initialFrame = java.io.ByteArrayOutputStream().also {
            javax.imageio.ImageIO.write(java.awt.image.BufferedImage(400, 300, java.awt.image.BufferedImage.TYPE_BYTE_BINARY), "bmp", it)
        }.toByteArray()
        val host get() = "127.0.0.1:${server.address.port}"
        fun fillApplications(count: Int) {
            for (index in 2..count) installed["owner$index"] = mapOf("name" to "owner$index", "version" to "1", "values" to emptyMap<String, Any>())
            maxInstalled = installed.size
        }

        init {
            installed["keeper"] = keeper
            packs["latin"] = mapOf("name" to "latin", "bytes" to 1000L, "faces" to 4)
            server.executor = workers
            server.createContext("/") { exchange ->
                val uri = exchange.requestURI
                val path = uri.path
                val call = "${exchange.requestMethod} $uri"
                calls += call
                onCall(call)
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
                        if (restartOnStatus > 0 && --restartOnStatus == 0) restart()
                        val count = statusCalls.incrementAndGet()
                        val sample = (baseline - "log_boot") + mapOf("dev_tools" to dev, "uptime_s" to uptime.incrementAndGet() + (now() - bootAt) / 1000,
                            "reset_reason" to resetReason, "last_panic" to if (panicRecord) "${panicCounter.get()} \"old fault\" main pc 1234" else "",
                            "storage_ok" to true, "storage_total" to 655360L, "storage_used" to 655360L - storageFree,
                            "failed_allocations" to if (failAllocations && count > 2) 1L else 0L)
                        val named = if (exposeRemembered) sample + ("last_app" to remembered) else sample
                        response = statusChange(count, if (panicRecord) named else named - "last_panic")
                    }
                    path == "/api/log" -> { exchange.responseHeaders.set("X-Log-Boot", "boot${bootCounter.get()}"); response = "log" }
                    path == "/api/screen" -> response = initialFrame
                    path == "/api/settings" -> {
                        if (exchange.requestMethod == "POST") {
                            if (body == null) code = 400
                            else timeAuto = body["time_auto"] as? Boolean ?: timeAuto
                        }
                        response = mapOf("time_auto" to timeAuto, "wifi_ssid" to "saved", "menu_order" to listOf("keeper"))
                    }
                    path == "/api/apps" -> {
                        if (absentPolls > 0 && --absentPolls == 0)
                            installed["sample"] = mapOf("name" to "sample", "version" to "1", "values" to emptyMap<String, Any>())
                        response = installed.values.toList()
                    }
                    path == "/api/apps/removed" -> response = removed
                    path == "/api/glyphs" -> if (exchange.requestMethod == "GET") response = mapOf("packs" to packs.values.map {
                        if (exposeFaces) it else it - "faces"
                    }, "free" to 65536) else {
                        check(bytes.copyOfRange(12, 24).toString(Charsets.US_ASCII).trimEnd('\u0000') == "stressbench")
                        packs["stressbench"] = mapOf("name" to "stressbench", "bytes" to bytes.size, "faces" to 4)
                    }
                    path == "/api/glyphs/remove" -> packs.remove(name)
                    path == "/api/catalog" -> {
                        if (!catalogAvailable) { code = 502; response = mapOf("error" to "offline") }
                        else response = mapOf("apps" to listOf(keeper + mapOf("files" to files()), mapOf("name" to "sample", "version" to "1", "files" to files())))
                    }
                    path == "/api/apps/install" -> {
                        if (pendingInstall) {
                            code = 503
                            if (name.isEmpty() && ++busyProbes == 4) {
                                installed["sample"] = mapOf("name" to "sample", "version" to "1", "values" to emptyMap<String, Any>())
                                pendingInstall = false
                            }
                        } else if (body == null || name.isEmpty()) code = 400
                        else {
                            check(name == "sample")
                            if (dropInstall) {
                                dropInstall = false
                                pendingInstall = true
                                exchange.close()
                                return@createContext
                            }
                            installed[name] = mapOf("name" to name, "version" to "1", "values" to emptyMap<String, Any>())
                            currentApp = name
                            remembered = name
                        }
                    }
                    path == "/api/dev/app" -> {
                        check(name != "keeper" && name != "retained")
                        if (failUpload && query["file"] == "app.wasm") { failUpload = false; code = 500 }
                        else if (name == "sbig03" && bytes.size > 1000) { code = storageCode; response = storageText }
                        else staged.computeIfAbsent(name) { ConcurrentHashMap() }[query.getValue("file")] = bytes
                    }
                    path == "/api/dev/run" -> {
                        for ((app, packageFiles) in staged) if ("app.wasm" in packageFiles && "manifest.json" in packageFiles) {
                            installed[app] = mapOf("name" to app, "version" to "0.1.0", "values" to (values[app] ?: emptyMap<String, String>()))
                        }
                        maxInstalled = maxOf(maxInstalled, installed.size)
                        remembered = name
                        currentApp = name
                        restart()
                    }
                    path == "/api/apps/settings" -> {
                        if (name.isEmpty() || body == null) code = 400 else {
                            check(name != "keeper")
                            removed = emptyList()
                            val saved = values.getOrDefault(name, emptyMap()) + body.entries.associate { it.key.toString() to it.value.toString() }
                            values[name] = saved
                            installed.computeIfPresent(name) { _, old -> old + ("values" to saved) }
                            polling.remove(name)
                            saved["server"]?.takeIf { it.isNotEmpty() }?.let { url ->
                                poll(url)
                                val generation = generations.incrementAndGet()
                                polling[name] = generation
                                if (repeatPolls) workers.submit {
                                    while (polling[name] == generation) {
                                        Thread.sleep(maxOf(20, releaseDelayMs))
                                        poll(url, if (wrongReleasePath) listOf("pick") else listOf("pick", "doc", "xml", "raw"))
                                    }
                                }
                            }
                        }
                    }
                    path == "/api/dev/open" -> { opened.incrementAndGet(); currentApp = name; remembered = name }
                    path == "/api/dev/button" -> {
                        val button = query.getValue("button")
                        when (query["press"]) {
                            "down" -> {
                                heldButtons += button
                                if (button == "4") { backHeld = true; backChorded = false; backLong = false }
                                else if (button in listOf("1", "3")) {
                                    swallowed -= button
                                    if (backHeld && currentApp.isNotEmpty()) {
                                        backChorded = true
                                        swallowed += button
                                        currentApp = "keeper"; remembered = "keeper"; siblingSwitches++
                                    }
                                    if (failDirectionDown) { failDirectionDown = false; code = 503 }
                                    interruptOnDirectionDown?.let { interruptOnDirectionDown = null; it.interrupt() }
                                }
                            }
                            "up" -> if (releaseFailures > 0 && (releaseFailureButton == null || releaseFailureButton == button)) { releaseFailures--; code = 503 }
                                else if (heldButtons.remove(button) && button == "4") {
                                    backHeld = false
                                    if (backLong && !backChorded) currentApp = ""
                                    backChorded = backChorded && !backLong
                                    backLong = false
                                }
                            "long" -> if (button == "4") {
                                if (backHeld) backLong = true else currentApp = ""
                            }
                            "short" -> if (button == "4") {
                                if (backChorded) backChorded = backHeld else currentApp = ""
                            } else if (button in listOf("1", "3") && !swallowed.remove(button) && currentApp == "keeper") {
                                ownerButtonWrites++
                            }
                        }
                    }
                    path == "/api/apps/remove" -> {
                        check(query["forget"] == "1" && name != "keeper")
                        if (removeBusy > 0) { removeBusy--; code = 503 }
                        else if (installed.remove(name) == null) code = 404
                        else {
                            staged.remove(name); values.remove(name); polling.remove(name)
                            if (name == "sample" && reappear) { reappear = false; absentPolls = 2 }
                        }
                        if (remembered == name && code == 200) remembered = ""
                    }
                    path == "/api/restart" -> restart()
                    path == "/api/clock" -> if (body == null) code = 400
                }
                val reply = when (response) { is ByteArray -> response; is String -> response.toByteArray(); else -> Json.write(response).toByteArray() }
                try {
                    exchange.responseHeaders.set("Connection", "close")
                    exchange.sendResponseHeaders(code, reply.size.toLong())
                    exchange.responseBody.use { it.write(reply) }
                } catch (_: java.io.IOException) { } finally { exchange.close() }
            }
            server.start()
        }
        private fun files() = listOf(mapOf("name" to "app.wasm", "size" to 100), mapOf("name" to "manifest.json", "size" to 100))
        private fun restart() {
            if (noopRestart) return
            if (restartDelay > 0) { restartOnStatus = restartDelay; restartDelay = 0; return }
            uptime.set(0)
            bootAt = now()
            resetReason = restartReason
            if (!freezePanicCounter) panicCounter.incrementAndGet()
            bootCounter.incrementAndGet()
        }
        private fun poll(url: String, paths: List<String> = listOf("pick", "doc", "xml", "raw")) {
            val uri = URI(url)
            val requests = paths.map { path -> workers.submit {
                runCatching {
                    val socket = Socket(uri.host, uri.port)
                    run {
                        pollSockets += socket
                        socket.soTimeout = 1000
                        socket.getOutputStream().write("GET /$path HTTP/1.1\r\nHost: bench\r\n\r\n".toByteArray())
                        socket.getInputStream().read(ByteArray(256))
                        if (path == holdReleasePath) {
                            // Hold the tested socket independently of subsequent ordinary polls.
                            workers.submit {
                                try { Thread.sleep(releaseDelayMs) } finally { socket.close(); pollSockets -= socket }
                            }
                            return@submit
                        }
                        socket.close()
                        pollSockets -= socket
                    }
                }
            } }
            requests.forEach { it.get() }
        }
        fun assertRestored() {
            check(installed == mapOf("keeper" to keeper)) { "applications changed: $installed" }
            check(packs == mapOf("latin" to mapOf("name" to "latin", "bytes" to 1000L, "faces" to 4))) { "glyph packs changed: $packs" }
            check(!backHeld && heldButtons.isEmpty())
            check(remembered == "keeper") { "remembered application changed: $remembered" }
            check(timeAuto)
        }
        override fun close() { polling.clear(); pollSockets.toList().forEach { it.close() }; server.stop(0); workers.shutdownNow() }
    }
}
