// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Pictures
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.build.Wasm
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.io.File
import java.nio.file.Files
import java.util.zip.CRC32
import java.util.zip.DeflaterOutputStream

object GraphicsContractTest {
    private fun png(colour: Int, depth: Int, transparency: ByteArray, samples: ByteArray): ByteArray {
        val bytes = ByteArrayOutputStream()
        val out = DataOutputStream(bytes)
        out.write(byteArrayOf(137.toByte(), 80, 78, 71, 13, 10, 26, 10))
        fun chunk(kind: String, body: ByteArray) {
            val name = kind.toByteArray(Charsets.US_ASCII)
            out.writeInt(body.size); out.write(name); out.write(body)
            val crc = CRC32().apply { update(name); update(body) }
            out.writeInt(crc.value.toInt())
        }
        val header = ByteArrayOutputStream().also {
            DataOutputStream(it).apply { writeInt(2); writeInt(1); writeByte(depth); writeByte(colour); write(byteArrayOf(0, 0, 0)) }
        }.toByteArray()
        chunk("IHDR", header)
        chunk("tRNS", transparency)
        val compressed = ByteArrayOutputStream()
        DeflaterOutputStream(compressed).use { it.write(0); it.write(samples) }
        chunk("IDAT", compressed.toByteArray())
        chunk("IEND", byteArrayOf())
        return bytes.toByteArray()
    }

    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-graphics-contract").toFile()
        try {
            val picture = File(scratch, "key.png")
            fun converted(colour: Int, depth: Int, key: ByteArray, samples: ByteArray, expected: String) {
                picture.writeBytes(png(colour, depth, key, samples))
                check(Pictures.show(listOf(picture)) == "key:\n$expected\n\n")
            }
            converted(0, 1, byteArrayOf(0, 0), byteArrayOf(0x40), "..")
            converted(0, 2, byteArrayOf(0, 1), byteArrayOf(0x40), ".#")
            converted(0, 4, byteArrayOf(0, 3), byteArrayOf(0x30), ".#")
            converted(0, 8, byteArrayOf(0, 32), byteArrayOf(32, 0), ".#")
            converted(0, 8, byteArrayOf(1, 32), byteArrayOf(32, 0), "##")
            converted(2, 8, byteArrayOf(0, 1, 0, 2, 0, 3), byteArrayOf(1, 2, 3, 1, 2, 4), ".#")
            converted(2, 8, byteArrayOf(1, 1, 0, 2, 0, 3), byteArrayOf(1, 2, 3, 1, 2, 4), "##")

            val sdk = Sdk(root)
            val graphics = sdk.contract.calls.map { it.name }.filter { it.startsWith("gfx_") || it.startsWith("ui_") }.toSet()
            check(graphics == AppBuilder.MEASURING_CALLS + AppBuilder.DRAWING_CALLS + "gfx_draw_qr")
            val wasi = System.getenv("WASI_SDK_PATH")
            if (wasi.isNullOrEmpty()) {
                println("Graphics: PNG transparency and exhaustive import classification passed; builder fixtures need WASI_SDK_PATH")
                return
            }
            val builder = AppBuilder(sdk, File(wasi)) {}
            val source = File(scratch, "geometry").apply { mkdirs() }
            File(source, "manifest.json").writeText("""{"name":"geometry","title":"Geometry","abi":3}""")
            val c = File(source, "geometry.c")
            val out = File(scratch, "out")
            val measuring = (AppBuilder.MEASURING_CALLS + "gfx_draw_qr").sorted()
            c.writeText("""
                // SPDX-License-Identifier: MIT
                #include "host_api.h"
                static void (*volatile imported)(void);
                void app_on_load(void) {
                ${measuring.joinToString("\n") { "    imported = (void (*)(void))$it;" }}
                }
            """.trimIndent())
            builder.build(source, out)
            check(Wasm(File(out, "app.wasm")).imports.map { it.second }.containsAll(measuring))
            for (call in AppBuilder.DRAWING_CALLS) {
                c.writeText("""
                    // SPDX-License-Identifier: MIT
                    #include "host_api.h"
                    static void (*volatile imported)(void);
                    void app_on_load(void) { imported = (void (*)(void))$call; }
                """.trimIndent())
                try {
                    builder.build(source, out)
                    error("$call without app_on_draw was accepted")
                } catch (error: BuildError) {
                    check("draws and has no app_on_draw" in error.message.orEmpty()) { error.message.orEmpty() }
                }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("Graphics: grey/RGB transparency keys, pure measurements, QR measurement import and drawing-handler requirements passed")
    }
}
