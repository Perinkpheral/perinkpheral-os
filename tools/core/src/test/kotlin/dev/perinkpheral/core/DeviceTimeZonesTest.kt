// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import java.io.File
import java.time.Clock
import java.time.Instant
import java.time.ZoneId
import java.time.ZoneOffset

object DeviceTimeZonesTest {
    @JvmStatic fun main(args: Array<String>) {
        val root = generateSequence(File(System.getProperty("user.dir")).canonicalFile) { it.parentFile }
            .first { File(it, "sdk/host_api.h").isFile }
        val zones = DeviceTimeZones(root, Clock.fixed(Instant.parse("2026-11-28T00:00:00Z"), ZoneOffset.UTC))
        fun offset(zone: String, at: String) = zones.rules(zone).getOffset(Instant.parse(at)).totalSeconds
        for (rule in listOf("UTC-3", "<+03>-3", "EST5EDT,M3.2.0,M11.1.0")) {
            check(zones.posix(rule) == rule) { "explicit POSIX rule was reinterpreted: $rule" }
        }
        check(offset("UTC-3", "2026-11-28T12:00:00Z") == 3 * 3600)
        check(offset("<+03>-3", "2026-11-28T12:00:00Z") == 3 * 3600)
        check(offset("EST5EDT,M3.2.0,M11.1.0", "2026-01-01T12:00:00Z") == -5 * 3600)
        check(offset("EST5EDT,M3.2.0,M11.1.0", "2026-07-01T12:00:00Z") == -4 * 3600)
        check(offset("Europe/Nicosia", "2000-01-01T12:00:00Z") == 2 * 3600)
        check(offset("Europe/Nicosia", "2000-07-01T12:00:00Z") == 3 * 3600)
        check(offset("Pacific/Apia", "1970-01-01T00:00:00Z") == 13 * 3600)
        check(offset("Pacific/Apia", "2000-07-01T12:00:00Z") == 13 * 3600)
        check(offset("Asia/Kolkata", "2000-01-01T12:00:00Z") == 5 * 3600 + 1800)
        check(zones.posix("Europe/Nicosia") == "STD-2DST,M3.5.0/3,M10.5.0/4")
        check(zones.posix("Pacific/Apia") == "FIX-13")
        check(zones.posix("Australia/Lord_Howe") == "STD-10:30DST-11,M10.1.0,M4.1.0")
        check(zones.posix("Asia/Kolkata") == "IST-5:30")
        for (name in listOf("Europe/Nicosia", "Australia/Lord_Howe")) {
            val expected = ZoneId.of(name).rules
            val actual = zones.rules(name)
            val start = Instant.parse("2026-01-01T00:00:00Z")
            val end = Instant.parse("2027-01-01T00:00:00Z")
            val transitions = generateSequence(expected.nextTransition(start)) { expected.nextTransition(it.instant) }
                .takeWhile { it.instant < end }.toList()
            check(transitions.size == 2)
            transitions.forEach { change ->
                for (at in listOf(change.instant.minusSeconds(1), change.instant)) {
                    check(actual.getOffset(at) == expected.getOffset(at)) { "$name at $at" }
                }
            }
        }
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
        ZoneId.getAvailableZoneIds().forEach { zones.rules(it) }
        check(runCatching { zones.posix("Unknown/Zone") }.exceptionOrNull() is BuildError)
        println("Device time zones: recurring POSIX rules, named aliases, historical dates and fractional DST passed.")
    }
}
