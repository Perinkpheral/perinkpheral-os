// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import java.io.File
import java.nio.file.Files
import java.util.concurrent.TimeUnit
import java.util.jar.Attributes
import java.util.jar.JarEntry
import java.util.jar.JarOutputStream
import java.util.jar.Manifest
import javax.tools.ToolProvider

/** Hold a new, newer-than-sources archive half-written while a second launcher starts. */
object LauncherTest {
    private const val TIMEOUT_SECONDS = 15L

    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "tools/perinkpheral").isFile }
        for (cached in listOf(false, true)) race(root, cached)
        println("Launcher: concurrent builds, atomic publication, cached runs and failed builds OK")
    }

    private fun race(root: File, cached: Boolean) {
        val scratch = Files.createTempDirectory("perinkpheral launcher test ").toFile()
        val processes = ArrayList<Process>()
        try {
            val cli = File(scratch, "tools/cli").apply { mkdirs() }
            val build = File(cli, "build").apply { mkdirs() }
            val source = File(cli, "src/input.kt").apply { parentFile.mkdirs(); writeText("input") }
            File(scratch, "tools/core/src").mkdirs()
            for (project in listOf(cli, File(scratch, "tools/core"))) {
                File(project, "build.gradle.kts").writeText("// fixture")
            }
            val launcher = File(root, "tools/perinkpheral").copyTo(File(scratch, "tools/perinkpheral"))
            val fixture = executableJar(scratch)
            val jar = File(build, "libs/perinkpheral.jar")
            if (cached) {
                jar.parentFile.mkdirs()
                fixture.copyTo(jar)
                check(jar.setLastModified(1_000_000))
            }
            check(source.setLastModified(2_000_000))
            File(cli, "gradlew").apply {
                writeText("""
                    #!/bin/sh
                    # SPDX-License-Identifier: MIT
                    output=build/libs
                    for argument do
                        case "${'$'}argument" in
                            -PcliJarDirectory=*) output=${'$'}{argument#*=} ;;
                        esac
                    done
                    mkdir -p "${'$'}output"
                    printf 'build\n' >> build/builds
                    printf 'PK' > "${'$'}output/perinkpheral.jar"
                    : > build/partial-ready
                    while [ ! -f build/release ]; do sleep 0.05; done
                    [ ! -f build/fail ] || exit 1
                    cp ../../fixture.jar "${'$'}output/perinkpheral.jar"
                """.trimIndent() + "\n")
                check(setExecutable(true))
            }
            fun launch(name: String): Pair<Process, File> {
                val log = File(scratch, "$name.log")
                val process = ProcessBuilder("sh", launcher.path, "argument with spaces")
                    .redirectErrorStream(true).redirectOutput(log).apply {
                        environment()["JAVA_HOME"] = System.getProperty("java.home")
                    }.start()
                processes.add(process)
                return process to log
            }
            fun completed(run: Pair<Process, File>, success: Boolean = true) {
                check(run.first.waitFor(TIMEOUT_SECONDS, TimeUnit.SECONDS)) { "Launcher timed out: ${run.second.readText()}" }
                check((run.first.exitValue() == 0) == success) { run.second.readText() }
                if (success) check("launcher fixture:argument with spaces" in run.second.readLines()) {
                    run.second.readText()
                }
            }
            val first = launch("first")
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(TIMEOUT_SECONDS)
            while (!File(build, "partial-ready").isFile) {
                check(first.first.isAlive && System.nanoTime() < deadline) { first.second.readText() }
                Thread.sleep(10)
            }
            check(File(build, ".building").isDirectory)
            val second = launch("second")
            check(!second.first.waitFor(1_200, TimeUnit.MILLISECONDS)) {
                "Concurrent launcher read an unfinished JAR: ${second.second.readText()}"
            }
            if (cached) check(jar.readBytes().contentEquals(fixture.readBytes())) {
                "Build overwrote the published JAR before completion"
            } else check(!jar.exists()) { "Unfinished JAR was published" }
            File(build, "release").writeText("")
            completed(first)
            completed(second)
            check(File(build, "builds").readLines().size == 1) { "Concurrent launchers built twice" }
            completed(launch("cached"))
            check(File(build, "builds").readLines().size == 1) { "Cached invocation rebuilt" }

            val lock = File(build, ".building")
            check(lock.mkdir())
            val lockedCache = launch("locked-cache")
            try {
                check(!lockedCache.first.waitFor(1_200, TimeUnit.MILLISECONDS)) {
                    "Cached launcher bypassed an active build lock: ${lockedCache.second.readText()}"
                }
            } finally { check(lock.delete()) }
            completed(lockedCache)

            check(jar.setLastModified(1_000_000))
            File(build, "fail").writeText("")
            completed(launch("failed"), success = false)
            check(jar.readBytes().contentEquals(fixture.readBytes())) { "Failed build damaged the published JAR" }
            check(!File(build, ".building").exists()) { "Failed build left the lock held" }
            check(jar.parentFile.listFiles()!!.map { it.name } == listOf("perinkpheral.jar")) {
                "Build left temporary archives behind"
            }
            File(build, "fail").delete()
            completed(launch("retry"))
        } finally {
            File(scratch, "tools/cli/build/release").writeText("")
            for (process in processes) {
                if (!process.waitFor(TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                    process.descendants().forEach { it.destroyForcibly() }
                    process.destroyForcibly()
                    process.waitFor(TIMEOUT_SECONDS, TimeUnit.SECONDS)
                }
            }
            scratch.deleteRecursively()
        }
    }

    private fun executableJar(scratch: File): File {
        val source = File(scratch, "LauncherFixture.java").apply {
            writeText("""
                // SPDX-License-Identifier: MIT
                public class LauncherFixture {
                    public static void main(String[] args) {
                        System.out.println("launcher fixture:" + args[0]);
                    }
                }
            """.trimIndent())
        }
        check(ToolProvider.getSystemJavaCompiler().run(null, null, null, source.path) == 0)
        val manifest = Manifest().apply {
            mainAttributes[Attributes.Name.MANIFEST_VERSION] = "1.0"
            mainAttributes[Attributes.Name.MAIN_CLASS] = "LauncherFixture"
        }
        return File(scratch, "fixture.jar").apply {
            JarOutputStream(outputStream(), manifest).use { archive ->
                archive.putNextEntry(JarEntry("LauncherFixture.class"))
                archive.write(File(scratch, "LauncherFixture.class").readBytes())
                archive.closeEntry()
            }
        }
    }
}
