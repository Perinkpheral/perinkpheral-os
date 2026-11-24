// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.contract.ContractSnapshot
import java.io.File
import java.nio.file.Files

object ContractSnapshotTest {
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
            fun run(check: Boolean = false) = ContractSnapshot.run(Sdk(scratch), wasi, snapshot, check)
            fun refused(message: String, check: Boolean = false) {
                try { run(check); error("invalid contract accepted") }
                catch (error: BuildError) { check(message in error.message.orEmpty()) { error.message.orEmpty() } }
                check(snapshot.readText() == baseline)
            }
            run(true) // Established unstamped baseline stays valid.
            val version = File(scratch, "VERSION").readText().trim()
            for ((name, declaration) in listOf(
                "test_added_import" to "HOST_IMPORT(test_added_import, int32_t test_added_import(void));",
                "app_test_added_export" to "APP_EXPORT(app_test_added_export, void app_test_added_export(void));",
                "HOST_TEST_ADDED_CONSTANT" to "#define HOST_TEST_ADDED_CONSTANT 17"
            )) {
                api.writeText("$original\n$declaration\n")
                refused("HOST_SINCE_$name")
                refused("HOST_SINCE_$name", true)
                for (invalid in listOf("not-a-version", "0.1.30", "999.999.999")) {
                    api.writeText("$original\n#define HOST_SINCE_$name \"$invalid\"\n$declaration\n")
                    refused("HOST_SINCE_$name")
                }
                api.writeText("$original\n#define HOST_SINCE_$name \"$version\"\n$declaration\n")
                for (invalid in listOf("\"$version\" trailing", "\"$version\" \"junk\"", version)) {
                    api.writeText("$original\n#define HOST_SINCE_$name $invalid\n$declaration\n")
                    refused("HOST_SINCE_$name")
                }
                api.writeText("$original\n#define HOST_SINCE_$name \"$version\"\n$declaration\n")
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
        println("Contract snapshot regressions passed: stamps and append-only response metadata")
    }
}
