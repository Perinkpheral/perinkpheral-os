// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.app.Applications
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.device.Bmp
import dev.perinkpheral.core.device.DeviceButton
import dev.perinkpheral.core.device.DeviceClient
import dev.perinkpheral.core.simulator.InOrder
import dev.perinkpheral.core.simulator.SimulatorNetwork
import dev.perinkpheral.core.simulator.SimulatorPort
import java.io.File
import java.io.RandomAccessFile
import java.nio.file.Files
import java.util.concurrent.ConcurrentHashMap
import javax.imageio.ImageIO

/**
 * Applications run in the simulator and their screens held against pictures that were looked at
 * and kept: what CI runs on every push, and what a person runs before one.
 *
 * A test is a folder with scenario.json and the pictures it expects, PNGs of the frame:
 *
 *     {
 *       "app": "apps/weather",              the application, from where the tests are run
 *       "at": 1794832200,                    the clock's start, Unix seconds
 *       "tz": "Europe/Moscow",               the computer's zone for the run
 *       "font": "departure", "language": "en", "clock": false, "glyphs": "glyphs",
 *       "serial": true,                      run while no other scenario does; absent: among others
 *       "store": { "lat": "59.93" },         the application's store before it starts
 *       "mocks": { "https://api...*": { "body": "..." } },  the network, as `sim --mocks` takes it,
 *                                            or the name of such a file beside the scenario
 *       "steps": [ "wait 6", "shot weather", "press 2 2 1L", "forward 60", "shot later" ]
 *     }
 *
 * A step is `wait <seconds>`, `press <keys>` (1-4, 1L-4L, each waited for on the panel),
 * `forward <seconds>` (the simulator's clock moved on) or `shot <name>`: the frame against
 * <name>.png. A frame that differs is written beside it as <name>.actual.png; --update writes
 * the expected pictures instead.
 *
 * Scenarios run several at a time and are told in their own order. Each has a simulator, a
 * port and a state folder of its own, and writes only into its own folder of pictures. The
 * simulator's clock is the computer's and `wait` is real time: what a scenario counts on
 * within a wait, it counts on from a computer that may be busy. So the applications are all
 * built before the first simulator starts, and a scenario that cannot share the computer
 * says `"serial": true`.
 */
class SimTest(private val root: File, private val builder: () -> AppBuilder) {

    /** A test as it was read; `steps` is why it could not be, when it could not. */
    private class Scenario(val number: Int, val folder: File, val name: String, val steps: Result<Map<String, Any?>>) {
        val appDir: File? get() = (steps.getOrNull()?.get("app") as? String)?.let { File(it).canonicalFile }
        val isSerial: Boolean get() = steps.getOrNull()?.get("serial") == true
    }

    /** An application built once for every scenario of it. */
    private class Package(val name: String, val folder: File)

    private val simulators = ConcurrentHashMap.newKeySet<Process>()

    /** Runs every test under `folder`, `workers` of them at a time; returns what failed, as lines. */
    @Suppress("UNCHECKED_CAST")
    fun run(folder: File, update: Boolean, workers: Int = defaultWorkers()): List<String> {
        val folders = folder.walk().filter { it.name == "scenario.json" }.map { it.parentFile }.sortedBy { it.path }.toList()
        if (folders.isEmpty()) throw BuildError("${folder.path}: no scenario.json under it")
        val binary = simulatorBinary()
        val scratch = Files.createTempDirectory("perinkpheral-simtest").toFile()
        // Stopped from outside, the run leaves no simulator behind.
        val cleanup = Thread {
            simulators.forEach { it.destroyForcibly() }
            scratch.deleteRecursively()
        }
        Runtime.getRuntime().addShutdownHook(cleanup)
        try {
            val scenarios = folders.mapIndexed { number, scenario ->
                Scenario(number, scenario, scenario.relativeTo(folder).path.ifEmpty { scenario.name },
                    runCatching { readScenario(File(scenario, "scenario.json")) })
            }
            val packages = build(scenarios.mapNotNull { it.appDir }.distinct(), File(scratch, "packages"), workers)
            val failures = ArrayList<String>()
            var shots = 0
            InOrder.run(scenarios, workers, isAlone = { it.isSerial },
                work = { runOne(it, packages, File(scratch, "state-${it.number}"), binary, update) }) { scenario, result ->
                result.onSuccess { shots += it }
                    .onFailure { failures += "${scenario.name}: ${it.message ?: it.toString()}" }
                println("${if (result.isSuccess) "ok  " else "FAIL"} ${scenario.name}")
            }
            println("${scenarios.size} scenarios, $shots pictures, ${failures.size} failed")
            return failures
        } finally {
            scratch.deleteRecursively()
            Runtime.getRuntime().removeShutdownHook(cleanup)
        }
    }

