// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import dev.perinkpheral.core.device.DeviceClient
import java.io.File
import java.nio.file.Files

object SdkDevexTest {
    @JvmStatic fun main(args: Array<String>) {
        val scratch = Files.createTempDirectory("perinkpheral-cli-devex").toFile()
        try {
            val scenario = File(scratch, "scenario.json")
            val base = mapOf("app" to "apps/hello", "steps" to listOf("wait 0", "forward 60", "press 1 4L", "shot hello"))
            fun read(fields: Map<String, Any?>): Result<Map<String, Any?>> {
                scenario.writeText(Json.write(fields))
                return runCatching { readScenario(scenario) }
            }
            check(read(base).isSuccess)
            for (steps in listOf(null, "shot hello", emptyList<String>(), listOf(1), listOf("forward 0"),
                    listOf("wait NaN"), listOf("wait -1"), listOf("forward 1.5"), listOf("press"),
                    listOf("press 5"), listOf("shot ../escape"), listOf("shot one two"), listOf("wait 1 extra"))) {
                check(read(base + ("steps" to steps)).exceptionOrNull() is BuildError) { steps.toString() }
            }
            check(read(base - "steps").isFailure)
            for ((key, value) in mapOf("app" to 1, "at" to 1.5, "clock" to "true", "font" to "bad", "serial" to null,
                    "store" to mapOf("value" to null), "language" to false, "glyphs" to 7)) {
                check(read(base + (key to value)).isFailure) { key }
            }
            val fixture = File(scratch, "answer.json").apply { writeText("{}") }
            val resolved = resolveInlineMocks(mapOf("https://example.test/*" to mapOf("file" to fixture.name)), scratch)
            check((resolved.values.single() as Map<*, *>)["file"] == fixture.canonicalPath)
            val missing = runCatching { resolveInlineMocks(mapOf("url" to mapOf("file" to "absent.json")), scratch) }.exceptionOrNull()
            check(missing is BuildError && File(scratch, "absent.json").path in missing.message.orEmpty())
            val refused = runCatching { requireCommand("forward 1", DeviceClient.Answer(400, "rejected".toByteArray(), emptyMap())) }.exceptionOrNull()
            check(refused is BuildError && "400: rejected" in refused.message.orEmpty())
            requireCommand("forward 1", DeviceClient.Answer(200, byteArrayOf(), emptyMap()))
            val adversarial = "/tmp/quote\"\\\n</script><script>throw 1</script>\u2028"
            val template = SdkDevexTest::class.java.getResourceAsStream("/dev/perinkpheral/cli/page.html")!!
                .bufferedReader().use { it.readText() }
            val rendered = renderPage(template, adversarial)
            val literal = rendered.substringAfter("const APP_DIR = ").substringBefore(";\n")
            check("<" !in literal && "{{app_json}}" !in rendered)
            check(Json.parse(literal) == adversarial)
            val script = File(scratch, "page.js").apply {
                writeText(rendered.substringAfter("<script>").substringBefore("</script>"))
            }
            val syntax = ProcessBuilder("node", "--check", script.path).redirectErrorStream(true).start()
            val syntaxOutput = syntax.inputStream.bufferedReader().readText()
            check(syntax.waitFor() == 0) { syntaxOutput }
            val project = File(scratch, "portable/app").apply { mkdirs() }
            File(project, "CMakeLists.txt").writeText(cmakeLists("app"))
            fun sdk(name: String): File = File(scratch, name).apply {
                val module = File(this, "sdk/cmake/perinkpheral.cmake")
                module.parentFile.mkdirs()
                module.writeText("file(WRITE \"\${CMAKE_BINARY_DIR}/selected.txt\" \"$name\")\nfunction(perinkpheral_app dir)\nendfunction()\n")
            }
            val explicit = sdk("explicit")
            val environment = sdk("environment")
            sdk("portable/perinkpheral-os")
            fun configure(name: String, selected: File?, env: File?, expected: String?) {
                val build = File(scratch, "build-$name")
                val command = mutableListOf("cmake", "-S", project.path, "-B", build.path)
                if (selected != null) command += "-DPERINKPHERAL=${selected.path}"
                val process = ProcessBuilder(command).redirectErrorStream(true).apply {
                    environment().remove("PERINKPHERAL")
                    if (env != null) environment()["PERINKPHERAL"] = env.path
                }.start()
                val said = process.inputStream.bufferedReader().readText()
                val code = process.waitFor()
                if (expected == null) check(code != 0 && "-DPERINKPHERAL" in said) { said }
                else check(code == 0 && File(build, "selected.txt").readText() == expected) { said }
            }
            configure("explicit", explicit, environment, "explicit")
            configure("environment", null, environment, "environment")
            configure("default", null, null, "portable/perinkpheral-os")
            configure("missing", File(scratch, "missing"), environment, null)
        } finally { scratch.deleteRecursively() }
        println("CLI development: scenario validation, fixture paths, command refusals, script escaping and CMake relocation passed")
    }
}
