// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files
import java.util.concurrent.TimeUnit

/** The CLI's simulator uses the same counted-text and XML vectors as both host suites. */
object XmlTextTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val wasi = File(System.getenv("WASI_SDK_PATH") ?: error("WASI_SDK_PATH is required"))
        val scratch = Files.createTempDirectory("perinkpheral-xml-text").toFile()
        try {
            val native = File(root, "tools/simulator")
            val buildLog = File(scratch, "build.log")
            val make = ProcessBuilder("make").directory(native).redirectErrorStream(true).redirectOutput(buildLog).start()
            check(make.waitFor(120, TimeUnit.SECONDS) && make.exitValue() == 0) { buildLog.readText() }
            val app = File(scratch, "probe").apply { mkdirs() }
            File(app, "manifest.json").writeText("""{"name":"probe","title":"Probe","abi":3}""")
            File(root, "firmware/test/host/text_cases.h").copyTo(File(app, "text_cases.h"))
            File(app, "main.c").writeText("""
                /* SPDX-License-Identifier: MIT */
                #include <stdbool.h>
                #include "host_api.h"
                #include "app_text.h"
                #include "text_cases.h"
                void app_on_draw(void)
                {
                    char output[8];
                    host_text_argument_t argument = {.text = "ok", .length = 2};
                    bool passed = text_contract_cases() &&
                        app_text_fill("{0}!", &argument, 1, output, sizeof output) == 3 &&
                        app_text_equal(output, "ok!");
                    log_write(HOST_LOG_INFO, passed ? "xml-text ok" : "xml-text failed", passed ? 11 : 15);
                }
            """.trimIndent() + "\n")
            val apps = File(scratch, "apps")
            AppBuilder(Sdk(root), wasi) { println(it) }.build(app, File(apps, "probe"))
            for (language in listOf("en", "ru")) {
                val log = File(scratch, "$language.log")
                val command = listOf(File(native, "build/perinkpheral-sim").path,
                    "--apps", apps.path, "--app", "probe", "--state", File(scratch, language).path,
                    "--port", "0", "--at", "1795255200", "--language", language)
                val process = ProcessBuilder(command).directory(root).redirectErrorStream(true).redirectOutput(log).start()
                try {
                    val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15)
                    while (true) {
                        val output = log.readText()
                        check("xml-text failed" !in output) { output }
                        if ("xml-text ok" in output) break
                        check(process.isAlive && System.nanoTime() < deadline) { output }
                        Thread.sleep(20)
                    }
                } finally {
                    process.destroy()
                    if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly().waitFor()
                }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("XML and counted text: simulator imports and terminated helper passed in English and Russian.")
    }
}
