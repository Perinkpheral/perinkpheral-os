// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import dev.perinkpheral.core.build.BuildFlags
import dev.perinkpheral.core.build.Sdk
import dev.perinkpheral.core.build.versionNumber
import java.io.File

/** Regression checks for the documented exceptions a caller must account for. */
object DocumentationTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        fun read(path: String) = File(root, path).readText().replace(Regex("\\s+"), " ")
        val specification = read("CLAUDE.md")
        val security = read("SECURITY.md")
        val guide = read("docs/guide/source/guide.html")
        val network = read("sdk/host/net.h").replace(" * ", " ")
        val api = read("docs/api/openapi.yaml")
        val wifi = api.substringAfter(" /api/wifi:").substringBefore(" /api/time:")
        val settings = api.substringAfter(" /api/apps/settings:").substringBefore(" /api/apps/remove:")
        val agents = read("AGENTS.md")
        val simulator = read("tools/simulator/README.md")
        var findings = 0
        fun finding(number: Int, vararg assertions: Pair<String, String>) {
            for ((text, expected) in assertions) {
                check(expected in text) { "Finding $number: missing documentation: $expected" }
            }
            findings++
        }
        finding(1, *listOf(specification, security, guide, network).map {
            it to "both ports in {80, 443}"
        }.toTypedArray())
        finding(2, security to "GET polls follow redirects", security to "without replaying the request")
        finding(3, specification to "a raw request's expanded URL is queued unchanged",
            specification to "Scheduled manifest polls instead expand their URL, headers and body at dispatch")
        finding(4, *listOf(specification, security, guide, settings).flatMap {
            listOf(it to "previous stored or default value is nonempty",
                it to "including supplied replacements", it to "leave a supplied secret deleted")
        }.toTypedArray())
        finding(5, specification to "an empty secret deletes its key",
            guide to "an empty secret deletes its key", settings to "an empty secret deletes its key")
        finding(6, wifi to "required: [ssid]", wifi to "Omitted password, tz and tz_city retain their previous values.")
        finding(7, wifi to "replacing any existing zone")
        finding(8, network to "before the power-saving floor", guide to "before the power-saving floor",
            network to "10 s to 20 s resets it", guide to "10 s to 20 s resets it")
        finding(9, guide to "While power saving is active, scheduled poll intervals have a minimum of 300 seconds",
            guide to "can request an earlier poll")
        finding(10, guide to "without manifest-setting validation or destination-change secret clearing",
            guide to "An empty value deletes the key", guide to "polls are requested again")
        finding(11, specification to "Recovery handles firmware uploads without this admission check")
        check("a firmware upload requires 32 KB" !in specification)
        finding(12, specification to "size, name and check are written first, then the module",
            specification to "only the four-byte magic is written last")
        finding(13, specification to "two event queues, with input taken first")
        finding(14, specification to "OTA probation, button debounce and network request timeouts keep their own timing")
        check("One module keeps every deadline" !in specification && "One module owns every deadline" !in specification)
        finding(15, specification to "Package downloads are staged by the web waiting-handler task or the automatic-update task")
        finding(16, specification to "after physical confirmation, recovery shows")
        finding(17, specification to "unless the instance has already stopped because of a fault")
        finding(18, read("sdk/host/buttons.h").replace(" * ", " ") to
            "BTN_BACK_KEYDOWN without BTN_BACK_KEYUP for that hold")
        finding(19, read("sdk/host/ui.h") to "one or two lines of body text",
            guide to "one or two detail lines")
        finding(20, specification to "UPDATE (the project mark), which always uses its original 11-cell drawing",
            guide to "UPDATE (the project mark) always uses its original 11-cell drawing")
        finding(21, specification to "new <name> --author \"Your name\"")
        finding(22, agents to "three firmware integration points", agents to "native stand-in",
            agents to "`sdk/check/standin.c`", agents to "`tools/perinkpheral contract snapshot`")
        finding(23, guide to "official catalog is the <code>perinkpheral-apps</code> repository.")
        check("with eighteen applications" !in guide)
        finding(24, specification to "pack sizes depend on the generated artifact")
        check("24,796" !in specification)
        finding(25, agents to "builds all four PDFs", agents to "attaches all four PDFs")
        finding(26, guide to "mocked redirects are delivered as final responses and are not followed",
            simulator to "redirects are delivered as final responses and are not followed")
        finding(27, network to "a receiving instance, on or off the screen, can be unloaded",
            specification to "Response delivery can be interrupted on or off the screen",
            guide to "Response delivery can be interrupted on or off the screen",
            read("docs/guide/source/guide.ru.txt") to
                "Передача ответа может прерваться как на экране, так и в фоне")
        finding(28, *listOf(specification, guide, network,
            read("docs/guide/source/guide.ru.txt")).map {
            it to "Saving the application's settings also stops a poll stream"
        }.toTypedArray(), read("docs/guide/source/guide.ru.txt") to
            "Сохранение настроек приложения также останавливает поток ответа на опрос")
        finding(29, network to "matching scalars in source order",
            network to "Missing members and non-scalar values are skipped",
            network to "first matching path only", network to "Parallel lists cannot safely be zipped",
            network to "Picked 2xx responses", network to "stored empty values are valid",
            guide to "Lists contain matching scalar values in source order",
            guide to "source guarantees corresponding scalars for every record",
            read("docs/guide/source/guide.ru.txt") to "Сопоставлять параллельные списки по индексам безопасно")
        check("two lists of one array stay in step" !in network)
        check("a {{setting}} used by the request is empty" !in network)
        val sdk = Sdk(root)
        val base = read("sdk/host/base.h").replace(" * ", " ")
        val umbrella = read("sdk/host_api.h").replace(" * ", " ")
        finding(30, base to "negative result means failure", base to "unknown negative",
            base to "ignore unknown events, intents and enum values", base to "unknown output flag bits",
            guide to "negative results mean failure", guide to "Ignore unknown output flag bits")
        finding(31, umbrella to "at most 64 KB (65536 bytes)",
            umbrella to "at least 112 KB (114688 bytes)", umbrella to "not compatibility promises",
            guide to "112 KB (114688 bytes)", guide to "not compatibility promises",
            read("docs/guide/source/guide.ru.txt") to "не являются обещаниями совместимости")
        finding(32, read("sdk/host/app.h").replace(" * ", " ") to
            "a file in the incoming package replaces an existing file of the same name",
            guide to "incoming package file replaces an existing file of the same name")
        check(sdk.contract.string("HOST_ABI_3_FIRMWARE_FIRST") == sdk.firmwareFirst)
        check(versionNumber(sdk.firmwareFirst)!! <= versionNumber(sdk.version)!!)
        check(BuildFlags.WASM_PAGE == 65536 && BuildFlags.APP_AREA >= 112 * 1024)
        val areaHeader = read("firmware/components/wasm3/m3_area.h")
        val area = Regex("#define M3_AREA_BYTES \\((\\d+) \\* 1024\\)").find(areaHeader)!!
            .groupValues[1].toInt() * 1024
        check(area >= BuildFlags.APP_AREA)
        check(findings == 32)
        println("DocumentationTest: $findings findings checked")
    }
}
