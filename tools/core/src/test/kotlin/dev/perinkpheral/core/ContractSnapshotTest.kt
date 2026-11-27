// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.contract.ContractSnapshot
import java.io.File
import java.nio.file.Files

object ContractSnapshotTest {
    private const val ABI_3_RELEASE = "0.2.0"

    @JvmStatic fun main(args: Array<String>) {
        val wasi = System.getenv("WASI_SDK_PATH")?.let(::File) ?: run {
            println("Skipping contract snapshot regressions: WASI_SDK_PATH is not set")
            return
        }
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-contract-test").toFile()
        try {
            File(root, "sdk").copyRecursively(File(scratch, "sdk"))
            File(root, "VERSION").copyTo(File(scratch, "VERSION"))
            val api = File(scratch, "sdk/host_api.h")
            val original = api.readText()
            val snapshot = File(scratch, "sdk/abi/3.txt")
            val baseline = snapshot.readText()
            fun run(check: Boolean = false, unfrozen: Boolean = false) =
                ContractSnapshot.run(Sdk(scratch), wasi, snapshot, check, unfrozen)
            fun refused(message: String, check: Boolean = false, unfrozen: Boolean = false) {
                val before = snapshot.readText()
                try { run(check, unfrozen); error("invalid contract accepted") }
                catch (error: BuildError) { check(message in error.message.orEmpty()) { error.message.orEmpty() } }
                check(snapshot.readText() == before)
            }
            run(true) // Established unstamped baseline stays valid.
            val sdk = Sdk(scratch)
            val release = sdk.firmwareFirst
            val importMinimum = sdk.firmwareFor(sdk.imports()).first
            check(release == ABI_3_RELEASE)
            check(release == sdk.contract.string("HOST_ABI_3_FIRMWARE_FIRST"))
            api.writeText("$original\n#define HOST_RELEASE_BASELINE_TEST 19\n")
            refused("HOST_SINCE_HOST_RELEASE_BASELINE_TEST")
            refused("HOST_SINCE_HOST_RELEASE_BASELINE_TEST", check = true, unfrozen = true)
            if (sdk.version == release) {
                run(unfrozen = true)
                run(true)
                check("constant HOST_RELEASE_BASELINE_TEST\t19" in snapshot.readText())
            } else {
                refused("HOST_SINCE_HOST_RELEASE_BASELINE_TEST", unfrozen = true)
            }
            api.writeText(original)
            snapshot.writeText(baseline)
            val parts = File(scratch, "VERSION").readText().trim().split('.').map(String::toInt)
            val version = "${parts[0]}.${parts[1]}.${parts[2] + 1}"
            File(scratch, "VERSION").writeText(version)
            check(Sdk(scratch).firmwareFor(emptyList()).first == release)
            check(Sdk(scratch).firmwareFor(Sdk(scratch).imports()).first == importMinimum)
            for ((name, declaration) in listOf(
                "test_added_import" to "HOST_IMPORT(test_added_import, int32_t test_added_import(void));",
                "app_test_added_export" to "APP_EXPORT(app_test_added_export, void app_test_added_export(void));",
                "HOST_TEST_ADDED_CONSTANT" to "#define HOST_TEST_ADDED_CONSTANT 17"
            )) {
                api.writeText("$original\n$declaration\n")
                refused("HOST_SINCE_$name")
                refused("HOST_SINCE_$name", true)
                refused("HOST_SINCE_$name", unfrozen = true)
                for (invalid in listOf("not-a-version", release, "999.999.999")) {
                    api.writeText("$original\n#define HOST_SINCE_$name \"$invalid\"\n$declaration\n")
                    refused("HOST_SINCE_$name")
                }
                api.writeText("$original\n#define HOST_SINCE_$name \"$version\"\n$declaration\n")
                for (invalid in listOf("\"$version\" trailing", "\"$version\" \"junk\"", version)) {
                    api.writeText("$original\n#define HOST_SINCE_$name $invalid\n$declaration\n")
                    refused("HOST_SINCE_$name")
                }
                api.writeText("$original\n#define HOST_SINCE_$name \"$version\"\n$declaration\n")
                check(Sdk(scratch).firmwareFor(listOf(name)) == (version to name))
                run()
                run(true)
                // Once recorded, its stamp cannot be removed or changed.
                val recorded = snapshot.readText()
                api.writeText("$original\n$declaration\n")
                try { run(); error("recorded stamp removed") } catch (error: BuildError) {
                    check("Frozen ABI changed" in error.message.orEmpty())
                }
                check(snapshot.readText() == recorded)
                snapshot.writeText(baseline)
            }
            api.writeText(original)
            // A future manifest feature still raises the minimum without a new import.
            api.writeText("$original\n#define HOST_SINCE_POLL_METHOD \"$version\"\n")
            val future = Sdk(scratch)
            val features = future.manifestFeatures(mapOf("polls" to listOf(mapOf("method" to "POST"))))
            check(future.firmwareFor(features) == (version to "POLL_METHOD"))
            api.writeText(original)
            for (name in ContractSnapshot.growableLimits) {
                val key = "constant $name"
                val line = baseline.lineSequence().single { it.startsWith("$key\t") }
                val value = line.substringAfter('\t').toLong()
                check(value > 1)
                snapshot.writeText(baseline.replace(line, "$key\t${value - 1}"))
                run(true) // Previously valid smaller requests remain accepted.
                run() // Ordinary recording accepts and records growth.
                check(snapshot.readText() == baseline)
                snapshot.writeText(baseline.replace(line, "$key\t${value + 1}"))
                refused(key)
                refused(key, true)
                snapshot.writeText(baseline)
            }
            val app = File(scratch, "sdk/host/app.h")
            val application = app.readText()
            app.writeText(application.replace(
                Regex("""^#define[ \t]+HOST_SESSION_MAX[ \t]+[^\n]+\n""", RegexOption.MULTILINE), ""))
            refused("constant HOST_SESSION_MAX")
            refused("constant HOST_SESSION_MAX", true)
            app.writeText(application)
            for (name in listOf("HOST_DATA_CHUNK_MAX", "SYS_SETTING_MAX", "HOST_ICON_CELLS",
                "HOST_TEXT_MAX", "HOST_VALUE_MAX", "HOST_LIST_BYTES", "HOST_HEADER_TEXT_MAX",
                "MANIFEST_SETTING_VALUE_MAX", "MANIFEST_ICON_CELLS")) {
                val key = "constant $name"
                val line = baseline.lineSequence().single { it.startsWith("$key\t") }
                val value = line.substringAfter('\t').toLong()
                for (changed in listOf(value - 1, value + 1)) {
                    snapshot.writeText(baseline.replace(line, "$key\t$changed"))
                    refused(key, true)
                    refused(key)
                }
                snapshot.writeText(baseline)
            }
            api.writeText(original)
            val net = File(scratch, "sdk/host/net.h")
            val network = net.readText()
            val extended = network.replace("} host_answer_info_t;", "    int32_t appended;\n} host_answer_info_t;")
                .replace("HOST_LAYOUT(host_answer_info_t, 476)", "HOST_LAYOUT(host_answer_info_t, 480)")
            net.writeText(extended)
            run() // No --unfrozen: a bounded prefix copy remains compatible.
            run(true)
            val recordedPrefix = baseline.lineSequence().filter { it.startsWith("field host_answer_info_t.") ||
                it.startsWith("struct host_answer_info_t align") }.toSet()
            check(snapshot.readLines().containsAll(recordedPrefix))
            snapshot.writeText(baseline)
            for (invalid in listOf(
                extended.replace("int32_t status;", "uint32_t status;"),
                extended.replace("int32_t status;", "int32_t swapped;").replace("int32_t length;", "int32_t status;").replace("int32_t swapped;", "int32_t length;"),
                network.replace("char    etag[64]", "char    etag[68]")
                    .replace("HOST_LAYOUT(host_answer_info_t, 476)", "HOST_LAYOUT(host_answer_info_t, 480)"),
                network.replace("} host_answer_info_t;", "    int64_t appended;\n} host_answer_info_t;")
                    .replace("HOST_LAYOUT(host_answer_info_t, 476)", "HOST_LAYOUT(host_answer_info_t, 488)")
            )) {
                net.writeText(invalid)
                refused("Frozen ABI changed")
            }
            net.writeText(network)
            val ui = File(scratch, "sdk/host/ui.h")
            ui.writeText(ui.readText().replace("} host_list_t;", "    int32_t appended;\n} host_list_t;")
                .replace("HOST_LAYOUT(host_list_t, 20)", "HOST_LAYOUT(host_list_t, 24)"))
            refused("Frozen ABI changed")
        } finally { scratch.deleteRecursively() }
        println("Contract snapshot regressions passed: release floor, introduction stamps, capacity growth and response metadata")
    }
}
