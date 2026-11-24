// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.check

import dev.perinkpheral.core.Json

/** Deliberately invalid responses derived from a supplied good answer, without a provider schema. */
object ResponseMutations {
    private const val MAX_MEMBER_MUTATIONS = 64
    private const val OVERSIZED_TEXT_BYTES = 80
    data class Mutation(val kind: String, val body: String)

    fun make(body: String): List<Mutation> {
        val parsed = runCatching { Json.parse(body) }.getOrNull()
        if (parsed !is Map<*, *> && parsed !is List<*>) {
            // A numeric token may belong to any streamed protocol. Only sanitizer safety
            // is asserted for these substitutions: a different number may be valid data.
            return Regex("[0-9]+").findAll(body).take(MAX_MEMBER_MUTATIONS).map { match ->
                Mutation("numeric-tokens", body.replaceRange(match.range, Long.MAX_VALUE.toString()))
            }.toList()
        }
        val result = arrayListOf(Mutation("truncated", body.take(body.length / 2)))
        fun replace(value: Any?, kind: String): Any? = when (value) {
            is Map<*, *> -> value.mapValues { replace(it.value, kind) }
            is List<*> -> when (kind) {
                "empty-lists" -> emptyList<Any?>()
                "missing-record" -> value.mapIndexed { index, item -> if (index == 0 && item is Map<*, *>) emptyMap<String, Any?>() else item }
                else -> value.map { replace(it, kind) }
            }
            else -> when (kind) {
                "nulls" -> null
                "wrong-types" -> if (value is String) true else "invalid"
                "long-values" -> if (value is String) "W".repeat(OVERSIZED_TEXT_BYTES) else value
                "numeric-overflow" -> if (value is Number) Long.MAX_VALUE else value
                else -> value
            }
        }
        for (kind in listOf("wrong-types", "empty-lists", "nulls", "missing-record", "long-values", "numeric-overflow")) {
            val changed = Json.write(replace(parsed, kind))
            if (changed != Json.write(parsed)) result += Mutation(kind, changed)
        }
        // One missing/null picked path must not shift every parallel list in the same way.
        // Change each object member in turn, including the first row of nested arrays.
        fun nullMembers(value: Any?): List<Any?> = when (value) {
            is Map<*, *> -> value.entries.flatMap { (key, item) ->
                listOf(value.toMutableMap().apply { put(key, null) }) +
                    nullMembers(item).map { child -> value.toMutableMap().apply { put(key, child) } }
            }
            is List<*> -> value.firstOrNull()?.let { first -> nullMembers(first).map { listOf(it) + value.drop(1) } }.orEmpty()
            else -> emptyList()
        }
        nullMembers(parsed).take(MAX_MEMBER_MUTATIONS).forEach { result += Mutation("picked-null", Json.write(it)) }
        return result.distinctBy { it.kind to it.body }
    }
}
