// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import java.io.File
import java.time.Instant

object DeviceTimeZonesTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val zones = DeviceTimeZones(root)
        fun offset(zone: String, at: String) = zones.rules(zone).getOffset(Instant.parse(at)).totalSeconds
        check(zones.posix("America/New_York") == "EST5EDT,M3.2.0,M11.1.0")
        check(offset("America/New_York", "2000-03-20T12:00:00Z") == -4 * 3600)
        check(offset("America/New_York", "2000-11-01T12:00:00Z") == -4 * 3600)
        check(offset("America/New_York", "2000-11-06T12:00:00Z") == -5 * 3600)
        check(offset("Europe/Moscow", "2000-07-01T12:00:00Z") == 3 * 3600)
        check(offset("Australia/Lord_Howe", "2000-01-01T12:00:00Z") == 11 * 3600)
        check(offset("Australia/Lord_Howe", "2000-07-01T12:00:00Z") == 10 * 3600 + 1800)
        check(offset("Pacific/Auckland", "2000-01-01T12:00:00Z") == 13 * 3600)
        check(offset("Etc/GMT-1", "1970-01-01T00:00:00Z") == 3600)
        check(offset("UTC", "2100-01-01T00:00:00Z") == 0)
        check(zones.posix("Asia/Calcutta") == zones.posix("Delhi"))
        check(zones.posix("CET-1CEST,M3.5.0,M10.5.0/3") == zones.posix("Europe/Berlin"))
        val cities = Json.parse(File(root, "firmware/system/settings/cities.json").readText()) as List<*>
        cities.forEach { entry ->
            val city = entry as Map<*, *>
            check(zones.posix(city["name"] as String) == city["zone"])
            zones.rules(city["name"] as String) // includes /24, /26, fractional and southern rules
        }
        check(runCatching { zones.posix("Unknown/Zone") }.isFailure)
        println("Device time zones: recurring POSIX rules, named aliases, historical dates and fractional DST passed.")
    }
}
