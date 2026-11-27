// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files

/** Exercise the public commands against a disposable contract and package. */
object ContractCommandTest {
    @JvmStatic fun main(args: Array<String>) {
        if (System.getenv("WASI_SDK_PATH").isNullOrEmpty()) {
            println("Skipping contract commands: WASI_SDK_PATH is not set")
            return
        }
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-contract-command-").toFile()
        try {
            File(root, "sdk").copyRecursively(File(scratch, "sdk"))
            File(root, "VERSION").copyTo(File(scratch, "VERSION"))
            fun command(success: Boolean, vararg words: String): String {
                val process = ProcessBuilder(File(System.getProperty("java.home"), "bin/java").path,
                    "-Dperinkpheral.root=${scratch.path}", "-cp", System.getProperty("java.class.path"),
                    "dev.perinkpheral.cli.MainKt", *words).redirectErrorStream(true).start()
                val output = process.inputStream.bufferedReader().use { it.readText() }
                check((process.waitFor() == 0) == success) { "${words.joinToString(" ")}: $output" }
                return output
            }
            command(true, "contract", "check")
            val appHeader = File(scratch, "sdk/host/app.h")
            val original = appHeader.readText()
            val sessionMax = Sdk(scratch).contract.number("HOST_SESSION_MAX")
            appHeader.writeText(original.replace(
                Regex("""^#define[ \t]+HOST_SESSION_MAX[ \t]+[^\n]+""", RegexOption.MULTILINE),
                "#define HOST_SESSION_MAX ${sessionMax + 1}"))
            check("constant HOST_SESSION_MAX" in command(false, "contract", "check"))
            command(true, "contract", "snapshot")
            command(true, "contract", "check")
            appHeader.writeText(original)
            check("constant HOST_SESSION_MAX" in command(false, "contract", "check"))
            check("constant HOST_SESSION_MAX" in command(false, "contract", "check", "--unfrozen"))
            File(root, "sdk/abi/3.txt").copyTo(File(scratch, "sdk/abi/3.txt"), overwrite = true)
            val net = File(scratch, "sdk/host/net.h")
            net.writeText(net.readText().replace("#define HOST_DATA_CHUNK_MAX 1024", "#define HOST_DATA_CHUNK_MAX 1025"))
            check("constant HOST_DATA_CHUNK_MAX" in command(false, "contract", "snapshot"))
            File(root, "sdk/host/net.h").copyTo(net, overwrite = true)
            val app = File(scratch, "apps/hello")
            File(root, "sdk/apps/hello").copyRecursively(app)
            val out = File(scratch, "package")
            command(true, "build", app.path, "--out", out.path)
            val manifest = Json.parse(File(out, "manifest.json").readText()) as Map<*, *>
            check(manifest["firmware"] == Sdk(root).firmwareFirst)
            val version = File(scratch, "VERSION")
            val parts = version.readText().trim().split('.').map(String::toInt)
            val later = "${parts[0]}.${parts[1]}.${parts[2] + 1}"
            version.writeText(later)
            command(true, "build", app.path, "--out", out.path)
            check((Json.parse(File(out, "manifest.json").readText()) as Map<*, *>)["firmware"] ==
                Sdk(root).firmwareFirst)
            // Later capacity use has an explicit minimum, independent of introduction stamps.
            val source = File(app, "manifest.json")
            val declared = Json.parse(source.readText()) as Map<*, *>
            source.writeText(Json.pretty(declared + ("firmware" to later)))
            command(true, "build", app.path, "--out", out.path)
            check((Json.parse(File(out, "manifest.json").readText()) as Map<*, *>)["firmware"] == later)
        } finally {
            scratch.deleteRecursively()
        }
        println("Contract commands: release floor, capacity growth, shrink rejection and fixed chunk size passed")
    }
}
