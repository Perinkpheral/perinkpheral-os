// SPDX-License-Identifier: MIT
package dev.perinkpheral.cli

import dev.perinkpheral.core.BuildError
import dev.perinkpheral.core.Json
import java.io.File
import java.net.URI
import java.security.MessageDigest
import kotlin.math.ceil

/**
 * The land of the map service: firmware/components/map/map_land_data.c and .h, made of Natural
 * Earth's 1:10m physical land (public domain) at a pinned commit. Land and sea only: no borders,
 * no names. The polygons are rasterised at one arc-minute (21,600 x 10,800 cells, a cell is land
 * when its centre is), each row is kept as the columns where land and sea change, a row is coded
 * against the row above it (a transition deleted, inserted, or moved by a small delta), and the
 * codes are compressed with one static rANS model. Blocks of 64 rows restart, so a row is at most
 * 63 rows of decoding away. Lossless: every row is decoded again here and compared before the
 * files are written, and the host tests decode every row with the firmware's decoder.
 */
object MapData {
    const val SOURCE_COMMIT = "ace5fed0eaf3c6c03c951e75b439ba8fffbc218e"
    const val SOURCE_PATH = "geojson/ne_10m_land.geojson"
    const val SOURCE_SHA256 = "1ac90796408bc6ad6911d69448485d3c4dbf2190370080368a09976e1c9f7416"
    const val SOURCE_URL = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/$SOURCE_COMMIT/$SOURCE_PATH"

    const val WIDTH = 21600
    const val HEIGHT = 10800
    const val BLOCK_ROWS = 64
    private const val SCALE_BITS = 12
    private const val SCALE = 1 shl SCALE_BITS
    private const val RANS_LOW = 1L shl 23

    const val DATA_C = "firmware/components/map/map_land_data.c"
    const val DATA_H = "firmware/components/map/include/map_land_data.h"

    /** The pinned source: [given] when named, else a copy kept between runs, else downloaded.
     * Whichever it is, its SHA-256 must be the pinned one. */
    fun source(given: File?): File {
        val file = given ?: File(System.getProperty("user.home"), ".perinkpheral/sources/ne_10m_land-${SOURCE_COMMIT.take(12)}.geojson")
        if (given == null && !file.isFile) {
            file.parentFile.mkdirs()
            val partial = File(file.path + ".part")
            try {
                URI(SOURCE_URL).toURL().openStream().use { input -> partial.outputStream().use { input.copyTo(it) } }
            } catch (error: java.io.IOException) {
                partial.delete()
                throw BuildError("$SOURCE_URL: ${error.message}; download it and pass --source")
            }
            if (sha256(partial.readBytes()) != SOURCE_SHA256) {
                partial.delete()
                throw BuildError("$SOURCE_URL: not the pinned file (SHA-256 differs)")
            }
            partial.renameTo(file)
        }
        if (!file.isFile) throw BuildError("${file.path}: no such file")
        if (sha256(file.readBytes()) != SOURCE_SHA256) throw BuildError("${file.path}: not $SOURCE_PATH at $SOURCE_COMMIT (SHA-256 differs)")
        return file
    }

