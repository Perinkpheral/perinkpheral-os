// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

/**
 * How an application is compiled and linked: written here once. The build uses them, the
 * SDK's CMake module asks for them (`tools/perinkpheral flags`), and the CLion plugin writes
 * them into its toolchain file, so that what an IDE compiles is what the device gets.
 */
object BuildFlags {
    /** The most memory a module may declare: one standard wasm page. A module is linked
     *  with what its stack and data take and no more (one-byte pages, `--page-size=1`),
     *  and what it leaves of the application area is the interpreter's. */
    const val WASM_PAGE = 65536

    /** The application area of the device: the module's memory and the interpreter's
     *  together (M3_AREA_BYTES of firmware/components/wasm3/m3_area.h). */
    const val APP_AREA = 112 * 1024

    /** The stack at the bottom of the memory. */
    const val APP_STACK = 8192

    const val TARGET = "--target=wasm32"

    /** No C library, built for size, every warning an error. */
    val compile = listOf("-nostdlib", "-ffreestanding", "-fno-builtin", "-Oz", "-flto", "-Wall", "-Wextra", "-Werror")

    /** What the linker is told: no entry, nothing kept that is not called, memory as large
     *  as the stack and the data, which cannot grow. */
    val linker = listOf("-Wl,--no-entry", "-Wl,--strip-all", "-Wl,--gc-sections",
        "-Wl,--page-size=1", // one-byte pages: the memory is what is needed, to the byte
        "-Wl,--no-growable-memory", "-Wl,-z,stack-size=$APP_STACK",
        "-Wl,--stack-first") // the stack below the data: an overflow leaves the memory and traps

    /** The link step on its own, as a build system makes it: the driver has to hear again
     * that there is no C library and that the code is link-time optimized. */
    val link = listOf("-nostdlib", "-flto") + linker

    /** A list as CMake reads one. */
    fun cmake(flags: List<String>): String = flags.joinToString(";")
}
