# 0007: RAM savings study

Date: 2026-11-24

Status: rejected

## Context

The ESP32-C6 has no PSRAM. The study evaluated the then-current 144 KiB
application area, which reduced heap available to Perinkpheral OS and TLS. It
evaluated additional savings without changing wasm3, ABI capacities, display
behavior or admission thresholds.
Streaming glyph uploads are covered by [0003](0003-streaming-glyph-uploads.md).

## Decision

The owner closed the study on 2026-11-24 and decided to take none of the changes:
"no real point". This includes the conservative heap-placement and poller
prototypes recommended by the report. No prototype is adopted into main.
Preserve the research on branch `research/ram-savings` as evidence for a future
RAM requirement.

## Evidence

Sources: the owner-supplied RAM savings study `report.md`, and the
[full audit](https://github.com/Perinkpheral/perinkpheral-os/blob/7906cc0/docs/testing/ram-savings.md)
with [machine-readable measurements](https://github.com/Perinkpheral/perinkpheral-os/blob/7906cc0/docs/testing/ram-savings.json)
on `research/ram-savings`. The audit is based on `a884685`, built as release
0.1.95 with ESP-IDF 5.5.5. KiB means 1,024 bytes.

### Ranked candidates

The order follows the report's assessment of release suitability, before the
owner rejected adoption. M denotes a build measurement, C a computed saving,
and E an estimate. Transient ceilings overlap and must not be added together;
they are not idle reservations or measured on-device free-heap gains.

| Rank | Candidate | KiB saved and scope | Risk | Visible effect |
| --- | --- | --- | --- | --- |
| 1 | Share picker/stream lookahead | 1 C per active application request; target layout measured | Low | None intended; same data and limits |
| 2 | Heap implementation in flash | 1.34 M static SRAM | Low; cache-off callers must remain safe | Possible allocation latency |
| 3 | Wi-Fi sleep code in flash | 13.24 M static SRAM, incremental after heap placement | Medium; radio and battery validation absent | Beacon/wake timing and battery duty cycle |
| 4 | TLS OUT 4096 -> 2048 bytes | Up to 2 C during full-size TX allocation; zero idle | Low/medium; largest requests and ClientHello need compatibility tests | More TLS records |
| 5 | Smaller task stacks | 4.5-6.5 E | Medium; runtime high-water marks absent | Stack overflow if undersized |
| 6 | Disable unused client session tickets | 0.12-0.22 C from observed transient payloads | Low | No current session resumption lost |
| 7 | Remove per-tag log cache | About 0.25 C static | Low; preserve global log setting | None intended |
| 8 | Wi-Fi static data RX 4 -> 3 or 2 | About 1.56 or 3.13 C while initialized | Medium/high | Burst loss and retries on weak radio |
| 9 | Management RX tuning | About 0.98-2.44 C, depending on option | Medium | Scanning/association behavior or allocator traffic |
| 10 | Dynamic RX/TX caps 8 -> 4 | Zero idle; up to 6.25 E per direction during bursts | Medium/high | Drops, retries and reduced throughput |
| 11 | Smaller TCP windows, oversize or out-of-order queues | Up to 1.41 C per direction for windows, 1.41 C TX tail or 2.81 C out-of-order payload per connection | Medium/high | Throughput, copies and retransmits |
| 12 | Sockets 12 -> 11; web clients 7 -> 6 | 0.21 C idle, plus traffic-dependent demand | Medium | Less browser/setup concurrency |
| 13 | HTTP URI/header-array sizing | 0.25 / 0.03 C | Low/medium | Shorter URI or response-header limits |
| 14 | LittleFS caches 512 -> 256 bytes | 0.5 C per mount plus 0.25 per open file | Medium | More flash traffic and latency |
| 15 | Log ring 4 -> 2 KiB | 2 C static, another 2 during readback | Low code risk | Half the diagnostic history; unsuitable during endurance |
| 16 | Start captive DNS task only when needed | At least 3.33 C in station-only mode | Medium | AP/rescue lifecycle changes |
| 17 | Compact deadline/poll records | 1.36 / 0.38 C static | Low/medium | None intended; preserve ordering and identity |
| 18 | Narrow manifest snapshots/deduplicate scenes | Up to 6.47 C transient / 7.17 C permanent | Medium/high | Reload ownership risks |
| 19 | Smaller event queue/JSON nodes | 0.88 C / 0.78 C per 100 nodes | Medium | Event loss or parser ownership changes |
| 20 | Request queue cap 16 -> 8 KiB | Zero idle; up to 8 C at saturation | Medium | More busy refusals |

The stack estimate comprises buttons, Wi-Fi plan, DNS and mDNS reductions of
1 KiB each, plus 0.5 KiB from esp_timer. Web and poller reductions could add
another 2 KiB only with measured margin. Keep the 12 KiB main stack. Compiler
function-frame sizes do not establish runtime call-chain peaks; any future cut
requires high-water coverage and at least 1 KiB spare after the proposed cut.

### Candidates rejected on technical grounds

| Candidate | Nominal saving | Risk and visible effect | Finding |
| --- | --- | --- | --- |
| TLS IN 16384 -> 4096 bytes | Up to 12 KiB, workload-dependent | High; incompatible server records can fail requests | Required probed servers did not negotiate `max_fragment_length`; no universal reduction is supported |
| Remove one display frame | 14.65 KiB C | High; rendering and refresh redesign | Both 15,000-byte frames support complete-frame composition, equality/damage detection and the retained screen copy |
| Smaller root bundle | Approximately zero steady RAM | Medium; reduced trusted-server coverage | The 150-root, 67.37 KiB bundle is in flash; its BSS pointer is four bytes and only the selected key is parsed |
| Reduce wasm3 overhead within its fixed area | Zero OS heap | Application failures if the reservation is cut | Internal savings do not recover heap at the study's fixed 144 KiB reservation |

TLS 1.2 probes offered a 4096-byte maximum fragment length. No server extension
was negotiated by `api.wheretheiss.at`, `raw.githubusercontent.com`,
`release-assets.githubusercontent.com`, `www.githubstatus.com`,
`discordstatus.com` or `status.openai.com`. Other probed servers did negotiate
it, so this is not a claim that all servers lack support. User-configured
Calendar, Home Assistant and router destinations remain unbounded. Firmware
also does not call `mbedtls_ssl_conf_max_frag_len`; server support alone would
not enable a smaller IN buffer.

Dynamic TLS RX/TX, configuration/CA release, no retained peer certificate,
disabled IPv6/BLE and major IRAM optimizations already exist. Headline savings
for enabling those options are not additional opportunities in this build.
Keep `L2_TO_L3_COPY`: removing it reproduces unread-upload/AP starvation.

### Prototypes and validation

The following commits remain on `research/ram-savings`:

- `7a9cc01`: heap placement; static SRAM 174,308 -> 172,932 bytes, saving
  1,376 bytes (1.34 KiB), including dependent SPI ISR placement and layout.
- `0578a1c`: picker/stream union; target `request_t` 9,536 -> 8,512 bytes,
  saving 1,024 bytes per active application request, not at boot.
- `445151e`: optional Wi-Fi overlay; static SRAM 172,932 -> 159,370 bytes,
  saving another 13,562 bytes (13.24 KiB), including its power-management
  dependency. The overlay is excluded from release defaults.
- `7906cc0`: audit and measurement evidence.

Together the placement prototypes reduce static SRAM by 14.59 KiB, or combined
demand by 15.59 KiB during an application request with the union. These are
build/layout results. The study reports successful main/recovery release builds
and partition checks, host tests (103,459 checks, zero failures), and simulator
tests (8 scenarios, 34 pictures, zero failures). No device validation, runtime
stack high-water measurements or actual before/after free-heap measurements
were performed. Radio, sleep/wake, weak-signal and battery behavior remain
untested for the optional overlay.

## Consequences

These prototypes remain rejected. [0011](0011-page-32k.md) subsequently reduced
the shared application area to 112 KiB. The 2026-11-24 rejection retained firmware
configuration, request storage, task stacks and capacities as they were at that
time: both frames, the full root bundle, 16 KiB TLS IN, the 144 KiB application
area, TLS admission at 60 KiB free / 17 KiB contiguous and critical pressure
at 12 KiB. No endurance or load-test acceptance
is inferred from build, host or simulator results. The research branch preserves
the prototypes without merging them into main.

## Reopen when

A new feature needs RAM, or failures in the
[endurance](../testing/stability.md) or [load tests](../testing/load.md) require
revisiting RAM demand. Reassess the relevant allocations and validation gates
against the failing workload or new requirement before adopting a prototype.