    /** Writes the two files into [dir] as the component has them (the component itself, or a
     * scratch folder) and says what they hold. */
    fun make(source: File, dir: File): String {
        val rows = transitions(rasterise(polygons(source)))
        val blocks = rows.toList().chunked(BLOCK_ROWS).map(::codes)
        val (freq, start) = model(blocks)
        val encoded = blocks.map { encode(it, freq, start) }
        // Exhaustive: every row back from the bytes that will be written.
        encoded.forEachIndexed { block, bytes ->
            val first = block * BLOCK_ROWS
            val decoded = decode(bytes, freq, start, minOf(BLOCK_ROWS, HEIGHT - first))
            decoded.forEachIndexed { row, values ->
                if (!values.contentEquals(rows[first + row])) throw BuildError("map: row ${first + row} does not decode to itself")
            }
        }
        val offsets = IntArray(encoded.size + 1)
        encoded.forEachIndexed { index, bytes -> offsets[index + 1] = offsets[index] + bytes.size }
        val stream = ByteArray(offsets.last())
        encoded.forEachIndexed { index, bytes -> bytes.copyInto(stream, offsets[index]) }
        val rowMax = rows.maxOf { it.size }
        File(dir, "include").mkdirs()
        File(dir, "include/" + File(DATA_H).name).writeText(header(encoded.size, rowMax, stream.size, check(rows)))
        File(dir, File(DATA_C).name).writeText(data(freq, start, offsets, stream))
        val total = stream.size + offsets.size * 4 + freq.size * 2 + start.size * 2 + SCALE
        return "$total bytes in flash: $HEIGHT rows of $WIDTH cells, ${encoded.size} blocks, ${stream.size} bytes of codes"
    }

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    /** Every polygon as its rings, each ring as longitude, latitude pairs. */
    private fun polygons(source: File): List<List<DoubleArray>> {
        val features = ((Json.parse(source.readText()) as Map<*, *>)["features"] as? List<*>)
            ?: throw BuildError("${source.path}: no features")
        val out = ArrayList<List<DoubleArray>>()
        for (feature in features) {
            val geometry = (feature as Map<*, *>)["geometry"] as Map<*, *>
            val coordinates = geometry["coordinates"] as List<*>
            val polygons = when (geometry["type"]) {
                "Polygon" -> listOf(coordinates)
                "MultiPolygon" -> coordinates.map { it as List<*> }
                else -> throw BuildError("${source.path}: a ${geometry["type"]} where land should be polygons")
            }
            for (polygon in polygons) {
                out += polygon.map { ring ->
                    val points = ring as List<*>
                    DoubleArray(points.size * 2).also { flat ->
                        points.forEachIndexed { index, point ->
                            val pair = point as List<*>
                            flat[2 * index] = (pair[0] as Number).toDouble()
                            flat[2 * index + 1] = (pair[1] as Number).toDouble()
                        }
                    }
                }
            }
        }
        return out
    }

    /** One bit a cell, rows of longs. A cell is land when its centre is inside a polygon: rings
     * of one polygon by parity, polygons together by union. The arithmetic is the research
     * prototype's, double for double, so the raster is the one that was measured. */
    private fun rasterise(polygons: List<List<DoubleArray>>): Array<LongArray> {
        val words = (WIDTH + 63) / 64
        val raster = Array(HEIGHT) { LongArray(words) }
        val scale = 60.0
        for (polygon in polygons) {
            val crossings = HashMap<Int, DoubleList>()
            for (ring in polygon) {
                val count = ring.size / 2
                var ax = (ring[2 * (count - 1)] + 180) * scale
                var ay = (90 - ring[2 * (count - 1) + 1]) * scale
                for (index in 0 until count) {
                    val bx = (ring[2 * index] + 180) * scale
                    val by = (90 - ring[2 * index + 1]) * scale
                    if (ay != by) {
                        val step = (bx - ax) / (by - ay)
                        val from = maxOf(0, ceil(minOf(ay, by) - .5).toInt())
                        val to = minOf(HEIGHT, ceil(maxOf(ay, by) - .5).toInt())
                        for (y in from until to) crossings.getOrPut(y) { DoubleList() }.add(ax + (y + .5 - ay) * step)
                    }
                    ax = bx
                    ay = by
                }
            }
            for ((y, list) in crossings) {
                val xs = list.sorted()
                var index = 0
                while (index + 1 < xs.size) {
                    val lo = maxOf(0, ceil(xs[index] - .5).toInt())
                    val hi = minOf(WIDTH, ceil(xs[index + 1] - .5).toInt())
                    if (hi > lo) fill(raster[y], lo, hi)
                    index += 2
                }
            }
        }
        return raster
    }

