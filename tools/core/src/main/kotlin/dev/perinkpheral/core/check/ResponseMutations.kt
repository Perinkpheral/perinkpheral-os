// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.check

import dev.perinkpheral.core.Json

/** Deliberately invalid responses derived from a supplied good answer, without a provider schema. */
object ResponseMutations {
    private const val MAX_MEMBER_MUTATIONS = 64
    private const val OVERSIZED_TEXT_BYTES = 80
    private const val PRESERVED_PREFIX_BYTES = 24
    data class Mutation(val kind: String, val body: String)

    private fun longer(text: String): String {
        val prefix = StringBuilder()
        var bytes = 0
        for (code in text.codePoints().toArray()) {
            val character = String(Character.toChars(code))
            val size = character.toByteArray().size
            if (bytes + size > PRESERVED_PREFIX_BYTES) break
            prefix.append(character)
            bytes += size
        }
        return prefix.toString() + "W".repeat(OVERSIZED_TEXT_BYTES)
    }

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
                "long-values" -> if (value is String) longer(value) else value
                "numeric-overflow" -> if (value is Number) Long.MAX_VALUE else value
                else -> value
            }
        }
        for (kind in listOf("wrong-types", "empty-lists", "nulls", "missing-record", "long-values", "numeric-overflow")) {
            val changed = Json.write(replace(parsed, kind))
            if (changed != Json.write(parsed)) result += Mutation(kind, changed)
        }
        // Keep the other fields valid: corrupting an identity must not prevent reaching
        // a displayed name or number. The first row keeps the mutation count bounded.
        fun members(value: Any?, accepts: (Any?) -> Boolean, change: (Any?) -> Any?): List<Any?> = when (value) {
            is Map<*, *> -> value.entries.flatMap { (key, item) ->
                val direct = if (accepts(item)) listOf(value.toMutableMap().apply { put(key, change(item)) }) else emptyList()
                direct + members(item, accepts, change).map { child -> value.toMutableMap().apply { put(key, child) } }
            }
            is List<*> -> if (value.isEmpty()) emptyList() else {
                val first = value.first()
                val direct = if (accepts(first)) listOf(listOf(change(first)) + value.drop(1)) else emptyList()
                direct + members(first, accepts, change).map { listOf(it) + value.drop(1) }
            }
            else -> emptyList()
        }
        fun addMembers(kind: String, accepts: (Any?) -> Boolean, change: (Any?) -> Any?) {
            members(parsed, accepts, change).take(MAX_MEMBER_MUTATIONS).forEach { result += Mutation(kind, Json.write(it)) }
        }
        addMembers("picked-null", { it != null }, { null })
        addMembers("long-values", { it is String }, { longer(it as String) })
        addMembers("numeric-overflow", { it is Number }, { Long.MAX_VALUE })
        addMembers("wrong-types", { it is Number }, { "invalid" })
        return result.distinctBy { it.kind to it.body }
    }
}
