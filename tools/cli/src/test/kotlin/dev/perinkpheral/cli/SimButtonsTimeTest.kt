// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.Json
import dev.perinkpheral.core.build.AppBuilder
import dev.perinkpheral.core.build.Sdk
import java.awt.image.BufferedImage
import java.io.File
import java.nio.file.Files
import javax.imageio.ImageIO

/** Exercises scene routing and time services through a real module in the simulator. */
object SimButtonsTimeTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val scratch = Files.createTempDirectory("perinkpheral-buttons-time").toFile()
        try {
            val app = File(scratch, "buttons-time").apply { mkdirs() }
            File(app, "manifest.json").writeText(Json.write(mapOf(
                "name" to "buttons-time", "title" to "Buttons and time", "version" to "1.0.0",
                "abi" to 3, "fullscreen" to true)))
            File(app, "probe.c").writeText("""
                /* SPDX-License-Identifier: MIT */
                #include "host_api.h"
                static int received;
                static const int expected[] = { 16, 17, 18, 32, 35, 33, 48, 49, 50, 64, 65 };
                void app_on_load(void)
                {
                    host_local_time_t parts;
                    int64_t moment;
                    char text[8];
                    if (time_now() != 0 || time_zone_offset() != 0 ||
                        time_local(&parts) != HOST_ERR_NOT_FOUND ||
                        time_local_at(HOST_TIME_MIN, &parts) != HOST_OK || parts.hour != 1 ||
                        time_from_local(&parts, &moment) != HOST_OK || moment != HOST_TIME_MIN ||
                        time_local_at(HOST_TIME_MIN - 1, &parts) != HOST_ERR_ARGUMENT ||
                        time_local_at(-1, &parts) != HOST_ERR_ARGUMENT ||
                        time_local_at(-299851200, &parts) != HOST_ERR_ARGUMENT ||
                        time_local_at(0, &parts) != HOST_OK || parts.year != 1970 || parts.hour != 1 ||
                        time_from_local(&parts, &moment) != HOST_OK || moment != 0 ||
                        time_text(0, HOST_TIME_CLOCK, text, 5) != 5 || text[1] != '1' ||
                        time_text(0, HOST_TIME_NEAR, text, 8) != 5 || text[0] != '1' || text[2] != 'J' ||
                        alarm_set_at(0, 1767225599LL) != HOST_ERR_ARGUMENT) { __builtin_trap(); }
                }
                void app_on_button(int32_t event)
                {
                    if (received >= 11 || event != expected[received] ||
                        BTN_OF(event) != (event >> 4) || BTN_ACTION_OF(event) != (event & 15)) {
                        __builtin_trap();
                    }
                    received++;
                }
                void app_on_back(void)
                {
                    if (received != 11) { __builtin_trap(); }
                    screen_redraw(HOST_REDRAW_NORMAL);
                }
                void app_on_alarm(int32_t id) { (void)id; }
                void app_on_draw(void)
                {
                    gfx_draw_rect(received == 11 ? 1 : 0, 0, 1, 1, HOST_RECT_FILLED);
                }
            """.trimIndent())
            val scenarios = File(scratch, "scenarios").apply { mkdirs() }
            File(scenarios, "scenario.json").writeText(Json.write(mapOf(
                "app" to app.path, "at" to 1L, "tz" to "Etc/GMT-1",
                "steps" to listOf("shot initial", "press 1 2L 3 4", "shot pressed"))))
            for ((name, x) in listOf("initial" to 0, "pressed" to 1)) {
                val expected = BufferedImage(400, 300, BufferedImage.TYPE_BYTE_BINARY)
                for (row in 0 until 300) for (column in 0 until 400) expected.setRGB(column, row, 0xffffff)
                expected.setRGB(x, 0, 0)
                ImageIO.write(expected, "png", File(scenarios, "$name.png"))
            }
            val wasi = File(System.getenv("WASI_SDK_PATH") ?: error("WASI_SDK_PATH is required"))
            val failures = SimTest(root) { AppBuilder(Sdk(root), wasi) {} }.run(scenarios, false, workers = 1)
            check(failures.isEmpty()) { failures.joinToString("\n") }
            File(app, "probe.c").writeText("""
                /* SPDX-License-Identifier: MIT */
                #include "host_api.h"
                void app_on_load(void)
                {
                    host_local_time_t parts;
                    int64_t moment;
                    if (time_local_at(953553600LL, &parts) != HOST_OK || parts.hour != 8 ||
                        time_from_local(&parts, &moment) != HOST_OK || moment != 953553600LL) __builtin_trap();
                }
                void app_on_draw(void) { gfx_draw_rect(0, 0, 1, 1, HOST_RECT_FILLED); }
            """.trimIndent())
            File(scenarios, "scenario.json").writeText(Json.write(mapOf(
                "app" to app.path, "at" to 1795780800L, "tz" to "America/New_York",
                "steps" to listOf("shot initial"))))
            val recurring = SimTest(root) { AppBuilder(Sdk(root), wasi) {} }.run(scenarios, false, workers = 1)
            check(recurring.isEmpty()) { recurring.joinToString("\n") }
        } finally {
            scratch.deleteRecursively()
        }
        println("Simulator: button event numbers, historical conversions and unknown current clock passed.")
    }
}
