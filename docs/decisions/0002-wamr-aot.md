# 0002: Replace wasm3 with WAMR AOT

Date: 2026-11-24

Status: rejected

## Context

Ahead-of-time compilation could remove interpreter dispatch and on-device
bytecode compilation. The proposal must preserve untrusted catalog applications,
ABI 3 behavior, recoverable handler termination, installation capacity and local
development. ESP32-C6 has no PSRAM; `appcode` is 320 KiB and modules are limited
to 48 KiB.

## Decision

Retain wasm3. WAMR AOT is build-feasible but does not provide an equivalent
replacement without changes to trust, deadlines, packages and tooling. No AOT
runtime or trusted compilation/signing service is adopted.

## Evidence

Source reports: `no-interpreter/1/report.md`, `no-interpreter/2/report.md` and
`no-interpreter/3/report.md`. All work was local, without device execution.

The first study built WAMR 2.4.3 (`fd3c104`) with LLVM 22.1.8 and ESP-IDF 5.5.5
for RV32/ILP32. XIP compilation used RV32IMAC, software memory bounds checks and
native-stack bounds checks. Hello grew from 880 to 2,924 bytes; Calendar from
15,225 to 44,710 bytes. Both had zero relocation groups. Calendar's flash slot,
including the 32-byte header and 4 KiB sector rounding, grew from 16 to 44 KiB.

| Standalone test configuration | Binary (bytes) | Static DIRAM (bytes) |
| --- | ---: | ---: |
| WAMR AOT only | 197,232 | 54,748 |
| WAMR AOT plus wasm3 | 245,296 | 54,756 |

WAMR itself contributed 36,876 flash bytes and 1,912 static RAM bytes, excluding
dependencies. These are minimal images, not full-OS deltas.

The two WAMR 2.4.6 studies measured the same RV32IMAC/ILP32 XIP profile with
memory/stack checks. Seventeen catalog modules grew from 134,372 to 397,310 raw
bytes; rounded storage grew from 41 to 105 sectors (164 to 420 KiB), exceeding
the 80-sector partition by 100 KiB. Eight preinstalled applications grew from
64 to 172 KiB of slots. The frozen ABI probe grew from 16,557 to 56,718 bytes,
exceeding the module limit. Repeated builds matched for all 17 applications on
one host; cross-host reproducibility was not tested.

The studies identified these constraints:

- WAMR treats AOT binaries as trusted. Its optional validator does not establish
  native-instruction safety. Arbitrary AOT can bypass generated checks; catalog
  hashes and HTTPS do not prove trusted compilation. See the upstream
  [security model](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/fd3c104fff67a0df47b08b2d59bb4229b906d2d4/doc/security_need_to_know.md)
  and [AOT validator](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/WAMR-2.4.6/core/iwasm/aot/aot_validator.c).
- A compiled non-shared-memory infinite loop had no termination check despite
  enabled memory/stack checks. The cooperative 3,000 ms handler stop requires
  compiler instrumentation and deadline integration; a watchdog reset does not
  preserve application-stop behavior. See
  [compiler control-flow checks](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/fd3c104fff67a0df47b08b2d59bb4229b906d2d4/core/iwasm/compilation/aot_emit_control.c#L938).
- The 65,536-byte linear memory remains, together with metadata, tables, execution
  state and native task-stack use. WAMR 2.4.3 requested 65,544 bytes for linear
  memory outside the configured runtime pool; a 144 KiB pool was additional.
  RV32 headers measured 240 bytes for a module, 464 for an instance and 72 for
  an execution environment, not complete allocation totals. See
  [memory allocation](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/fd3c104fff67a0df47b08b2d59bb4229b906d2d4/core/iwasm/common/wasm_memory.c#L2061).
- The ESP-IDF port returns no native-stack boundary without FreeRTOS tracing,
  disabled in the inspected firmware. Supply the boundary or integrate the port
  before relying on checks. See the
  [platform implementation](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/WAMR-2.4.6/core/shared/platform/esp-idf/espidf_platform.c).
- C6 instruction mapping makes XIP plausible; 64 KiB mapping pages do not require
  one full page per application. Mapping does not provide isolation. Relocation
  exceptions and cache-disabled flash writes require integration tests. See
  [WAMR XIP](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/WAMR-2.4.6/doc/xip.md),
  [ESP-IDF mapping](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/peripherals/spi_flash/index.html)
  and [flash concurrency](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/peripherals/spi_flash/spi_flash_concurrency.html).

## Consequences

The interpreter's RAM and execution costs remain. AOT speed, energy use and net
RAM savings were not measured on C6. No matched full-OS image delta was measured.

A future AOT option needs authenticated trusted compilation, allocator/stack
integration, equivalent deadlines, preserved import/privilege/pointer checks,
package migration and diagnostics for separately loaded code. AOT has a target
and runtime compatibility profile in addition to ABI 3; WAMR recommends matching
compiler/runtime versions and documents format incompatibilities. See
[compatibility guidance](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/main/doc/build_wasm_app.md#aot-compiled-module-compatibility-among-wamr-versions).
Interpreter fallback retains another runtime and its worst-case RAM requirement.
Desktop simulation and native sanitizer stand-ins do not validate RV32 AOT.
WAMR also adds Apache-2.0 WITH LLVM-exception notice obligations; see its
[license](https://github.com/wasm-micro-runtime/wasm-micro-runtime/blob/WAMR-2.4.6/LICENSE).

## Reopen when

After release, a separate prototype demonstrates equivalent isolation and safe
handler termination, hardware-measured RAM/stack and performance gains, viable
flash capacity, and an acceptable compilation/distribution/compatibility model.
