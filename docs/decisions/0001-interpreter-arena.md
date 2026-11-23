# 0001: Interpreter arena from 64 to 80 KiB

Date: 2026-11-24

Status: accepted

## Context

The ESP32-C6 has 512 KiB SRAM and no PSRAM. One application instance runs at a
time. Its fixed 64 KiB WebAssembly linear memory is separate from interpreter
working memory. The former 64 KiB interpreter budget did not cover all exercised
application paths. Increasing the startup reservation reduces system/TLS heap.

## Decision

Reserve 80 KiB for interpreter allocations and retain the 64 KiB linear-memory
contract: 144 KiB total, 147,456 bytes, allocated at startup and reset on unload.
The increase is 16,384 bytes. Keep the one-page limit, 8 KiB wasm stack, 4 KiB
interpreter stack and 512-byte interpreter code pages. Raise the builder's warning
budget to 80 KiB; an estimate above the budget remains a warning.

The decision includes size-aware glyph-upload admission and explicit reporting of
failed startup reservations. It does not adopt the deferred streaming prototype
in [0003](0003-streaming-glyph-uploads.md).

## Evidence

Source reports: `size-astras/measure-report.md`, `interp-80/astra-risk.md`, and
`interp-80/astra-confirm-1.md`, `astra-confirm-2.md`, `astra-confirm-3.md`.
The measurement implementation and hashed results are preserved on
`research/interpreter-measure`, commit `26f42ed`; see the
[measurement dataset](https://github.com/Perinkpheral/perinkpheral-os/blob/26f42ed4dacf324db2fe3eb91112c21abf7c9717/tools/simulator/measurements/2026-11-24.json).

Measured peaks below include allocator overhead and temporary allocations above
the 64 KiB wasm page. The study ran 60 cases at each arena size with 32-bit ARM
under QEMU, firmware wasm3 settings, the actual application-area allocator and
ESP-IDF 5.5.5. Interpreter structure sizes matched the C6 RISC-V compiler.

| Application | Build estimate (KiB) | Measured peak (KiB) |
| --- | ---: | ---: |
| Calendar | 62.6 | 56.5 |
| Stocks | 38.1 | 40.1 |
| Weather | 31.4 | 33.1 |
| HA | 38.2 | 36.9 |
| Earthquakes | 85.8 | 84.1 |
| Space Launch | 64.0 | 65.0 |

Earthquakes and Space Launch peaks required a larger diagnostic arena. At the
actual former 128 KiB combined limit, Earthquakes failed an 8,128-byte map-scratch
allocation and later code allocations; Space Launch failed a 512-byte code-page
allocation. The measured Earthquakes peak also exceeds the new 80 KiB working
budget: this increase does not establish that every application fits.

The estimate `11,264 + 4 * wasm code-section bytes + 8,128 if gfx_draw_map is
imported` is not consistently conservative. Lazy compilation, instruction mix,
function records, page slack and moving reallocations affect peaks. The research
branch adds an opt-in `PERINKPHERAL_ARENA_PROBE` that compiles every function with
the 32-bit interpreter and reports a separate 8 KiB reserve; recommendations were
92.1 KiB for Earthquakes and 73.1 KiB for Space Launch. That reserve is a heuristic.

The risk review examined 819 main-firmware samples across six versions, about
14 hours with restarts; maximum observed uptime was 3 h 22 min. It found one
memory refusal, zero allocation failures and a minimum recorded heap of 48,248
bytes. Subtracting the increase yields 31,864 bytes; this is an extrapolation,
not a candidate measurement or prediction of the largest contiguous block.

Three reviews confirmed the 147,456-byte startup allocation in disassembly.
Reported checks included 103,349 host assertions, core checks, browser suites,
ABI/guide/API checks and partition checks. Reviewed release images were
1,599,776 bytes for main and 855,856 bytes for recovery; the development main
image was 1,686,928 bytes. These precede subsequent guard changes. Some PDF
rebuilds failed during Chromium teardown. Ordinary simulator/host runtime tests
do not enforce the combined device arena. No review contacted a device.

## Memory guards and consequences

- Glyph admission in [memory.c](../../firmware/main/memory.c) requires body size
  plus 32 KiB free and body size plus 16 KiB contiguous. This covers the body,
  two 4 KiB compaction buffers, and 24 KiB total/8 KiB contiguous reserve.
  A maximum 32 KiB body requires 64 KiB free/48 KiB contiguous. Admission or
  subsequent allocation failure returns the standard HTTP 503 memory refusal.
  Implemented by commit `7b444ad` with boundary and browser regression coverage.
- Startup reservation failure is logged; `app_area_available` reports actual
  availability while `app_area` retains its configured-size meaning. Applications
  show `App memory unavailable.` Main remains available for the page and recovery
  entry; freeing heap later does not create the missing reservation. Implemented
  by commit `ca68cf9`, including allocation/registration-failure tests.
- Existing TLS admission remains 60 KiB total/17 KiB contiguous; page admission
  remains 24/8 KiB and screenshot admission 40/16 KiB. These checks observe heap,
  without reserving it against concurrent allocations.
- Existing pressure handling sheds web connections below 12 KiB total, below a
  4 KiB block, or after a new allocation failure. Three consecutive checks at
  five-second intervals trigger orderly restart; ten minutes of unresolved
  session admission pressure also triggers restart. A healthy heap observation
  clears that interval. Refusal and failed-allocation counters remain exposed.
- Less system heap increases overlap risk for TLS, two optional 4 KiB CA files,
  a 16 KiB request queue, browser traffic and translation buffers up to 8 KiB.
  Recovery has no application arena and must retain independent size checks.

Release qualification requires the exact candidate's
[72-hour endurance run](../testing/stability.md) and
[load scenarios](../testing/load.md), including glyph/TLS overlap, both CA roots,
full queues, reservation checks across boots and recovery handoff. Host checks
and historical low-water samples do not establish multi-day fragmentation safety.

## Reopen when

Measured application paths fail within 80 KiB, the candidate cannot retain stable
system-heap/contiguous-block headroom, or endurance/load tests show allocation
failures or unexplained restarts. Evaluate application/code reductions and measured
RAM savings before increasing the reservation again.