    /** The simulator, built before any scenario needs it. Two runs of one checkout at once
     * build it one after the other: the lock is the file's, so it holds between programs. */
    private fun simulatorBinary(): String {
        val folder = File(root, "tools/simulator")
        val lock = File(folder, "build/.sim-test.lock").apply { parentFile.mkdirs() }
        RandomAccessFile(lock, "rw").channel.use { channel ->
            channel.lock().use {
                if (ProcessBuilder("make", "-s", "-C", folder.path).inheritIO().start().waitFor() != 0) {
                    throw BuildError("the simulator could not be built: it needs a C compiler")
                }
            }
        }
        return File(folder, "build/perinkpheral-sim").path
    }

    /** Every application the scenarios name, built once, before the first simulator starts:
     * the compiler is the one part of a run that keeps the processors busy, and a scenario
     * waits in real time. An application that does not build fails its scenarios, each with
     * the build's words. */
    private fun build(appDirs: List<File>, into: File, workers: Int): Map<File, Result<Package>> {
        val packages = HashMap<File, Result<Package>>()
        val numbered = appDirs.withIndex().toList()
        InOrder.run(numbered, minOf(workers, Runtime.getRuntime().availableProcessors()), work = { (number, appDir) ->
            val name = Applications.nameOf(appDir)
            val folder = File(into, number.toString())
            builder().build(appDir, folder)
            Package(name, folder)
        }) { (_, appDir), result -> packages[appDir] = result }
        return packages
    }

    @Suppress("UNCHECKED_CAST")
    private fun runOne(test: Scenario, packages: Map<File, Result<Package>>, state: File, binary: String,
                       update: Boolean): Int {
        val folder = test.folder
        val scenario = test.steps.getOrThrow()
        val built = packages[test.appDir ?: throw BuildError("no \"app\"")]!!.getOrThrow()
        val name = built.name
        try {
            Applications.install(built.folder, name, state)
            val store = File(state, "store/$name").apply { mkdirs() }
            (scenario["store"] as? Map<String, Any?>).orEmpty().forEach { (key, value) ->
                File(store, key.toByteArray().joinToString("") { "%02x".format(it) }).writeText(value.toString())
            }
            // The simulator binds a port of the system's choice and says which: one chosen here
            // and bound there could be taken in between.
            val portFile = File(state, "sim.port")
            val command = mutableListOf(binary, "--apps", File(state, "apps").path, "--app", name, "--state", state.path,
                "--port", SimulatorPort.ANY.toString(), "--port-file", portFile.path, "--language", scenario["language"] as? String ?: "en",
                "--font", scenario["font"] as? String ?: "departure", "--fetcher-port", SimulatorNetwork.port().toString())
            (scenario["at"] as? Long)?.let { command += listOf("--at", it.toString()) }
            if (scenario["clock"] == true) command += "--clock"
            (scenario["glyphs"] as? String)?.let { command += listOf("--glyphs", File(it).absolutePath) }
            when (val mocks = scenario["mocks"]) {
                // a file beside the scenario, whose "file"s are beside it too
                is String -> command += listOf("--mocks", File(folder, mocks).absolutePath)
                is Map<*, *> -> command += listOf("--mocks", File(state, "mocks.json").apply { writeText(Json.write(resolveInlineMocks(mocks, folder))) }.path)
                else -> if (scenario.containsKey("mocks")) throw BuildError("\"mocks\" must be an object or the name of a file")
            }
            val builderOfProcess = ProcessBuilder(command).redirectErrorStream(true).redirectOutput(File(state, "sim.log"))
            builderOfProcess.environment()["TZ"] = scenario["tz"] as? String ?: "UTC"
            val process = builderOfProcess.start()
            simulators += process
            try {
                val client = waitForStart(portFile, process, File(state, "sim.log"))
                var shots = 0
                for (step in (scenario["steps"] as? List<String>).orEmpty()) {
                    val words = step.trim().split(Regex("\\s+"))
                    when (words[0]) {
                        "wait" -> Thread.sleep((words[1].toDouble() * 1000).toLong())
                        "forward" -> requireCommand("$step", client.raw("/api/sim/forward?seconds=${words[1]}", "POST"))
                        "press" -> for (key in words.drop(1)) {
                            if (!Regex("[1-4][Ll]?").matches(key)) throw BuildError("a key is 1-4 or 1L-4L: $key")
                            val seen = client.screen().number
                            client.press(DeviceButton.entries[key[0] - '1'], key.length == 2)
                            client.settledScreen(seen)
                        }
                        "shot" -> {
                            check(client.screen().rows, File(folder, "${words[1]}.png"), update)
                            shots++
                        }
                        else -> throw BuildError("a step is wait, press, forward or shot: $step")
                    }
                }
                return shots
            } finally {
                process.destroy()
                process.waitFor()
                simulators -= process
            }
        } finally {
            state.deleteRecursively()
        }
    }

    private fun waitForStart(portFile: File, process: Process, log: File): DeviceClient {
        val deadline = System.currentTimeMillis() + START_TIMEOUT_MS
        var client: DeviceClient? = null
        while (System.currentTimeMillis() < deadline) {
            if (!process.isAlive) throw BuildError("the simulator stopped: ${log.readText().takeLast(400)}")
            client = client ?: SimulatorPort.read(portFile)?.let { DeviceClient("127.0.0.1:$it") }
            if (client != null && runCatching { client.status() }.isSuccess) {
                // As a device after its start: the first frame and the first answers in.
                Thread.sleep(SETTLE_MS)
                return client
            }
            Thread.sleep(POLL_MS)
        }
        throw BuildError("the simulator did not answer in ${START_TIMEOUT_MS / 1000} s")
    }