    private class DoubleList {
        private var items = DoubleArray(8)
        private var size = 0
        fun add(value: Double) {
            if (size == items.size) items = items.copyOf(size * 2)
            items[size++] = value
        }
        fun sorted(): DoubleArray = items.copyOf(size).also { it.sort() }
    }

    private fun fill(row: LongArray, from: Int, to: Int) {
        for (x in from until to) row[x ushr 6] = row[x ushr 6] or (1L shl (x and 63))
    }

    /** A row as the columns where it changes, starting from sea at column 0: a row whose first
     * cell is land has a transition at 0. */
    private fun transitions(raster: Array<LongArray>): Array<IntArray> = Array(HEIGHT) { y ->
        val row = raster[y]
        val out = ArrayList<Int>()
        var land = false
        for (x in 0 until WIDTH) {
            val bit = (row[x ushr 6] ushr (x and 63)) and 1L != 0L
            if (bit != land) {
                out += x
                land = bit
            }
        }
        out.toIntArray()
    }

    private fun bits(value: Int): Int {
        var x = value
        var n = 0
        do {
            n++
            x = x ushr 1
        } while (x != 0)
        return n
    }

    private fun zigzag(delta: Int): Int = if (delta >= 0) 2 * delta else -2 * delta - 1

    private fun uleb(out: java.io.ByteArrayOutputStream, value: Int) {
        var n = value
        while (n >= 128) {
            out.write((n and 127) or 128)
            n = n ushr 7
        }
        out.write(n)
    }

