# 0004: Lossless one-arc-minute land data without borders or names

Date: 2026-11-23

Status: accepted

## Context

The map service needs physical land/coastlines within the ESP32-C6 flash budget.
The initial one-arc-minute raster occupied 506,137 bytes. A 150 KB target was
studied, subject to preserving coastline detail at the finest 500 m/pixel view.

## Decision

Retain the one-arc-minute land raster and compress it losslessly using matched
row transitions and static byte rANS, approximately 220 KB before optional
acceleration tables. Do not include borders, country names or city names.
City labels are rejected because name selection is contested and would require
a maintained naming policy.

## Evidence

Sources: `research-map/compress/sol-1.md`, `sol-2.md`, and
`research-map/cities/REPORT.md`, including the detailed city report it references.
Both compression studies reproduced the 506,137-byte baseline from pinned
[Natural Earth 1:10m physical land](https://www.naturalearthdata.com/downloads/10m-physical-vectors/10m-land/).
Only physical land was used; no administrative geometry or names were required.

| Study and candidate | Measured bytes | Fidelity |
| --- | ---: | --- |
| Study 1: one-minute transitions plus arithmetic coding | 283,576 | Lossless |
| Study 1: two-minute transitions plus rANS | 148,071 | Coastlines changed |
| Study 2: one-minute matched transitions plus rANS, 64-row restarts | 220,076 | Lossless |
| Study 2: two-minute matched rANS plus exact world overview | 142,000 | Regional coastlines changed |

No tested candidate at or below 150,000 bytes preserved baseline pixels
everywhere. One-minute cells span approximately 1.85 km north-south, or 3.7
pixels at 500 m/pixel; two-minute cells span 7.4 pixels. Additional two-minute
close-ups changed 1,263 Tokyo pixels and 1,885 Istanbul pixels out of 104,000
each, and closed the Bosporus. An exact world overview does not repair regional
topology. Island removal below 0.25 square km saved only 32 bytes in study 2;
study 1's arithmetic variant saved 46 bytes.

Study 2 estimated the lossless candidate at 0.6-2 seconds on C6 and 15 KiB
additional RAM. These are estimates, excluding existing framebuffers and panel
refresh, not hardware timings or the final firmware's memory requirement.

The implemented [data header](../../firmware/components/map/include/map_land_data.h)
defines 21,600 by 10,800 cells, 64-row blocks, 169 blocks and a 218,368-byte
compressed stream. The research size excludes optional acceleration tables;
it is not the entire map component's linked size. The specification records a
233,456-byte main-image increase (1,366,304 to 1,599,760 bytes) for map data and
code, leaving 497,392 bytes in the main slot at introduction. Recovery omits maps.
The generator pins Natural Earth commit `ace5fed0eaf3c6c03c951e75b439ba8fffbc218e`
and validates its source hash; see [MapData.kt](../../tools/cli/src/main/kotlin/dev/perinkpheral/cli/MapData.kt).
The upstream [byte-rANS implementation](https://github.com/rygorous/ryg_rans/blob/master/rans_byte.h)
is cited by study 1.

The city prototype evaluated 16 map variants at 400 by 260 pixels. Packed
bilingual label tables measured 19,541 bytes for 500 cities, 79,303 for 2,000,
and 279,975 for 7,000, excluding code, fonts and policy/spatial indexes. Its
42-entry naming watchlist was not exhaustive; a source-name fallback or a
suppression list did not establish a neutral naming rule. Sources included
[Natural Earth populated places](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-populated-places/),
its [public-domain terms](https://www.naturalearthdata.com/about/terms-of-use/),
the [UN permanent-status issues](https://www.un.org/unispal/permanent-status-issues/)
and [official naming guidance](https://mfa.gov.ua/en/correctua).
Desktop inspection did not establish physical-panel readability.

## Consequences

The lossless asset exceeds the original 150 KB target but preserves the selected
grid. Rendering remains physical geography only. Compression adds decoder and
generator complexity. Regeneration must verify the pinned input and round-trip
every row; rendered samples alone do not prove losslessness. Performance and
scratch-memory changes require separate measurements.

## Reopen when

A smaller encoding preserves every row of the selected grid with acceptable
decoder/RAM cost, or explicit product requirements change the grid or flash
budget. Borders or labels require a new owner-approved scope and naming policy;
available flash alone is insufficient reason to add them.
