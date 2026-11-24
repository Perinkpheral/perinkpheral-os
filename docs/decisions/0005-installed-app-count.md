# 0005: Reduce installed applications to save RAM

Date: 2026-11-24

Status: rejected

## Context

The hypothesis is that the 24-installed-application ceiling represents 24 runtime
instances or a corresponding reservation that can be recovered by uninstalling
applications. The device's runtime and storage limits are separate.

## Decision

Reject reducing the installed-application allowance as a RAM-saving measure.
The 24-application limit is a flash-storage guarantee, not a RAM budget. Preserve
one loaded wasm instance and the fixed startup arena.

## Evidence

Source: the owner's decision of 2026-11-24, the specification's application
limits, [catalog.h](../../firmware/components/appstore/include/catalog.h),
[flash_budget.c](../../firmware/main/flash_budget.c) and
[m3_area.h](../../firmware/components/wasm3/m3_area.h).

- `CATALOG_INSTALLED_MAX` is 24. Each installed application is guaranteed a
  2 KiB KV store within the 56 KiB `appkv` flash partition; compile-time checks
  include NVS entry/page overhead and system stores.
- Modules occupy the 320 KiB `appcode` partition with 32-byte headers and 4 KiB
  sector rounding. Files use the 640 KiB LittleFS partition. Installation also
  checks actual code/filesystem space; 24 is a count ceiling, not a guarantee
  that any 24 packages fit.
- Only one instance occupies the 144 KiB application area: 64 KiB linear memory
  plus 80 KiB interpreter space. Background delivery swaps instances. Installed
  application count does not multiply that reservation.

These are configured capacities and implementation facts, not a measured
before/after RAM study. No per-uninstall RAM saving was measured. Application
metadata, polling and queued work can still consume system resources; this
decision does not assert that installed count has zero effect on all heap use.

## Consequences

Removing applications frees flash and may reduce their scheduled workload, but
does not recover the fixed application arena. RAM work must identify actual
allocations; the separate [RAM study](0007-ram-savings.md) was closed without
adopting changes on 2026-11-24.

## Reopen when

A repeatable comparison attributes material RAM consumption to installed-app
metadata or workload and demonstrates a useful saving without inferring it from
the flash count ceiling, or the one-instance architecture changes.
