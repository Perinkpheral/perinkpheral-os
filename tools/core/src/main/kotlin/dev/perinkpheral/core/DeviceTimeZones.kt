// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

import java.io.File
import java.time.Clock
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.zone.ZoneOffsetTransition
import java.time.zone.ZoneRules

/** Explicit POSIX syntax keeps its signs; named zones use the device's recurring rules. */
class DeviceTimeZones(root: File, private val clock: Clock = Clock.systemUTC()) {
    private val named: Map<String, String> by lazy {
        val cities = Json.parse(File(root, "firmware/system/settings/cities.json").readText()) as List<*>
        buildMap {
            cities.forEach { entry ->
                val city = entry as Map<*, *>
                val rule = city["zone"] as String
                put(city["name"] as String, rule)
                (city["iana"] as List<*>).forEach { put(it as String, rule) }
            }
        }
    }

    fun posix(name: String): String {
        if (ZONE.matches(name)) return name
        named[name]?.let { return it }
        runCatching { ZoneId.of(name).rules }.getOrNull()?.let { return recurring(it) }
        throw BuildError("unknown device time zone: $name; use a Settings city, a JDK time-zone name or POSIX rules")
    }

    private fun recurring(rules: ZoneRules): String {
        val now = clock.instant()
        val year = now.atOffset(ZoneOffset.UTC).year
        val start = LocalDate.of(year, 1, 1).atStartOfDay().toInstant(ZoneOffset.UTC)
        val end = LocalDate.of(year + 1, 1, 1).atStartOfDay().toInstant(ZoneOffset.UTC)
        val changes = generateSequence(rules.nextTransition(start.minusNanos(1))) { rules.nextTransition(it.instant) }
            .takeWhile { it.instant < end }.toList()
        // Match the device page's current-year inference, without retaining IANA history.
        val yearlyPair = changes.size == 2 && changes[0].offsetBefore == changes[1].offsetAfter &&
            changes[0].offsetAfter == changes[1].offsetBefore
        if (!yearlyPair) return "FIX" + signedTime(-rules.getOffset(now).totalSeconds)
        val summer = changes.first { it.offsetAfter.totalSeconds > it.offsetBefore.totalSeconds }
        val winter = changes.first { it.offsetAfter.totalSeconds < it.offsetBefore.totalSeconds }
        val standard = summer.offsetBefore.totalSeconds
        val daylight = summer.offsetAfter.totalSeconds
        val daylightOffset = if (daylight - standard == SECONDS_PER_HOUR) "" else signedTime(-daylight)
        return "STD${signedTime(-standard)}DST$daylightOffset,${monthRule(summer)},${monthRule(winter)}"
    }

    private fun monthRule(change: ZoneOffsetTransition): String {
        val wall = change.dateTimeBefore
        val date = wall.toLocalDate()
        val week = if (date.dayOfMonth + 7 > date.lengthOfMonth()) 5 else (date.dayOfMonth - 1) / 7 + 1
        val at = wall.toLocalTime().toSecondOfDay()
        val time = if (at == 2 * SECONDS_PER_HOUR) "" else "/${signedTime(at)}"
        return "M${date.monthValue}.$week.${date.dayOfWeek.value % 7}$time"
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

    private fun signedTime(seconds: Int): String {
        val size = kotlin.math.abs(seconds)
        val minutes = size / 60 % 60
        val remainder = size % 60
        return (if (seconds < 0) "-" else "") + (size / SECONDS_PER_HOUR) +
            (if (minutes != 0 || remainder != 0) ":${minutes.toString().padStart(2, '0')}" else "") +
            (if (remainder != 0) ":${remainder.toString().padStart(2, '0')}" else "")
    }

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
