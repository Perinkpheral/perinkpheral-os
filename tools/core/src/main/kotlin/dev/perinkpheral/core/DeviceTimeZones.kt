// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import java.io.File
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.zone.ZoneOffsetTransition
import java.time.zone.ZoneRules

/** Named test zones use the same recurring POSIX rules as Settings, including past dates. */
class DeviceTimeZones(root: File) {
    private val named: Map<String, String> by lazy {
        val cities = Json.parse(File(root, "firmware/system/settings/cities.json").readText()) as List<*>
        buildMap {
            cities.forEach { entry ->
                val city = entry as Map<*, *>
                val rule = city["zone"] as String
                put(city["name"] as String, rule)
                (city["iana"] as List<*>).forEach { put(it as String, rule) }
            }
            put("Australia/Lord_Howe", "LHST-10:30LHDT-11,M10.1.0,M4.1.0")
        }
    }

    fun posix(name: String): String {
        named[name]?.let { return it }
        val fixed = runCatching { ZoneId.of(name).rules }.getOrNull()?.takeIf { it.isFixedOffset }
        if (fixed != null) return "FIX" + signedTime(-fixed.getOffset(Instant.EPOCH).totalSeconds)
        if (ZONE.matches(name)) return name
        throw BuildError("unknown device time zone: $name; use a Settings city, its IANA name or POSIX rules")
    }

    fun rules(name: String): ZoneRules {
        val rule = posix(name)
        val fields = ZONE.matchEntire(rule)?.groupValues ?: throw BuildError("unsupported POSIX zone: $rule")
        val standard = ZoneOffset.ofTotalSeconds(-seconds(fields[1]))
        if (fields[2].isEmpty()) return ZoneRules.of(standard)
        val daylight = ZoneOffset.ofTotalSeconds(if (fields[3].isEmpty()) standard.totalSeconds + SECONDS_PER_HOUR else -seconds(fields[3]))
        fun transition(year: Int, text: String, before: ZoneOffset, after: ZoneOffset): ZoneOffsetTransition {
            val parts = MONTH_RULE.matchEntire(text)?.groupValues ?: throw BuildError("unsupported POSIX transition: $text")
            val month = parts[1].toInt()
            val week = parts[2].toInt()
            val weekday = parts[3].toInt()
            val first = LocalDate.of(year, month, 1)
            val firstDay = 1 + (weekday - first.dayOfWeek.value % 7 + 7) % 7
            val day = (firstDay + (week - 1) * 7).let { if (it > first.lengthOfMonth()) it - 7 else it }
            val at = if (parts[4].isEmpty()) 2 * SECONDS_PER_HOUR else seconds(parts[4])
            return ZoneOffsetTransition.of(first.withDayOfMonth(day).atStartOfDay().plusSeconds(at.toLong()), before, after)
        }
        val transitions = (FIRST_RULE_YEAR..LAST_RULE_YEAR).flatMap { year -> listOf(
            transition(year, fields[4], standard, daylight), transition(year, fields[5], daylight, standard))
        }.sortedBy { it.instant }
        return ZoneRules.of(standard, transitions.first().offsetBefore, emptyList(), transitions, emptyList())
    }

    private fun seconds(text: String): Int {
        val parts = text.removePrefix("-").removePrefix("+").split(':').map { it.toInt() }
        val total = parts[0] * SECONDS_PER_HOUR + parts.getOrElse(1) { 0 } * 60 + parts.getOrElse(2) { 0 }
        return if (text.startsWith('-')) -total else total
    }

    private fun signedTime(seconds: Int): String = (if (seconds < 0) "-" else "") +
        "${kotlin.math.abs(seconds) / SECONDS_PER_HOUR}:${kotlin.math.abs(seconds) / 60 % 60}:${kotlin.math.abs(seconds) % 60}"

    companion object {
        private const val SECONDS_PER_HOUR = 3600
        // Enclose the contract's conversion range so southern summers start with their daylight offset.
        private const val FIRST_RULE_YEAR = 1899
        private const val LAST_RULE_YEAR = 2101
        private const val NAME = "(?:[A-Za-z]{3,}|<[^>]+>)"
        private const val OFFSET = "[+-]?[0-9]{1,3}(?::[0-9]{1,2}(?::[0-9]{1,2})?)?"
        private val ZONE = Regex("$NAME($OFFSET)(?:($NAME)($OFFSET)?,([^,]+),([^,]+))?")
        private val MONTH_RULE = Regex("M([1-9]|1[0-2])\\.([1-5])\\.([0-6])(?:/($OFFSET))?")
    }
}