    private fun check(rows: List<BooleanArray>, expected: File, update: Boolean) {
        val actual = File(expected.parentFile, expected.nameWithoutExtension + ".actual.png")
        if (update || !expected.isFile) {
            Bmp.writePng(expected, rows, 1)
            actual.delete()
            if (!update) throw BuildError("${expected.name} was not there: written now, look at it and keep it")
            return
        }
        val image = ImageIO.read(expected)
        val want = List(image.height) { y -> BooleanArray(image.width) { x -> luminance(image.getRGB(x, y)) < INK_BELOW } }
        val differing = if (want.size != rows.size || want[0].size != rows[0].size) -1
        else rows.indices.sumOf { y -> rows[y].indices.count { x -> rows[y][x] != want[y][x] } }
        if (differing != 0) {
            Bmp.writePng(actual, rows, 1)
            throw BuildError("${expected.name}: ${if (differing < 0) "another size" else "$differing pixels differ"} - see ${actual.name}")
        }
        actual.delete()
    }

    private fun luminance(rgb: Int): Int = ((rgb shr 16 and 0xFF) * 299 + (rgb shr 8 and 0xFF) * 587 + (rgb and 0xFF) * 114) / 1000

    companion object {
        /** How many scenarios run at once when nobody says. */
        fun defaultWorkers(): Int = Runtime.getRuntime().availableProcessors()

        private const val START_TIMEOUT_MS = 60_000L
        private const val SETTLE_MS = 6_000L
        private const val POLL_MS = 300L
        private const val INK_BELOW = 128
    }
}

/** Validated before building applications or starting any simulator. */
internal fun readScenario(file: File): Map<String, Any?> {
    val value = Json.parse(file.readText()) as? Map<*, *> ?: throw BuildError("scenario must be an object")
    fun invalid(field: String): Nothing = throw BuildError("invalid scenario field: $field")
    if (value["app"] !is String || (value["app"] as String).isBlank()) invalid("app")
    for (key in listOf("tz", "glyphs")) if (key in value && (value[key] !is String || (value[key] as String).isBlank())) invalid(key)
    if ("language" in value && value["language"] !in listOf("en", "ru")) invalid("language")
    if ("font" in value && value["font"] !in listOf("departure", "jersey")) invalid("font")
    for (key in listOf("clock", "serial")) if (key in value && value[key] !is Boolean) invalid(key)
    if ("at" in value && value["at"] !is Long) invalid("at")
    if ("store" in value && (value["store"] !is Map<*, *> || (value["store"] as Map<*, *>).any {
            it.key !is String || (it.key as String).isEmpty() || it.value !is String
        })) invalid("store")
    if ("mocks" in value && value["mocks"] !is String && value["mocks"] !is Map<*, *>)
        throw BuildError("\"mocks\" must be an object or the name of a file")
    val steps = value["steps"] as? List<*> ?: invalid("steps")
    if (steps.isEmpty()) invalid("steps: at least one step is required")
    for (step in steps) {
        if (step !is String) invalid("steps: each step must be text")
        val words = step.trim().split(Regex("\\s+"))
        val valid = when (words[0]) {
            "wait" -> words.size == 2 && words[1].toDoubleOrNull()?.let {
                it.isFinite() && it >= 0 && it <= Long.MAX_VALUE / 1000
            } == true
            "forward" -> words.size == 2 && Regex("[0-9]+").matches(words[1]) &&
                words[1].toLongOrNull()?.let { it in 1..Int.MAX_VALUE.toLong() } == true
            "press" -> words.size > 1 && words.drop(1).all { Regex("[1-4][Ll]?").matches(it) }
            "shot" -> words.size == 2 && Regex("[a-zA-Z0-9_-][a-zA-Z0-9_.-]*").matches(words[1])
            else -> false
        }
        if (!valid) invalid("steps: $step")
    }
    @Suppress("UNCHECKED_CAST")
    return value as Map<String, Any?>
}

internal fun resolveInlineMocks(mocks: Map<*, *>, folder: File): Map<*, *> = mocks.mapValues { (url, value) ->
    val mock = value as? Map<*, *> ?: throw BuildError("mock $url must be an object")
    if ("file" !in mock) mock else {
        val path = mock["file"] as? String ?: throw BuildError("mock $url: file must be text")
        val resolved = File(path).let { if (it.isAbsolute) it else File(folder, path) }.canonicalFile
        if (!resolved.isFile || !resolved.canRead()) throw BuildError("mock file ${resolved.path} cannot be read")
        mock + ("file" to resolved.path)
    }
}

internal fun requireCommand(step: String, answer: DeviceClient.Answer) {
    if (answer.status !in 200..299) throw BuildError("$step: simulator returned ${answer.status}: ${String(answer.body, Charsets.UTF_8)}")
}
