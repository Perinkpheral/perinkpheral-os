// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.check

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.build.Sdk
import java.io.File
import java.nio.file.Files

/**
 * The capability checks held to an application made for them, `sdk/check/apps/specimen`:
 * every check passes on it as it is, and each fails on a copy with one line of the
 * contract broken. A check that cannot fail is not a check.
 *
 * It compiles C for this computer and builds a module: without a C compiler or wasi-sdk
 * (WASI_SDK_PATH) only what needs neither is tested, and it says so.
 */
object AppCheckTest {
    /** One line of the contract broken in the specimen, and the capability that has to notice. */
    private class Break(val capability: String, val what: String, val edits: List<Pair<String, String>>, val saying: String,
                        val manifestEdits: List<Pair<String, String>> = emptyList(), val checkPatch: Map<String, Any?> = emptyMap())

    private val BREAKS = listOf(
        Break("store", "an unsaved answer silently reverts immediately on unload",
            listOf("        gfx_draw_text(MARGIN, 200, HOST_FONT_SMALL, \"Storage error.\", 14);" to ""), "response-save:"),
        Break("network", "a streamed protocol parses an unbounded integer",
            listOf("        arriving[arriving_length] = '\\0';\n        keep_text(\"note\"" to
                "        arriving[arriving_length] = '\\0';\n" +
                "        volatile int32_t number = 0;\n" +
                "        for (int32_t at = 0; at < arriving_length; at++) {\n" +
                "            if (arriving[at] >= '0' && arriving[at] <= '9') {\n" +
                "                number = number * 10 + arriving[at] - '0';\n            }\n        }\n" +
                "        keep_text(\"note\""), "app_on_data"),
        Break("sessions", "a refused session allocation is ignored",
            listOf("        log_write(HOST_LOG_ERROR, \"Session error.\", 14);" to ""), "session-refusal:"),
        Break("store", "a button publishes a value whose save failed",
            listOf("            count--;" to "            /* ignored refusal */"), "button-save:"),
        Break("store", "unchanged answers rewrite freshness as time advances",
            listOf("    rate_limit(http_status);\n    char picked" to
                "    int64_t seen = time_now();\n    kv_set(\"seen\", 4, &seen, sizeof seen);\n    rate_limit(http_status);\n    char picked"),
            "unchanged answers with elapsed time"),
        Break("timers", "alarm allocation refusal is ignored",
            listOf("schedule_failed = alarm_set_every(ASK_ALARM, ASK_EVERY_S, 0) != HOST_OK;" to
                "alarm_set_every(ASK_ALARM, ASK_EVERY_S, 0);"), "alarm-refusal:"),
        Break("notices", "a rejected notice consumes its reservation",
            listOf("        kv_set(\"told\", 4, &told, sizeof told);" to ""), "notice-queue full then accepting"),
        Break("notices", "a failed receipt after posting is lost on unload",
            listOf("if (kv_set(\"told\", 4, &reservation, sizeof reservation) != HOST_OK)" to "if (false)",
                "    if (notification_post(&notice) != HOST_OK) {\n        kv_set(\"told\", 4, &told, sizeof told);\n        alarm_set(1, 1);\n    }" to
                "    if (notification_post(&notice) != HOST_OK) {\n        alarm_set(1, 1);\n    } else {\n        kv_set(\"told\", 4, &reservation, sizeof reservation);\n    }"), "notice-receipt refused"),
        Break("network", "Retry-After does not gate new requests",
            listOf("    if (time_now() < retry_until) {\n        return;\n    }" to ""), "retry-after:"),
        Break("network", "wrong scalar types replace a good value",
            listOf("length < 10 && picked[0] >= '0' && picked[0] <= '9'" to "length <= HOST_VALUE_MAX"), "response wrong-types"),
        Break("network", "an empty or missing list overwrites good content",
            listOf("    if (answer_count(1) != 1 || row_length <= 0 || row_length >= HOST_VALUE_MAX) {\n        return;" to
                "    if (answer_count(1) != 1 || row_length <= 0 || row_length >= HOST_VALUE_MAX) {\n        keep_text(\"value\", value, \"bad\", &value_is_unsaved);\n        return;"), "response empty-lists"),
        Break("network", "null picked values are displayed literally",
            listOf("length < 10 && picked[0] >= '0' && picked[0] <= '9'" to "length <= HOST_VALUE_MAX",
                "    if (is_same(row, \"null\")) {\n        return;\n    }" to ""), "response nulls"),
        Break("network", "numeric overflow is accepted as data",
            listOf("length < 10" to "length <= HOST_VALUE_MAX"), "response numeric-overflow"),
        Break("network", "a picker-truncated name looks complete",
            listOf("row_length >= HOST_VALUE_MAX" to "row_length > HOST_VALUE_MAX"), "response long-values"),
        Break("clock", "future clock uses the current date's offset",
            listOf("time_local_at(1806321600LL, &future);" to
                "time_local_at(1806321600LL, &future);\n    int64_t local_seconds = 1806321600LL + time_zone_offset();\n    future.hour = (int32_t)(local_seconds / 3600 % 24);"), "clock event"),

        Break("sessions", "the page is not kept in the session",
            listOf("    int32_t saved = session_set(&page, sizeof page);" to "    int32_t saved = HOST_OK;"), "back from the stack"),
        Break("sessions", "a handler never returns",
            listOf("void app_on_back(void)\n{\n" to "static volatile int forever = 1;\n\nvoid app_on_back(void)\n{\n    while (forever) { }\n"),
            "did not return in 3 s"),
        Break("store", "every answer is counted in the store, changed or not",
            listOf("{\n    if (is_same(kept, text) && !*is_unsaved) {" to
                "{\n    static int32_t answers;\n    answers++;\n    kv_set(\"answers\", 7, &answers, sizeof answers);\n" +
                "    if (is_same(kept, text) && !*is_unsaved) {"), "unchanged answers"),
        Break("store", "a refused write is not tried again",
            listOf("    if (is_same(kept, text) && !*is_unsaved) {" to "    if (is_same(kept, text)) {"),
            "not saved when the same answer comes again"),
        Break("timers", "the timer is set by a press and not again in app_on_focus()",
            listOf("    timer_set(TICK_TIMER, TICK_MS);\n    schedule_failed = alarm_set_every" to "    schedule_failed = alarm_set_every",
                "    if (page == 0) {\n        page = 1;" to "    timer_set(TICK_TIMER, TICK_MS);\n    if (page == 0) {\n        page = 1;"),
            "set again in app_on_focus()"),
        Break("timers", "app_on_updated() does not set the alarm again",
            listOf("void app_on_updated(void)\n{\n    schedule_failed = alarm_set_every(ASK_ALARM, ASK_EVERY_S, 0) != HOST_OK;" to "void app_on_updated(void)\n{"),
            "app_on_updated() sets them again"),
        Break("network", "HOST_DATA_FAILED keeps the pieces that came before it",
            listOf("        arriving_length = 0;\n        return;\n    }\n    if ((flags & HOST_DATA_FIRST)" to
                "        arriving[arriving_length] = '\\0';\n        keep_text(\"note\", note, arriving, &note_is_unsaved);\n        return;\n    }\n    if ((flags & HOST_DATA_FIRST)"),
            "fails half way"),
        Break("network", "an answer's pieces are kept as they come, whole or not",
            listOf("    if ((flags & HOST_DATA_LAST) != 0) {\n        arriving[arriving_length] = '\\0';" to
                "    {\n        arriving[arriving_length] = '\\0';"), "stops half way"),
        Break("network", "the buffer of a note is shorter than the note",
            listOf("static char arriving[TEXT_BYTES];" to "static char arriving[16];"), "app_on_data"),
        Break("drawing", "a button's handler draws",
            listOf("    if (event != BTN_OK_PRESSED) {\n        return;\n    }\n" to
                "    if (event != BTN_OK_PRESSED) {\n        return;\n    }\n    gfx_draw_text(0, 0, HOST_FONT_TEXT, \"x\", 1);\n"),
            "outside app_on_draw()"),
        Break("drawing", "a rectangle is drawn past what the host takes",
            listOf("    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n    gfx_draw_rect(0, 0, 5000, 10, HOST_RECT_FILLED);\n"),
            "HOST_ERR_ARGUMENT"),
        Break("network", "an answer is taken only by the instance that asked for it",
            listOf("static void ask_note(void)\n{\n" to "static bool has_asked;\n\nstatic void ask_note(void)\n{\n    has_asked = true;\n",
                "    if (request != NOTE_REQUEST) {" to "    if (request != NOTE_REQUEST || !has_asked) {"),
            "an application that was on the screen when the answers came"),
        Break("network", "what an answer changed is not asked to be drawn",
            listOf("    *is_unsaved = kv_set(key, length_of(key), kept, at) != HOST_OK;\n    screen_redraw(HOST_REDRAW_NORMAL);\n" to
                "    *is_unsaved = kv_set(key, length_of(key), kept, at) != HOST_OK;\n"),
            "screen_redraw()"),
        Break("network", "a request goes to a host the manifest does not list",
            listOf("static const char NOTE_URL[] = \"https://example.test/note\";" to
                "static const char NOTE_URL[] = \"https://elsewhere.test/note\";"),
            "does not list"),
        Break("network", "a request with a tag the contract does not take",
            listOf("#define NOTE_REQUEST   HOST_REQUEST_FIRST" to "#define NOTE_REQUEST   (HOST_REQUEST_FIRST - 1)"),
            "does not take"),
        Break("network", "a request to a listed host at another port",
            listOf("static const char NOTE_URL[] = \"https://example.test/note\";" to
                "static const char NOTE_URL[] = \"https://example.test:8443/note\";"),
            "does not list"),
        Break("network", "one handler queues more than the device's 16 KB of requests",
            listOf("static void ask_note(void)\n{\n" to "static void ask_note(void)\n{\n" +
                "    static char filler[HOST_HTTP_BODY_MAX], headers[4 * 252];\n" +
                "    for (int32_t at = 0; at < (int32_t)sizeof headers; at++) {\n" +
                "        headers[at] = at % 252 == 0 ? 'A' : at % 252 == 1 ? ':' : at % 252 == 251 ? '\\n' : 'x';\n" +
                "    }\n" +
                "    for (int32_t count = 0; count < 11; count++) {\n" +
                "        http_send(NOTE_REQUEST + 1 + count, HOST_HTTP_POST, NOTE_URL, length_of(NOTE_URL), headers,\n" +
                "                  sizeof headers, filler, sizeof filler);\n" +
                "    }\n"),
            "16 KB"),
        Break("drawing", "a bar of no width",
            listOf("    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n    ui_draw_bar(MARGIN, MARGIN, 0, 1, 2);\n"),
            "ui_draw_bar() answered HOST_ERR_ARGUMENT"),
        Break("drawing", "app_on_draw() asks the surface's size and draws nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    (void)gfx_width();\n    return;\n"),
            "drew nothing"),
        Break("drawing", "a picture of no width",
            listOf("    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" +
                "    static const uint8_t nothing[] = { 0, 1 };\n    gfx_draw_picture(MARGIN, MARGIN, nothing, 1);\n"),
            "gfx_draw_picture() answered HOST_ERR_ARGUMENT"),
        Break("network", "a value picked from an answer that broke off is kept",
            listOf("    if (request != 0 || http_status != 200) {" to "    (void)http_status;\n    if (request != 0) {"),
            "failed with no pieces"),
        Break("timers", "a timer of an id the device does not have",
            listOf("#define TICK_TIMER     0" to "#define TICK_TIMER     HOST_TIMER_COUNT"),
            "timer_set("),
        Break("drawing", "app_on_draw() chooses a colour and draws nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    gfx_set_color(HOST_INK);\n    return;\n"),
            "drew nothing"),
        Break("store", "nothing written at an offset of a file",
            listOf("    if (event != BTN_OK_PRESSED) {\n" to
                "    file_write(\"cache\", 5, 0, \"x\", 1);\n    file_write(\"cache\", 5, 1, NULL, 0);\n    if (event != BTN_OK_PRESSED) {\n"),
            "file_write(\"cache\"", listOf("\"fullscreen\": false," to "\"fullscreen\": false, \"writes_files\": true,")),
        Break("store", "a write longer than the module's page",
            listOf("    if (event != BTN_OK_PRESSED) {\n" to
                "    static char byte;\n    file_write(\"cache\", 5, 0, &byte, 2147483647);\n    if (event != BTN_OK_PRESSED) {\n"),
            "2147483647 bytes", listOf("\"fullscreen\": false," to "\"fullscreen\": false, \"writes_files\": true,")),
        Break("drawing", "a press changes what is drawn, held in memory only, and asks for no redraw",
            listOf("static int32_t count;" to "static int32_t presses_seen;\nstatic int32_t count;",
                "void app_on_button(int32_t event)\n{\n" to "void app_on_button(int32_t event)\n{\n    presses_seen++;\n",
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                    "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" +
                    "    gfx_draw_text(MARGIN, 200, HOST_FONT_TEXT, presses_seen > 0 ? \"x\" : \"o\", 1);\n"),
            "than it draws now"),
        Break("network", "a handler hangs on the failure of a poll over an empty list, which has no item",
            listOf("    char picked[HOST_VALUE_MAX + 1];\n" to "    char picked[HOST_VALUE_MAX + 1];\n" +
                "    if (answer_item() < 0 && http_status < 0) {\n        static volatile int forever = 1;\n        while (forever) { }\n    }\n"),
            "did not return",
            listOf("\"url\": \"https://example.test/now?place={{place}}\", \"every\": 60," to
                    "\"url\": \"https://example.test/now?place={{places}}\", \"every\": 60, \"each\": \"places\",",
                "{ \"key\": \"place\", \"type\": \"text\", \"title\": \"Place\", \"default\": \"here\" }," to
                    "{ \"key\": \"place\", \"type\": \"text\", \"title\": \"Place\", \"default\": \"here\" },\n" +
                    "    { \"key\": \"places\", \"type\": \"list\", \"title\": \"Places\" },")),
        Break("drawing", "app_on_draw() fills a rectangle of no size and draws nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    gfx_draw_rect(0, 0, 0, 0, HOST_RECT_FILLED);\n    return;\n"),
            "drew nothing"),
        Break("drawing", "app_on_draw() faults on a frame it did not ask for, as the header's minute makes",
            listOf("#include \"host_api.h\"\n" to "#include \"host_api.h\"\n\nstatic int32_t unasked_draws;\n" +
                    "#define screen_redraw(flags) (unasked_draws = 0, screen_redraw(flags))\n",
                "void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    if (unasked_draws++ > 0) {\n        static volatile char *nowhere;\n        nowhere[0] = 1;\n    }\n"),
            "drawing it again"),
        Break("timers", "app_on_alarm() faults on an alarm set for the last moment there is",
            listOf("void app_on_focus(void)\n{\n" to "void app_on_focus(void)\n{\n    alarm_set_at(1, HOST_TIME_MAX);\n",
                "void app_on_updated(void)\n{\n" to "void app_on_updated(void)\n{\n    alarm_set_at(1, HOST_TIME_MAX);\n",
                "void app_on_alarm(int32_t id)\n{\n" to "void app_on_alarm(int32_t id)\n{\n    if (id == 1) {\n        static volatile char *nowhere;\n        nowhere[0] = 1;\n    }\n"),
            "app_on_alarm"),
        Break("drawing", "app_on_draw() draws only the reason of an answer that came, which is nothing",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    ui_draw_reason(200);\n    return;\n"),
            "drew nothing"),
        Break("drawing", "app_on_draw() draws only the lines of an empty text",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    ui_draw_text_lines(0, 0, HOST_UI_TEXT, \"\", 0, 100, 1);\n    return;\n"),
            "drew nothing"),
        Break("drawing", "app_on_draw() draws only actions with no labels",
            listOf("void app_on_draw(void)\n{\n" to "void app_on_draw(void)\n{\n    ui_draw_actions(\"\", 0, \"\", 0);\n    return;\n"),
            "drew nothing"),
        Break("store", "a value larger than the store takes is written",
            listOf("        count++;" to
                "        static char large[HOST_KV_TOTAL_MAX];\n        kv_set(\"large\", 5, large, sizeof large);\n        count++;"),
            "over the limits"),
    )

    /** What the stand-in host copied from the firmware, as a name there and a name here. */
    private val PINS = listOf(
        Triple("firmware/components/ui/kit.c", "TITLE_TOP", "TITLE_TOP"),
        Triple("firmware/components/ui/kit.c", "TITLE_RULE_GAP", "TITLE_RULE_GAP"),
        Triple("firmware/components/ui/kit.c", "TITLE_AFTER_RULE", "TITLE_AFTER_RULE"),
        Triple("firmware/components/ui/kit.c", "CELL_PADDING", "CELL_PADDING"),
        Triple("firmware/components/ui/kit.c", "CELL_PICTURE_GAP", "CELL_PICTURE_GAP"),
        Triple("firmware/components/ui/kit.c", "KEY_PAD_X", "KEY_PAD_X"),
        Triple("firmware/components/ui/kit.c", "KEY_ACTION_GAP", "KEY_ACTION_GAP"),
        Triple("firmware/components/ui/kit.c", "VALUE_UNIT_GAP", "VALUE_UNIT_GAP"),
        Triple("firmware/components/ui/kit.c", "VALUE_UNIT_GAP_LARGE", "VALUE_UNIT_GAP_LARGE"),
        Triple("firmware/components/ui/kit.c", "TEXT_BUFFER", "KIT_TEXT_BUFFER"),
        Triple("firmware/components/ui/list.c", "ROW_HEIGHT", "LIST_ROW_HEIGHT"),
        Triple("firmware/components/ui/list.c", "TOP_GAP", "LIST_TOP_GAP"),
        Triple("firmware/components/appsys/app_api_ui.c", "BUTTON_NAME_MAX", "BUTTON_NAME_MAX"),
        Triple("firmware/components/appstore/include/request_queue.h", "REQUEST_QUEUE_BYTES_MAX", "REQUEST_QUEUE_BYTES"),
    )

    private fun defines(file: File): Map<String, String> =
        Regex("""^#define\s+(\w+)\s+([^/\n]*?)\s*(/\*.*)?$""", RegexOption.MULTILINE).findAll(file.readText())
            .associate { it.groupValues[1] to it.groupValues[2] }

    /** The stand-in host's numbers are the firmware's: a change of kit.c, list.c, app_api_ui.c,
     * request_queue.h or the font that is not made here too fails this. The record of a queued
     * request is measured for wasm32, whose layout is the device's 32-bit one. */
    private fun pinned(root: File, wasi: File) {
        val standin = File(root, "sdk/check/standin.c")
        val here = defines(standin)
        for ((file, there, ours) in PINS) {
            val theirs = defines(File(root, file))[there] ?: error("$file no longer defines $there")
            check(here[ours] == theirs) { "standin.c's $ours is ${here[ours]}, $file's $there is $theirs" }
        }
        val scratch = defines(File(root, "firmware/components/map/include/map.h"))["MAP_SCRATCH_BYTES"]
        check(scratch == dev.perinkpheral.core.build.AppBuilder.MAP_WORKING_BYTES.toString()) {
            "AppBuilder's MAP_WORKING_BYTES is not map.h's MAP_SCRATCH_BYTES $scratch"
        }
        val ascent = Regex("""departure_11_ascent = (\d+)""").find(File(root, "firmware/components/gfx/fonts/departure_11.h").readText())
        check(ascent != null && here["DEPARTURE_ASCENT"] == ascent.groupValues[1]) { "the font's ascent is not standin.c's" }
        val record = here["REQUEST_RECORD_BYTES"] ?: error("standin.c has no REQUEST_RECORD_BYTES")
        val probe = Files.createTempFile("request-record", ".c").toFile()
        try {
            probe.writeText("#include \"request_queue.h\"\n_Static_assert(sizeof(queued_request_t) == $record, \"the record\");\n")
            val clang = File(wasi, "bin/clang").path
            val command = listOf(clang, "--target=wasm32", "-ffreestanding", "-fsyntax-only",
                "-I" + File(root, "firmware/components/appstore/include").path, "-I" + File(root, "sdk").path,
                "-I" + File(root, "sdk/lib").path, probe.path)
            val process = ProcessBuilder(command).redirectErrorStream(true).start()
            val said = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { "a queued request is not $record bytes on the device:\n$said" }
        } finally {
            probe.delete()
        }
        println("app-check: the stand-in host's measures are the firmware's (${PINS.size + 2} pinned)")
    }

    /** Copies of the specimen that are right, if unusual: the capability passes them. */
    private val FINE = listOf(
        Break("timers", "a declared custom visible schedule error",
            listOf("\"Schedule error.\", 15" to "\"Schedule unavailable.\", 21"), "",
            checkPatch = mapOf("failure_text" to listOf("Schedule unavailable."))),
        Break("network", "a documented response-shape exception remains visible in the report", emptyList(), "",
            checkPatch = mapOf("exceptions" to mapOf("responses.empty-lists" to "Empty is valid for this endpoint."))),
        Break("timers", "a timer as long as one may be", listOf("#define TICK_MS        1000" to "#define TICK_MS        2147483647"), ""),
        Break("timers", "a countdown of three seconds, over by the time a late tick comes",
            listOf("void app_on_timer(int32_t id)\n{\n    if (id == TICK_TIMER) {" to
                "void app_on_timer(int32_t id)\n{\n    if (id == TICK_TIMER && time_uptime_ms() < 3000) {"), ""),
        Break("drawing", "an outline of no height, which is a stroke", listOf("void app_on_draw(void)\n{\n" to
            "void app_on_draw(void)\n{\n    gfx_draw_rect(10, 10, 10, 0, HOST_RECT_OUTLINE);\n    return;\n"), ""),
        Break("drawing", "a text whose bytes past its end change, with no redraw: nothing shown changed",
            listOf("static int32_t count;" to "static char shown_text[8] = \"OK\";\nstatic int32_t count;",
                "void app_on_button(int32_t event)\n{\n" to "void app_on_button(int32_t event)\n{\n    shown_text[7]++;\n",
                "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" to
                    "    int32_t line_height = gfx_font_height(HOST_FONT_TEXT);\n" +
                    "    gfx_draw_text(MARGIN, 200, HOST_FONT_TEXT, shown_text, sizeof shown_text);\n"), ""),
        Break("timers", "an alarm set again by every instance in app_on_load(), and nothing in app_on_updated()",
            listOf("void app_on_load(void)\n{\n" to
                "void app_on_load(void)\n{\n    if (time_now() != 0) {\n        alarm_set_at(1, time_now() + 3600);\n    }\n"), ""),
    )

    @JvmStatic fun main(args: Array<String>) {
        check(AppCheck.text("a \"b\" \\ ?? \n é") == "\"a \\\"b\\\" \\\\ \\?\\? \\n \\303\\251\"")
        check(AppCheck.text(null) == "NULL")
        check(AppCheck.applies("sessions", emptySet(), emptySet(), emptyMap<String, Any>()))
        check(!AppCheck.applies("store", setOf("kv_get"), emptySet(), emptyMap<String, Any>()))
        check(AppCheck.applies("store", setOf("file_write"), emptySet(), emptyMap<String, Any>()))
        check(AppCheck.applies("timers", setOf("alarm_set_every"), emptySet(), emptyMap<String, Any>()))
        check(AppCheck.applies("network", emptySet(), emptySet(), mapOf("polls" to listOf(mapOf("url" to "https://a")))))
        check(!AppCheck.applies("network", setOf("poll_run"), emptySet(), emptyMap<String, Any>()))
        check(!AppCheck.applies("drawing", emptySet(), setOf("app_on_load"), emptyMap<String, Any>()))
        // A module whose stack and data fill the page leaves app_alloc() nothing, not the default.
        check(AppCheck.arenaBytes(65530) == 0L && AppCheck.arenaBytes(65536) == 0L)
        check(AppCheck.arenaBytes(57000) == 65536L - 57008L && AppCheck.arenaBytes(0) == -1L)

        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val wasi = System.getenv("WASI_SDK_PATH")?.let { File(it) }?.takeIf { File(it, "bin/clang").exists() || File(it, "bin/clang.exe").exists() }
        val compiler = System.getenv("CC")?.takeIf { it.isNotBlank() } ?: "cc"
        val hasCompiler = runCatching { ProcessBuilder(compiler, "--version").redirectErrorStream(true).start().waitFor() == 0 }.getOrDefault(false)
        if (wasi == null || !hasCompiler) {
            println("app-check: no wasi-sdk (WASI_SDK_PATH) or no C compiler: the checks themselves were not run")
            return
        }
        val variants = ResponseMutations.make("{\"value\":21,\"rows\":[{\"name\":\"row\"}]}")
        check(variants.map { it.kind }.toSet() == setOf("truncated", "wrong-types", "empty-lists", "nulls", "missing-record", "long-values", "numeric-overflow", "picked-null"))
        check(ResponseMutations.make("ordinary text").isEmpty())
        check(ResponseMutations.make("COUNT=7").single().body == "COUNT=9223372036854775807")
        check(variants.filter { it.kind != "truncated" }.all { runCatching { dev.perinkpheral.core.Json.parse(it.body) }.isSuccess })
        pinned(root, wasi)
        val specimen = File(root, "sdk/check/apps/specimen")
        val said = ArrayList<String>()
        val checker = AppCheck(Sdk(root), wasi) { said += it }

        // As it is: every capability applies to it and passes, and its own test is found and run.
        val whole = checker.run(specimen.parentFile)
        check(whole.failed.isEmpty()) { said.joinToString("\n") }
        check(whole.lines.map { it.what } == AppCheck.CAPABILITIES + "logic host.c") { whole.lines.map { it.what } }
        check(whole.lines.all { it.state == AppCheck.State.OK })
        check(said.first().startsWith("ok   specimen")) { said.first() }

        try { checker.run(specimen.parentFile, only = "pixels"); error("an unknown capability was accepted") }
        catch (error: BuildError) { check("no capability" in error.message.orEmpty()) }
        try { checker.run(specimen.parentFile, onlyApp = "absent"); error("an absent application was accepted") }
        catch (error: BuildError) { check("no application" in error.message.orEmpty()) }

        val scratch = Files.createTempDirectory("perinkpheral-app-check-test").toFile()
        try {
            val mockCases = listOf("missing", "malformed", "array", "unmatched")
            for (mockCase in mockCases) {
                val copy = File(scratch, "mocks/$mockCase/specimen")
                specimen.copyRecursively(copy)
                File(copy, "test/check.json").writeText("{\"mocks\":\"answers.json\"}")
                val answers = File(copy, "test/answers.json")
                when (mockCase) {
                    "malformed" -> answers.writeText("{")
                    "array" -> answers.writeText("[]")
                    "unmatched" -> answers.writeText("{}")
                }
                val report = checker.run(copy, only = "network")
                if (mockCase == "unmatched") {
                    check(report.failed.isEmpty()) { report.lines.flatMap { it.details }.joinToString("\n") }
                    check(report.lines.single().details.any { "checked with failures only" in it })
                } else {
                    check(report.failed.isNotEmpty()) { "stand-in host accepted $mockCase mocks" }
                    if (mockCase == "missing") check(report.failed.any { line -> line.details.any { "answers.json" in it } })
                }
            }
            val invalidDeclarations = listOf(
                "{\"exceptions\":{\"responses.empty-lists\":\"\"}}",
                "{\"exceptions\":{\"unknown\":\"reason\"}}",
                "{\"exceptions\":false}",
                "{\"failure_text\":[\"\"]}",
                "{\"clock_probes\":[{\"now\":1795262400,\"event\":-1}]}"
            )
            invalidDeclarations.forEachIndexed { index, declaration ->
                val copy = File(scratch, "declaration-$index/specimen")
                specimen.copyRecursively(copy)
                File(copy, "test/check.json").writeText(declaration)
                check(checker.run(copy, only = "network").failed.isNotEmpty()) { "accepted invalid declaration: $declaration" }
            }
            val system = File(scratch, "system/specimen")
            specimen.copyRecursively(system)
            val manifest = File(system, "manifest.json")
            manifest.writeText(manifest.readText().replace("\"fullscreen\": false", "\"fullscreen\": false, \"system\": true"))
            File(system, "test/must-not-compile.c").writeText("#error system own tests must be skipped\n")
            val skipped = checker.run(system.parentFile)
            check(skipped.lines.single().state == AppCheck.State.NOT_APPLICABLE)
            check(skipped.lines.single().what == "all" && skipped.failed.isEmpty())
            fun copied(number: Int, broken: Break): File {
                val copy = File(scratch, "$number/specimen")
                specimen.copyRecursively(copy)
                val source = File(copy, "specimen.c")
                var code = source.readText()
                for ((from, to) in broken.edits) {
                    check(from in code) { "${broken.what}: the specimen no longer has the line this breaks" }
                    code = code.replace(from, to)
                }
                source.writeText(code)
                if (broken.checkPatch.isNotEmpty()) {
                    val checkFile = File(copy, "test/check.json")
                    val kept = (dev.perinkpheral.core.Json.parse(checkFile.readText()) as Map<*, *>).entries
                        .associate { it.key.toString() to it.value }.toMutableMap()
                    kept.putAll(broken.checkPatch)
                    checkFile.writeText(dev.perinkpheral.core.Json.write(kept))
                }
                val manifest = File(copy, "manifest.json")
                var described = manifest.readText()
                for ((from, to) in broken.manifestEdits) {
                    check(from in described) { "${broken.what}: the specimen's manifest no longer has what this breaks" }
                    described = described.replace(from, to)
                }
                manifest.writeText(described)
                said.clear()
                return copy
            }
            BREAKS.forEachIndexed { number, broken ->
                val report = checker.run(copied(number, broken).parentFile, only = broken.capability)
                val line = report.lines.single()
                check(line.state == AppCheck.State.FAILED) { "${broken.what}: ${broken.capability} did not notice" }
                check(line.details.any { broken.saying in it }) { "${broken.what}: ${line.details.joinToString("\n")}" }
            }
            // And what is right, if unusual, passes: the checks find no fault the device would not have.
            FINE.forEachIndexed { number, fine ->
                val report = checker.run(copied(BREAKS.size + number, fine).parentFile, only = fine.capability)
                val line = report.lines.single()
                check(line.state == AppCheck.State.OK) { "${fine.what}: ${line.details.joinToString("\n")}" }
                if (fine.checkPatch.containsKey("exceptions")) {
                    check(line.details.any { "exception responses.empty-lists" in it }) { "exception was not reported" }
                }
            }
        } finally {
            scratch.deleteRecursively()
        }
        println("app-check: the specimen passes; ${BREAKS.size} broken copies are each caught by their capability, " +
            "${FINE.size} unusual but right ones pass")
    }
}
