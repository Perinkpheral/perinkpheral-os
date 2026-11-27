// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.ManifestCheck
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.catalog.Catalog
import dev.perinkpheral.core.check.AppCheck
import java.io.File
import java.nio.file.Files

object PollManifestTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val sdk = Sdk(root)
        val introduced = sdk.callSince["POLL_METHOD"] ?: sdk.firmwareFirst
        val scratch = Files.createTempDirectory("perinkpheral-poll-method").toFile()
        try {
            val app = File(scratch, "apps/probe").apply { mkdirs() }
            val manifestFile = File(app, "manifest.json")
            val base = linkedMapOf<String, Any?>("name" to "probe", "title" to "Probe", "author" to "Perinkpheral",
                "version" to "0.1.0", "abi" to 3L)
            val get = mapOf("url" to "https://example.test/data", "every" to 60L)
            fun manifest(poll: Map<String, Any?>, minimum: String? = null): Map<String, Any?> {
                val manifest = LinkedHashMap(base).apply {
                    put("polls", listOf(poll))
                    if (minimum != null) put("firmware", minimum)
                }
                manifestFile.writeText(Json.pretty(manifest))
                return manifest
            }
            fun accepted(poll: Map<String, Any?>) {
                manifest(poll)
                ManifestCheck(sdk).check(app)
            }
            fun refused(poll: Map<String, Any?>, message: String? = null) {
                manifest(poll)
                try { ManifestCheck(sdk).check(app); error("invalid poll accepted: $poll") }
                catch (error: BuildError) {
                    if (message != null) check(message in error.message.orEmpty()) { error.message.orEmpty() }
                }
            }
            refused(get + ("format" to "auto"))
            manifest(get, "0.1.102")
            ManifestCheck(sdk).check(app)
            accepted(get)
            accepted(get + ("method" to "GET"))
            val pick = get + ("pick" to listOf("value"))
            for (format in listOf("json","raw","xml")) {
                check(sdk.manifestFeatures(manifest(get + ("format" to format))).isEmpty())
                check(sdk.manifestFeatures(manifest(get) + ("format" to format)).isEmpty())
            }
            check(sdk.manifestFeatures(manifest(pick)).isEmpty())
            check(sdk.manifestFeatures(manifest(pick + ("format" to "json"))).isEmpty())
            check(sdk.manifestFeatures(manifest(pick) + ("format" to "json")).isEmpty())
            check(sdk.manifestFeatures(manifest(get)).isEmpty())
            check(sdk.manifestFeatures(manifest(get + ("method" to "GET"))).isEmpty())
            for (method in listOf("POST", "PUT", "DELETE", "PATCH", "HEAD", "PROPFIND", "REPORT")) {
                val poll = get + ("method" to method)
                accepted(poll)
                check(sdk.firmwareFor(sdk.manifestFeatures(manifest(poll))).first == introduced)
                if (method != "HEAD") accepted(poll + ("body" to "request"))
            }
            for (method in listOf(null, "get", "OPTIONS", "", 1L, emptyMap<String, Any>())) refused(get + ("method" to method))
            for (body in listOf(null, 1L, emptyList<Any>(), "x".repeat(1025), "é".repeat(513))) {
                refused(get + mapOf("method" to "POST", "body" to body))
            }
            for (body in listOf("", "x")) {
                refused(get + ("body" to body))
                refused(get + mapOf("method" to "GET", "body" to body))
                refused(get + mapOf("method" to "HEAD", "body" to body))
            }
            accepted(get + mapOf("method" to "POST", "body" to "x".repeat(1024)))
            accepted(get + mapOf("method" to "POST", "body" to "é".repeat(512)))
            check(sdk.manifestFeatures(manifest(get + mapOf("method" to "POST", "body" to ""))) == listOf("POLL_METHOD"))

            fun templates(text: String): List<Map<String, Any?>> = listOf(
                get + mapOf("method" to "POST", "body" to text),
                get + ("url" to "https://example.test/$text"),
                get + ("headers" to mapOf("Authorization" to text))
            )
            // Keys need not be declared settings; all printable non-space ASCII is allowed.
            for (text in listOf("{{undeclared}}", "{{x}}", "{{abcdefghijklmno}}", "{{!~._-+@/#:[]?}}", "{{a}}{{b}}", "plain { text }")) {
                templates(text).forEach(::accepted)
            }
            for (text in listOf("{{", "{{}}", "{{open", "{{abcdefghijklmnop}}", "{{two words}}", "{{é}}",
                    "{{a\n}}", "{{a\r}}", "{{a\t}}", "{{a\u007f}}", "{{valid}} then {{bad key}}", "{{ states('sensor.x') }}")) {
                templates(text).forEach { refused(it, "a literal \"{{\" cannot be sent") }
                refused(get + ("url" to text), "a literal \"{{\" cannot be sent")
            }

            val wasi = System.getenv("WASI_SDK_PATH")?.let(::File)
            if (wasi != null) {
                File(app, "probe.c").writeText("""
                    /* SPDX-License-Identifier: MIT */
                    #include "host_api.h"
                    void app_on_load(void) {}
                    void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status) { (void)request; (void)data; (void)length; (void)flags; (void)status; }
                    void app_on_picked(int32_t request, int32_t status) { (void)request; (void)status; }
                """.trimIndent() + "\n")
                val buildLines = mutableListOf<String>()
                val builder = AppBuilder(sdk, wasi) { buildLines.add(it) }
                val out = File(scratch, "built")
                fun builtMinimum(poll: Map<String, Any?>, minimum: String? = null): String {
                    manifest(poll, minimum)
                    buildLines.clear()
                    builder.build(app, out)
                    val packed = Json.parse(File(out, "manifest.json").readText()) as Map<*, *>
                    check(packed["polls"] == listOf(poll))
                    check(packed["abi"] == 3L)
                    return packed["firmware"] as String
                }
                val floor = sdk.firmwareFor(emptyList()).first
                check(builtMinimum(get) == floor)
                check(builtMinimum(get + ("method" to "GET")) == floor)
                for (method in listOf("POST", "HEAD", "REPORT")) {
                    check(builtMinimum(get + ("method" to method)) == introduced)
                    check(buildLines.any { "for a poll's method or body" in it } == (introduced != floor))
                    check(buildLines.none { "POLL_METHOD" in it })
                }
                val post = get + mapOf("method" to "POST", "body" to "request", "pick" to listOf("value"))
                check(builtMinimum(post, floor) == introduced)
                check(builtMinimum(get, introduced) == introduced)
                manifest(post)
                val catalog = File(scratch, "catalog")
                Catalog.make(builder, listOf(app.parentFile), catalog)
                val index = Json.parse(File(catalog, "catalog.json").readText()) as Map<*, *>
                check(((index["apps"] as List<*>).single() as Map<*, *>)["firmware"] == introduced)

                // A module may omit the raw handler only for a statically structured request or poll.
                val probe = File(app, "probe.c")
                val originalProbe = probe.readText()
                fun docOnly(default: String?, local: String?, sends: Boolean, allowed: Boolean) {
                    val poll = if (local == null) get else get + ("format" to local)
                    val source = manifest(poll).toMutableMap()
                    if (default != null) source["format"] = default
                    manifestFile.writeText(Json.pretty(source))
                    val send = if (sends) "http_send(HOST_REQUEST_FIRST, HOST_HTTP_GET, \"https://example.test/\", 21, 0, 0, 0, 0);" else ""
                    probe.writeText("""/* SPDX-License-Identifier: MIT */
                        #include "host_api.h"
                        void app_on_load(void) { $send }
                        void app_on_document(int32_t r,int32_t f,int32_t s,int32_t n) { (void)r; (void)f; (void)s; (void)n; }
                    """.trimIndent())
                    buildLines.clear()
                    val error = runCatching { builder.build(app,out) }.exceptionOrNull()
                    if (allowed) check(error == null) { error.toString() + buildLines.joinToString("\n") }
                    else check(error is BuildError && "app_on_data" in error.message.orEmpty()) { error.toString() + buildLines.joinToString("\n") }
                }
                docOnly(null, null, false, false)
                docOnly("json", "raw", false, false)
                docOnly(null, "json", true, false)
                docOnly("xml", "json", true, true)
                probe.writeText(originalProbe)
                // The same specimen exercises both data delivery modes with POST polls.
                val specimen = File(scratch, "native/specimen")
                File(root, "sdk/check/apps/specimen").copyRecursively(specimen)
                val file = File(specimen, "manifest.json")
                for ((picked, method) in listOf(true to "POST", false to "POST", true to "HEAD")) {
                    val original = Json.parse(File(root, "sdk/check/apps/specimen/manifest.json").readText()) as Map<*, *>
                    val polls = original["polls"] as List<*>
                    val poll = LinkedHashMap(polls.single() as Map<*, *>).apply {
                        put("method", method)
                        if (method != "HEAD") put("body", "value={{place}}")
                        if (!picked) remove("pick")
                    }
                    file.writeText(Json.pretty(LinkedHashMap(original).apply { put("polls", listOf(poll)) }))
                    if (!picked) {
                        val source = File(specimen, "specimen.c")
                        source.writeText(source.readText().replace("request != NOTE_REQUEST", "request != NOTE_REQUEST && request != 0"))
                    }
                    val report = AppCheck(sdk, wasi) {}.run(specimen, only = "network")
                    check(report.failed.isEmpty()) { report.lines.toString() }
                }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("Poll manifest: methods, body limits, build minimum, catalog minimum and POST capability delivery passed")
    }
}
