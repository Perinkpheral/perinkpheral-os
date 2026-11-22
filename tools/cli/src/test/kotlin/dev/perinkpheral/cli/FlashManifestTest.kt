// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files

/** Check the actual flasher artifacts, including the entire two-sector OTA journal. */
object FlashManifestTest {
    private val flashFiles = linkedMapOf(
        0L to "bootloader.bin", 0x8000L to "partition-table.bin",
        0xf000L to "ota_data_initial.bin", 0x20000L to "perink_recovery.bin",
        0x100000L to "perinkpheral.bin", 0x350000L to "apps.bin",
    )

    @JvmStatic fun main(args: Array<String>) {
        val scratch = Files.createTempDirectory("perinkpheral-flash-manifest-test").toFile()
        try {
            val build = File(scratch, "build").apply { mkdirs() }
            val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
                .first { File(it, "firmware/partitions.csv").isFile }
            val table = partitionTable(File(root, "firmware/partitions.csv"))
            for ((_, name) in flashFiles) {
                File(build, name).writeBytes(when (name) {
                    "partition-table.bin" -> table
                    "ota_data_initial.bin" -> ByteArray(0x2000) { 0xff.toByte() }
                    else -> name.toByteArray()
                })
            }
            File(build, "flasher_args.json").writeText(Json.write(mapOf(
                "flash_files" to flashFiles.entries.associate { (offset, name) -> "0x${offset.toString(16)}" to name },
                "flash_settings" to mapOf("flash_mode" to "dio", "flash_size" to "4MB", "flash_freq" to "80m"),
            )))
            val originals = flashFiles.values.associateWith { File(build, it).readBytes() }
            val out = File(scratch, "flash")
            for (prefix in listOf("", "debug-")) {
                FirmwareTools.flashManifest(build, out, "0.1.83-test", prefix)
                val fresh = manifest(File(out, "${prefix}manifest.json"))
                val migration = manifest(File(out, "${prefix}migration-manifest.json"))
                check(fresh["new_install_prompt_erase"] == true)
                check(migration["new_install_prompt_erase"] == false)
                check(fresh["version"] == "0.1.83-test" && migration["version"] == fresh["version"])
                check(fresh["flash"] == migration["flash"])
                val freshParts = parts(fresh)
                val migrationParts = parts(migration)
                val expectedMigration = flashFiles.filterKeys { it != 0x350000L }.mapValues { prefix + it.value }
                check(migrationParts == expectedMigration) { "Keep data must retain blank metadata and omit apps" }
                check(freshParts == flashFiles.mapValues { (offset, name) ->
                    prefix + if (offset == 0xf000L) "ota_data_first_install.bin" else name
                })
                val image = File(out, freshParts.getValue(0xf000L)).readBytes()
                checkOtaData(image)
                for ((name, bytes) in originals) {
                    check(File(build, name).readBytes().contentEquals(bytes)) { "Build input changed: $name" }
                    check(File(out, prefix + name).readBytes().contentEquals(bytes)) { "Copied asset changed: $name" }
                }
            }
            // Fail closed before deriving a first-install image from an unexpected journal/layout.
            val metadata = File(build, "ota_data_initial.bin")
            metadata.writeBytes(ByteArray(0x2000))
            refused { FirmwareTools.flashManifest(build, out, "test") }
            metadata.writeBytes(ByteArray(0x1000) { 0xff.toByte() })
            refused { FirmwareTools.flashManifest(build, out, "test") }
            metadata.writeBytes(originals.getValue(metadata.name))
            val alteredTable = table.copyOf()
            // factory is the fifth partition; moving it must not produce a boot selection.
            ByteBuffer.wrap(alteredTable).order(ByteOrder.LITTLE_ENDIAN).putInt(4 * 32 + 4, 0x30000)
            File(build, "partition-table.bin").writeBytes(alteredTable)
            refused { FirmwareTools.flashManifest(build, out, "test") }
            println("Flash manifests: fresh main on probation, erased fallback sector, Keep data and debug assets OK")
        } finally {
            scratch.deleteRecursively()
        }
    }

    private fun checkOtaData(image: ByteArray) {
        check(image.size == 0x2000)
        val entry = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN)
        check(entry.getInt(0) == 1) { "Sequence 1 must select ota_0" }
        check(entry.getInt(24) == 0) { "NEW must become PENDING_VERIFY at first boot" }
        // esp_rom_crc32_le(UINT32_MAX, {1,0,0,0}, 4), verified with IDF 5.5.5's C implementation.
        check(entry.getInt(28) == 0x4743989a)
        val expected = ByteArray(0x2000) { 0xff.toByte() }
        val record = listOf(
            0x01, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x00, 0x00, 0x00, 0x00, 0x9a, 0x98, 0x43, 0x47,
        )
        record.forEachIndexed { index, value -> expected[index] = value.toByte() }
        check(image.contentEquals(expected)) { "OTA image must match all 8192 bytes" }
        check(image.copyOfRange(0x1000, 0x2000).all { it == 0xff.toByte() }) {
            "An erased second entry must let failed probation return to factory without retrying main"
        }
        check(image.copyOfRange(64, 72).all { it == 0xff.toByte() }) { "No planned-update recovery hint" }
    }

    private fun manifest(file: File) = Json.parse(file.readText()) as Map<*, *>

    private fun parts(manifest: Map<*, *>): Map<Long, String> {
        val build = (manifest["builds"] as List<*>).single() as Map<*, *>
        check(build["chipFamily"] == "ESP32-C6")
        return (build["parts"] as List<*>).associate {
            val part = it as Map<*, *>
            (part["offset"] as Number).toLong() to part["path"] as String
        }
    }

    private fun partitionTable(csv: File): ByteArray {
        val rows = csv.readLines().filter { it.isNotBlank() && !it.trimStart().startsWith("#") }
        val bytes = ByteArray((rows.size + 1) * 32) { 0xff.toByte() }
        val table = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val subtypes = mapOf("nvs" to 2, "ota" to 0, "phy" to 1, "factory" to 0,
            "ota_0" to 0x10, "littlefs" to 0x83)
        rows.forEachIndexed { index, row ->
            val fields = row.split(',').map { it.trim() }
            val at = index * 32
            table.putShort(at, 0x50aa.toShort())
            table.put(at + 2, (if (fields[1] == "app") 0 else 1).toByte())
            table.put(at + 3, (subtypes[fields[2]] ?: fields[2].removePrefix("0x").toInt(16)).toByte())
            table.putInt(at + 4, fields[3].removePrefix("0x").toInt(16))
            table.putInt(at + 8, fields[4].removePrefix("0x").toInt(16))
            fields[0].toByteArray(Charsets.US_ASCII).copyInto(bytes, at + 12)
            bytes[at + 12 + fields[0].length] = 0
            table.putInt(at + 28, 0)
        }
        return bytes
    }

    private fun refused(action: () -> Unit) {
        try {
            action()
            error("Unsafe flash inputs were accepted")
        } catch (_: BuildError) {
            // The existing recovery-layout guards must still reject the inputs.
        }
    }
}
