// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.app.Applications
import dev.perinkpheral.core.build.Certificates
import dev.perinkpheral.core.build.ManifestCheck
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.check.AppCheck
import java.io.File
import java.nio.file.Files
import java.util.Base64

object SdkDevexTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val sdk = Sdk(root)
        val scratch = Files.createTempDirectory("perinkpheral-sdk-devex").toFile()
        try {
            val app = File(scratch, "probe").apply { mkdirs() }
            val corpus = Json.parse(File(root, "firmware/test/host/manifest_validation.json").readText()) as List<*>
            for (entry in corpus) {
                entry as Map<*, *>
                File(app, "manifest.json").writeText(entry["manifest"] as String)
                val accepted = runCatching { ManifestCheck(sdk).check(app) }.isSuccess
                check(accepted == entry["valid"]) { "manifest validation differs: $entry" }
            }
            for ((text, seconds) in mapOf("30 seconds" to 30L, "2 minutes" to 120L, "1 hour" to 3600L,
                    "1 day" to 86400L, "30 s!" to 0L, "30 smuggled" to 0L, "30 minutes!" to 0L)) {
                check(ManifestCheck.paceSeconds(text) == seconds) { text }
            }
            val pem = File(root, "firmware/test/mbedtls/fixtures/test-root.pem").readText()
            val certificate = File(scratch, "root.pem")
            certificate.writeText(pem)
            val der = Certificates.read(certificate, 8192).encoded
            certificate.writeBytes(der)
            Certificates.read(certificate, 8192)
            val encoded = Base64.getEncoder().encodeToString(der)
            for (bad in listOf((pem + pem).toByteArray(), (pem + "garbage").toByteArray(),
                    ("garbage\n" + pem).toByteArray(), der + der, der + byteArrayOf(0), der.dropLast(1).toByteArray(),
                    ("-----BEGIN CERTIFICATE-----\n$encoded\n").toByteArray())) {
                certificate.writeBytes(bad)
                check(runCatching { Certificates.read(certificate, 8192) }.exceptionOrNull() is BuildError)
            }
            val wasi = System.getenv("WASI_SDK_PATH")?.let(::File)
            val checker = AppCheck(sdk, wasi ?: File("unused")) {}
            for (code in listOf(1, -1, 139)) {
                val lines = checker.capabilityLines("probe", listOf("store"), code to "check store ok\nsanitizer failed")
                check(lines.single().state == AppCheck.State.FAILED)
                check(lines.single().details.any { "sanitizer failed" in it })
            }
            check(checker.capabilityLines("probe", listOf("store"), 0 to "check store ok").single().state == AppCheck.State.OK)
            val mixed = checker.capabilityLines("probe", listOf("store", "drawing"),
                1 to "check store fail\nproblem store save refused\ncheck drawing ok")
            check(mixed.map { it.state } == listOf(AppCheck.State.FAILED, AppCheck.State.OK))
            check(mixed.first().details == listOf("save refused"))
            val diagnostic = checker.capabilityLines("probe", listOf("store", "drawing"),
                1 to "check store fail\ncheck drawing ok\nsanitizer failed")
            check("sanitizer failed" in diagnostic.first().details)
            val crashed = checker.capabilityLines("probe", listOf("store", "drawing"),
                139 to "check store fail\ncheck drawing ok\nsanitizer failed")
            check(crashed.all { it.state == AppCheck.State.FAILED })
            if (wasi != null) {
                val guideApp = File(scratch, "guide/hello").apply { mkdirs() }
                val guide = File(root, "docs/guide/source/guide.html").readText()
                val example = guide.substringAfter("<h2 id=\"start-sample\">")
                    .substringAfter("<pre>").substringBefore("</pre>")
                    .replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&")
                File(guideApp, "hello.c").writeText("/* SPDX-License-Identifier: MIT */\n$example\n")
                File(root, "sdk/apps/hello/manifest.json").copyTo(File(guideApp, "manifest.json"))
                File(guideApp, "test").mkdirs()
                File(guideApp, "test/restore.c").writeText("""
                    /* SPDX-License-Identifier: MIT */
                    #include "host_api.h"
                    #include "standin.h"
                    #include <assert.h>
                    #include <string.h>
                    static int32_t failed_restore(const char *key, int32_t key_length, void *out, int32_t capacity)
                    {
                        (void)key; (void)key_length; (void)out; (void)capacity;
                        return HOST_ERR_FAILED;
                    }
                    #define kv_get failed_restore
                    #include "../hello.c"
                    #undef kv_get
                    int main(void)
                    {
                        standin_reset();
                        int32_t saved = 42, restored = 0;
                        assert(kv_set("presses", 7, &saved, sizeof saved) == HOST_OK);
                        int32_t writes = standin()->store_writes;
                        app_on_load();
                        app_on_button(BTN_UP_PRESSED);
                        assert(standin()->store_writes == writes);
                        assert(kv_get("presses", 7, &restored, sizeof restored) == sizeof restored && restored == saved);
                        standin()->is_drawing = true;
                        app_on_draw();
                        assert(strstr(standin()->draw_text, "Storage error.") != NULL);
                        assert(strstr(standin()->draw_text, "Presses:") == NULL);
                        return 0;
                    }
                """.trimIndent())
                val guideReport = checker.run(guideApp, only = "logic")
                check(guideReport.failed.isEmpty()) { guideReport.failed.joinToString("\n") { it.details.joinToString("\n") } }
                val fresh = File(scratch, "fresh")
                Applications.create(root, fresh, "Fresh", "Test Author") { "" }
                File(fresh, "strings").mkdirs()
                File(fresh, "strings/en.json").writeText("{}")
                File(fresh, "test").mkdirs()
                File(fresh, "test/persistence.c").writeText("""
                    /* SPDX-License-Identifier: MIT */
                    #include "standin.h"
                    #include "../fresh.c"
                    #include "strings.h"
                    #include <strings.h>
                    #include <assert.h>
                    int main(void)
                    {
                        assert(strcasecmp("AbC", "abc") == 0);
                        standin_reset();
                        standin_set("presses", "x");
                        app_on_load();
                        assert(presses == 0);
                        app_on_button(BTN_UP_PRESSED);
                        assert(presses == 1);
                        standin()->refuse_writes = HOST_ERR_FAILED;
                        app_on_button(BTN_UP_PRESSED);
                        assert(presses == 1 && storage_failed);
                        standin_draw();
                        standin()->refuse_writes = 0;
                        app_on_load();
                        assert(presses == 1);
                        app_on_button(BTN_UP_PRESSED);
                        assert(presses == 2 && !storage_failed);
                        return 0;
                    }
                """.trimIndent())
                val report = checker.run(fresh)
                check(report.failed.isEmpty()) { report.failed.joinToString("\n") { it.what + ": " + it.details.joinToString("\n") } }
            }
        } finally { scratch.deleteRecursively() }
        println("SDK development: manifest parity, duration grammar, certificate boundaries, process failures and starter checks passed")
    }
}