    /** The codes of a block of rows, each row against the one above it (the first against none):
     * 0 ends a row, 1 drops the next transition above, 2 and a ULEB gap inserts one after the last
     * of this row, 4 + zigzag(delta) (or 3 and a ULEB zigzag past 250) moves the next one above.
     * Which of the three is chosen is the cheapest alignment, ties to moving, then dropping. */
    private fun codes(block: List<IntArray>): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        var previous = IntArray(0)
        for (current in block) {
            val n = previous.size
            val m = current.size
            val cost = Array(n + 1) { IntArray(m + 1) }
            val op = Array(n + 1) { ByteArray(m + 1) }
            fun insertCost(j: Int) = 12 + bits(current[j - 1] - (if (j > 1) current[j - 2] else 0))
            for (i in 1..n) { cost[i][0] = cost[i - 1][0] + 10; op[i][0] = 1 }
            for (j in 1..m) { cost[0][j] = cost[0][j - 1] + insertCost(j); op[0][j] = 2 }
            for (i in 1..n) for (j in 1..m) {
                val z = zigzag(current[j - 1] - previous[i - 1])
                var best = cost[i - 1][j - 1] + 2 + bits(z)
                var choice: Byte = 3
                val drop = cost[i - 1][j] + 10
                if (drop < best) { best = drop; choice = 1 }
                val insert = cost[i][j - 1] + insertCost(j)
                if (insert < best) { best = insert; choice = 2 }
                cost[i][j] = best
                op[i][j] = choice
            }
            val path = ArrayList<Byte>()
            var i = n
            var j = m
            while (i > 0 || j > 0) {
                val x = op[i][j]
                path += x
                if (x.toInt() != 2) i--
                if (x.toInt() != 1) j--
            }
            path.reverse()
            i = 0
            j = 0
            for (x in path) when (x.toInt()) {
                1 -> { out.write(1); i++ }
                2 -> { out.write(2); uleb(out, current[j] - (if (j > 0) current[j - 1] else 0)); j++ }
                else -> {
                    val z = zigzag(current[j] - previous[i])
                    if (z <= 250) out.write(z + 4) else { out.write(3); uleb(out, z) }
                    i++
                    j++
                }
            }
            out.write(0)
            previous = current
        }
        return out.toByteArray()
    }

    /** One model for every block: byte frequencies scaled to 4096, none below 1, rounding
     * corrected one step at a time where it is furthest off (the prototype's order). */
    private fun model(blocks: List<ByteArray>): Pair<IntArray, IntArray> {
        val counts = LongArray(256)
        blocks.forEach { block -> block.forEach { counts[it.toInt() and 255]++ } }
        val total = counts.sum().toDouble()
        val ideal = DoubleArray(256) { (counts[it] * SCALE).toDouble() / total }
        val freq = IntArray(256) { maxOf(1, ideal[it].toInt()) }
        fun firstMax(score: (Int) -> Double): Int {
            var best = 0
            for (s in 1 until 256) if (score(s) > score(best)) best = s
            return best
        }
        while (freq.sum() > SCALE) freq[firstMax { freq[it] - ideal[it] }]--
        while (freq.sum() < SCALE) freq[firstMax { ideal[it] - freq[it] }]++
        val start = IntArray(257)
        for (s in 0 until 256) start[s + 1] = start[s] + freq[s]
        return freq to start
    }

    /** Byte rANS, written backwards so that it reads forwards: the state as four bytes, then
     * the bytes renormalisation takes, in reading order. */
    private fun encode(codes: ByteArray, freq: IntArray, start: IntArray): ByteArray {
        var x = RANS_LOW
        val out = java.io.ByteArrayOutputStream()
        for (index in codes.indices.reversed()) {
            val s = codes[index].toInt() and 255
            val limit = (RANS_LOW shr SCALE_BITS) * 256 * freq[s]
            while (x >= limit) {
                out.write((x and 255).toInt())
                x = x shr 8
            }
            x = ((x / freq[s]) shl SCALE_BITS) + x % freq[s] + start[s]
        }
        val tail = out.toByteArray().reversedArray()
        return byteArrayOf((x and 255).toByte(), (x shr 8 and 255).toByte(), (x shr 16 and 255).toByte(), (x shr 24 and 255).toByte()) + tail
    }

    /** What map_land.c does with a block, to prove the bytes before they are kept. */
    private fun decode(bytes: ByteArray, freq: IntArray, start: IntArray, rows: Int): List<IntArray> {
        val symbol = IntArray(SCALE)
        for (s in 0 until 256) for (slot in start[s] until start[s + 1]) symbol[slot] = s
        var at = 4
        var x = (bytes[0].toLong() and 255) or ((bytes[1].toLong() and 255) shl 8) or
            ((bytes[2].toLong() and 255) shl 16) or ((bytes[3].toLong() and 255) shl 24)
        fun next(): Int {
            val slot = (x and (SCALE - 1).toLong()).toInt()
            val s = symbol[slot]
            x = freq[s] * (x shr SCALE_BITS) + slot - start[s]
            while (x < RANS_LOW) {
                if (at >= bytes.size) throw BuildError("map: a block reads past its end")
                x = (x shl 8) or (bytes[at++].toLong() and 255)
            }
            return s
        }
        fun varint(): Int {
            var value = 0
            var shift = 0
            while (true) {
                val b = next()
                value = value or ((b and 127) shl shift)
                if (b and 128 == 0) return value
                shift += 7
            }
        }
        val out = ArrayList<IntArray>()
        var previous = IntArray(0)
        repeat(rows) {
            val current = ArrayList<Int>()
            var i = 0
            while (true) {
                val op = next()
                if (op == 0) break
                when (op) {
                    1 -> i++
                    2 -> current += (current.lastOrNull() ?: 0) + varint()
                    else -> {
                        val z = if (op == 3) varint() else op - 4
                        current += previous[i++] + (if (z and 1 != 0) -((z + 1) shr 1) else z shr 1)
                    }
                }
            }
            previous = current.toIntArray()
            out += previous
        }
        if (at != bytes.size || x != RANS_LOW) throw BuildError("map: a block does not end where it should")
        return out
    }

    /** FNV-1a over every row, its count and its transitions as 16-bit numbers: what the host
     * test of the firmware's decoder must get back. */
    private fun check(rows: Array<IntArray>): Long {
        var hash = -0x340d631b7bdddcdbL
        fun mix(value: Int) {
            for (shift in intArrayOf(0, 8)) {
                hash = hash xor ((value shr shift) and 255).toLong()
                hash *= 0x100000001b3L
            }
        }
        rows.forEach { row -> mix(row.size); row.forEach(::mix) }
        return hash
    }

    private const val PROVENANCE = """ * Made by tools/perinkpheral map from Natural Earth 1:10m physical land
 * (public domain, naturalearthdata.com): $SOURCE_PATH of github.com/nvkelso/natural-earth-vector
 * at $SOURCE_COMMIT, SHA-256 $SOURCE_SHA256.
 * Land and sea only: no borders, no names. Run the command again, do not edit this file."""

    private fun header(blocks: Int, rowMax: Int, streamBytes: Int, check: Long) = """/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
$PROVENANCE
 */
#pragma once

#include <stdint.h>

/* One arc-minute cells: column 0 starts at 180 W, row 0 at 90 N. */
#define MAP_LAND_WIDTH      $WIDTH
#define MAP_LAND_HEIGHT     $HEIGHT
/* Rows coded together; each block starts from a row of its own. */
#define MAP_LAND_BLOCK_ROWS $BLOCK_ROWS
#define MAP_LAND_BLOCKS     $blocks
/* The most transitions any row has: the decoder's rows are this long. */
#define MAP_LAND_ROW_MAX    $rowMax
/* rANS: probabilities in 1/4096, the state kept at 2^23 or more. */
#define MAP_LAND_SCALE_BITS $SCALE_BITS
#define MAP_LAND_STREAM_BYTES $streamBytes
/* FNV-1a of every row's count and transitions, as 16-bit little-endian numbers. */
#define MAP_LAND_CHECK      0x${"%016x".format(check)}ull

extern const uint16_t map_land_freq[256];
extern const uint16_t map_land_cumulative[256];
extern const uint8_t  map_land_symbol[1 << MAP_LAND_SCALE_BITS];
/* Where each block's bytes begin in the stream; the last entry is its end. A block is its
 * rANS state, four bytes little-endian, and the bytes the decoder reads after it. */
extern const uint32_t map_land_block[MAP_LAND_BLOCKS + 1];
extern const uint8_t  map_land_stream[MAP_LAND_STREAM_BYTES];
"""

    private fun data(freq: IntArray, start: IntArray, offsets: IntArray, stream: ByteArray): String {
        val symbol = IntArray(SCALE)
        for (s in 0 until 256) for (slot in start[s] until start[s + 1]) symbol[slot] = s
        fun table(type: String, name: String, size: String, values: List<String>, perLine: Int) = buildString {
            append("const $type $name[$size] = {\n")
            values.chunked(perLine).forEach { line -> append("    ").append(line.joinToString(",")).append(",\n") }
            append("};\n")
        }
        return """/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
$PROVENANCE
 */
#include "map_land_data.h"

""" + table("uint16_t", "map_land_freq", "256", freq.map { it.toString() }, 16) + "\n" +
            table("uint16_t", "map_land_cumulative", "256", start.take(256).map { it.toString() }, 16) + "\n" +
            table("uint8_t", "map_land_symbol", "1 << MAP_LAND_SCALE_BITS", symbol.map { it.toString() }, 32) + "\n" +
            table("uint32_t", "map_land_block", "MAP_LAND_BLOCKS + 1", offsets.map { it.toString() }, 10) + "\n" +
            table("uint8_t", "map_land_stream", "MAP_LAND_STREAM_BYTES", stream.map { "0x%02x".format(it.toInt() and 255) }, 24)
    }
}
