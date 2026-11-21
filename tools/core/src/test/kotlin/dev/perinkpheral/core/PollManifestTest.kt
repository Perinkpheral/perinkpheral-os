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
        val introduced = sdk.contract.string("HOST_SINCE_POLL_METHOD")!!
        check(sdk.callSince.getValue("POLL_METHOD") == introduced)
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
            fun refused(poll: Map<String, Any?>) {
                manifest(poll)
                try { ManifestCheck(sdk).check(app); error("invalid poll accepted: $poll") }
                catch (_: BuildError) { }
            }
            accepted(get)
            accepted(get + ("method" to "GET"))
            check(sdk.manifestFeatures(manifest(get)).isEmpty())
            check(sdk.manifestFeatures(manifest(get + ("method" to "GET"))).isEmpty())
            for (method in listOf("POST", "PUT", "DELETE", "PATCH", "HEAD", "PROPFIND", "REPORT")) {
                val poll = get + ("method" to method)
                accepted(poll)
                check(sdk.firmwareFor(sdk.manifestFeatures(manifest(poll))).first == introduced)
                if (method != "HEAD") accepted(poll + ("body" to "{{undeclared}}"))
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

            val wasi = System.getenv("WASI_SDK_PATH")?.let(::File)
            if (wasi != null) {
                File(app, "probe.c").writeText("""
                    /* SPDX-License-Identifier: MIT */
                    #include "host_api.h"
                    void app_on_load(void) {}
                    void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status) { (void)request; (void)data; (void)length; (void)flags; (void)status; }
                    void app_on_picked(int32_t request, int32_t status) { (void)request; (void)status; }
                """.trimIndent() + "\n")
                val builder = AppBuilder(sdk, wasi) {}
                val out = File(scratch, "built")
                fun builtMinimum(poll: Map<String, Any?>, minimum: String? = null): String {
                    manifest(poll, minimum)
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
                }
                val post = get + mapOf("method" to "POST", "body" to "request", "pick" to listOf("value"))
                check(builtMinimum(post, floor) == introduced)
                check(builtMinimum(get, introduced) == introduced)
                manifest(post)
                val catalog = File(scratch, "catalog")
                Catalog.make(builder, listOf(app.parentFile), catalog)
                val index = Json.parse(File(catalog, "catalog.json").readText()) as Map<*, *>
                check(((index["apps"] as List<*>).single() as Map<*, *>)["firmware"] == introduced)

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
