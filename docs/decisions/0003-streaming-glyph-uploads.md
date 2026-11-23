# 0003: Stream glyph-pack uploads to flash

Date: 2026-11-24

Status: deferred

## Context

The existing upload allocates a complete pack, at most 32 KiB, and compaction
can allocate two 4 KiB buffers. This competes with TLS and system heap after the
interpreter-area increase. The 64 KiB glyph partition and v1 format must remain
readable by recovery. Feature freeze limits pre-release changes.

## Decision

Defer streaming uploads until after release. Retain whole-pack upload with the
size-aware admission in [0001](0001-interpreter-arena.md).
The prototype lives on branch `research/glyph-stream`: implementation `8155252`,
design/results `9d04f2c`. Its versioned
[design note](https://github.com/Perinkpheral/perinkpheral-os/blob/9d04f2c2677b024cfa3663f8a2c4c548dd99ed03/docs/research/glyph-stream.md)
is evidence, not hardware acceptance or an adopted main-branch mechanism.

## Finding

The prototype retains `POST /api/glyphs`, Content-Length, the 32 KiB limit and
v1 bytes. It allocates one 1,024-byte receive buffer. The main event loop selects
and erases a free contiguous extent, then writes chunks between draws without
waiting for network input. The new pack's first four `PGLY` bytes are withheld.
Validation reads mapped flash, checks the full pack and compares received versus
read-back CRC-32; magic is committed last. Old versions remain until the new
pack validates and commits, then their magic is tombstoned.

Insufficient contiguous space returns permanent HTTP 409 without changing an
installed pack. Destructive compaction is removed. An interior sector beginning
with `PGLY` is rejected to prevent an interrupted upload exposing a nested pack.
A cut after commit but before retirement can leave two valid versions; physical
scan order may select either. The v1 format has no generation metadata or
persistent checksum. Lost responses must not trigger automatic replay.

## Evidence

Sources: `glyph-stream/report.md` and the branch design note above. No device
was contacted. Baseline `a884685` and prototype release builds both used version
0.1.95 and ESP-IDF 5.5.5.

| Measurement | Existing upload | Prototype |
| --- | ---: | ---: |
| Latin admission, total free bytes | 64,732 | 25,600 |
| Latin admission, contiguous bytes | 48,348 | 9,216 |
| Main image bytes | 1,600,272 | 1,600,912 |
| Main flash `.text` bytes | 956,482 | 957,110 |
| Recovery image bytes | 855,952 | 855,984 |

Static DIRAM remained 174,308 bytes. Main grew 640 bytes and retained 496,240
bytes of slot space; recovery grew 32 bytes and retained 61,520 bytes. Both
partition checks passed.

Controlled requested heap peaks at 1,108 bytes: 1,024-byte receive buffer plus
an 84-byte completion semaphore. Allocator metadata, sockets and platform
transients are excluded; this is not a measured system peak. Transaction state
is 48 bytes on the 32-bit target's existing server stack. Admission is fixed at
25 KiB total/9 KiB contiguous for any valid pack, but remains an observation.

Generated Latin, Cyrillic and Greek packs were 31,964, 18,760 and 1,428 bytes,
occupying 8, 5 and 1 sectors. All three occupy 14 of 16 sectors: replacing the
eight-sector Latin pack while retaining all three cannot fit. Latin alone can
hold eight old plus eight new sectors. Fragmentation can also cause 409 even
when aggregate free space suffices.

The reported host suite passed 59 C test reports and 2,989,735 assertions.
Synthetic replacement tests covered byte-prefix cuts across all 42 erase/write
operations; the real Latin invocation passed 3,043,012 assertions, including
overlapping common tests. Browser update/request/settings suites, `pageTest`
and the 35-route API check passed. The tests model byte-prefix NOR operations;
they do not establish analog brownout, arbitrary partial erase or flash wear.

The design inspected IDF receive, partition-write and mapped-cache invalidation
code. Relevant upstream interfaces are
[HTTP server receive](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/protocols/esp_http_server.html),
[partition access](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/storage/partition.html)
and [flash concurrency](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/peripherals/spi_flash/spi_flash_concurrency.html).

## Consequences

Streaming reduces controlled upload allocations but changes full/fragmented
storage behavior. Transparent replacement may require durable scratch storage
or a separate format/layout design. Adding unsafe compaction is not an accepted
substitute. The HTTP server task still handles the receive synchronously.

The source estimate is one to two engineering days plus owner-operated physical
power cuts for the current space policy. Hardware acceptance must cover mapped
cache behavior, redraws, TLS overlap, heap/stack minima, slow/interrupted uploads,
full/fragmented storage, duplicate versions, recovery rendering and power cuts
at erase/write/commit/retirement. Transparent compaction and reliable newest-copy
selection are separate work. Removing USB with a connected battery is not a
power-cut test.

## Reopen when

The release is complete and the owner accepts the explicit 409 space policy.
Complete review and hardware acceptance before adoption; redesign separately if
automatic compaction or deterministic newest-version recovery is required.
