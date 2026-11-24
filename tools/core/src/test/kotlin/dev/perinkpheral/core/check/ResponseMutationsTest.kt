// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.check

import dev.perinkpheral.core.Json

/** Field mutations must reach one bad value without first invalidating its neighbours. */
object ResponseMutationsTest {
    @JvmStatic fun main(args: Array<String>) {
        val body = """{"first":"alpha | beta","second":"keep","number":21,"other":22}"""
        val mutations = ResponseMutations.make(body)
        val longer = mutations.filter { it.kind == "long-values" }.map { Json.parse(it.body) as Map<*, *> }
        check(longer.any { (it["first"] as String).startsWith("alpha | betaW") && it["second"] == "keep" && it["number"] == 21L })
        check(longer.any { it["first"] == "alpha | beta" && (it["second"] as String).startsWith("keepW") })
        val numbers = mutations.filter { it.kind == "numeric-overflow" }.map { Json.parse(it.body) as Map<*, *> }
        check(numbers.any { it["number"] == Long.MAX_VALUE && it["other"] == 22L && it["first"] == "alpha | beta" })
        check(numbers.any { it["number"] == 21L && it["other"] == Long.MAX_VALUE })
        val types = mutations.filter { it.kind == "wrong-types" }.map { Json.parse(it.body) as Map<*, *> }
        check(types.any { it["number"] == "invalid" && it["other"] == 22L && it["first"] == "alpha | beta" })
        val lists = ResponseMutations.make("""{"values":["first","second"]}""")
        check(lists.any { it.kind == "picked-null" && (Json.parse(it.body) as Map<*, *>)["values"] == listOf(null, "second") })
        val unicode = ResponseMutations.make(Json.write(mapOf("first" to "\u00e9".repeat(40), "second" to "keep")))
        val changed = unicode.filter { it.kind == "long-values" }.map { Json.parse(it.body) as Map<*, *> }
            .first { it["second"] == "keep" }["first"] as String
        check(changed.substringBefore('W').toByteArray().size <= 24 && changed.contains("W".repeat(80)))
        val wide = ResponseMutations.make(Json.write((0 until 20_000).associate { "field$it" to it }))
        check(wide.count { it.kind == "picked-null" } == 64)
        check(wide.count { it.kind == "numeric-overflow" } == 65) // all leaves plus 64 members
        check(wide.count { it.kind == "wrong-types" } == 65)
        println("Response mutations: single-field strings/numbers, list nulls and UTF-8 prefixes passed")
    }
}
