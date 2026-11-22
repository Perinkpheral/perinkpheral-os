// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import java.io.File
import java.nio.file.Files
import java.util.zip.GZIPInputStream

/** Exercise delayed app reads against the assembled page shipped in the firmware. */
object PageTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "firmware/main/web/page.html").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-page-test").toFile()
        try {
            val page = File(scratch, "page.html")
            val packed = File(scratch, "page.html.gz")
            FirmwareTools.page(File(root, "firmware/main/web"), page, packed)
            val unpacked = GZIPInputStream(packed.inputStream()).use { it.readBytes() }
            check(unpacked.contentEquals(page.readBytes())) { "The served page differs from the assembled page" }
            for (test in listOf("test_settings_drafts.js", "test_app_navigation.js")) {
                val process = ProcessBuilder("node", File(root, "firmware/test/web/$test").path)
                    .directory(root).redirectErrorStream(true).apply {
                        environment()["PERINKPHERAL_TEST_PAGE"] = page.path
                    }.start()
                val output = process.inputStream.bufferedReader().use { it.readText() }
                check(process.waitFor() == 0) { "$test failed on the assembled page:\n$output" }
                print(output)
            }
        } finally {
            scratch.deleteRecursively()
        }
    }
}
