// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

/** Stress-run consent must parse as a bare flag without consuming the next option. */
object ArgumentsTest {
    @JvmStatic fun main(args: Array<String>) {
        for (options in listOf(
            arrayOf("--out", "run", "--allow-remembered-change"),
            arrayOf("--allow-remembered-change", "--out", "run")
        )) {
            val parsed = Arguments(arrayOf("stress-run", *options, "--quick", "--axes", "e,f"))
            check(parsed.words == listOf("stress-run"))
            check(parsed.has("allow-remembered-change"))
            check(parsed.has("quick"))
            check(parsed.option("out") == "run")
            check(parsed.option("axes") == "e,f")
        }
        check(Arguments(arrayOf("stress-run", "--out", "run", "--allow-remembered-change"))
            .has("allow-remembered-change"))
        val absent = Arguments(arrayOf("stress-run", "--out", "run"))
        check(!absent.has("allow-remembered-change"))
        val valued = Arguments(arrayOf("app-check", "apps", "--app", "clock"), emptySet())
        check(valued.option("app") == "clock")
        println("Arguments: bare remembered-application flag, option order and absent consent OK")
    }
}
