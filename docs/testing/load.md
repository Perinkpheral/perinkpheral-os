# Load testing

Run load testing after [endurance testing](stability.md) has passed on the same image.
The scenarios measure resource limits, responsiveness and recovery under sustained load.

Nothing here is automated yet: `tools/cli` has no workload runner. The feature freeze
covers the firmware, not test tools; a runner may be added. The short stress run of
2026-11-27 is recorded in [stress-2026-11-27.md](stress-2026-11-27.md).

## Builds

Presses can be injected only into a development build (`/api/dev`, see stability.md).
A development build differs from the release in size and timing, so:

- scenarios without injected presses (L1, L2 and L4-L10) run on the **release** image of
  the candidate;
- L3 runs on the development build of the same commit, and its result is about
  navigation and memory, not about the release image's timing.

Run on a bench device. The owner's shelf device is used only with the owner's agreement.

## What is measured

Before, during and after every scenario, once a minute, from `/api/status`:
`free_memory`, `least_free_memory`, `largest_free_block`, `memory_refusals`,
`failed_allocations`, `app_area_available`, `app_area_used`, `reset_reason`,
`last_panic`, `uptime_s`, storage use.
For release-image scenarios, `tools/perinkpheral soak` collects these fields and the
log. Give each scenario its own directory and record its name in `phase.txt`. For L3's
development build, collect `/api/status` directly once a minute and capture the log
separately; `soak` refuses development builds.

Return to the same quiet state - the menu, no request in flight - for five minutes
before and after, and compare the two.

## Scenarios

| # | Load | How | Duration |
|---|---|---|---|
| L1 | Every network application installed at once, each at its shortest poll interval | Install the whole catalog; where an application has a refresh setting, set its shortest | 6 h |
| L2 | The device page under requests | Four parallel loops on `/api/status`, `/api/apps`, `/api/log` and `/` from the bench computer, while L1's applications poll | 1 h |
| L3 | Navigation without rest | Loop through the menu and applications with `tools/perinkpheral press`, waiting for each command to finish and then pausing two seconds; use long BACK every tenth press. Record the achieved press intervals | 2 h |
| L4 | Storage at its limits | Install test packages until the installer refuses another with "Not enough storage."; record application count, code space and filesystem use. Remove and reinstall a package twenty times. Separately exercise an application that fills its KV and writable-file limits | 1 h |
| L5 | A bad network | L1 with the access point switched off for 1, 10 and 30 minutes, and with DNS answering nothing | 3 h |
| L6 | Everything | L1, L2 and L5's outages together, including L7's two-root TLS/queue workload and L8's Latin uploads during the online periods | 2 h |
| L7 | TLS, both application CA roots and a full request queue | Install a test application with two packaged CA roots and endpoints signed by each root. Keep requests pending up to both the 16-request application limit and the device's 16 KB queue limit, replenishing on completion; alternate the two TLS endpoints. Run L2's browser loops and catalog reads/installs throughout | 2 h |
| L8 | Largest glyph upload during TLS | With L7 running, use the page to install the catalog's Latin pack (31,964 bytes at review; record the actual candidate size), then remove it; repeat twenty times, starting uploads during handshakes and streamed responses. Capture upload HTTP responses and retry times; also test one quiet-state installation | 20 cycles |
| L9 | Application-area reservation across boots | Alternate twenty cold boots (remove all power, including battery, on a suitable fixture) and twenty warm restarts. On every boot record status, open an installed application using the physical buttons, and record status again while it remains loaded. Keep the candidate image and saved settings unchanged | 40 boots |
| L10 | Recovery handoff under load | During L7/L8 traffic, enter recovery from the page, both planned update and manual entry, five times each. In recovery verify status and the recovery access point, upload the same candidate, confirm physically, and wait for main's probation to finish before resuming the load | 10 cycles |

L2 and L5 exercise what a home network does by accident; they are not attacks, and
[SECURITY.md](../../SECURITY.md) promises no defence against a hostile home network.

L7 must use both roots in the same application's package, with a successful request
through each; record the accepted queue count/bytes and the immediate refusal at its
limit. A smaller workload does not establish the full-queue case. Record TLS and
certificate logs with the request results and memory counters.

L8 passes when uploads either complete or return the explicit 503 memory/busy refusal
without an allocation failure, HTTP 500, disconnection or restart. The page retries
refusals after 2, 4 and 8 seconds, then says "Device busy. Try again shortly." and
re-enables Install; a quiet retry must install the intact pack. Verify its glyphs on
the panel, and verify an already installed pack remains usable after a refusal.

L9 passes only when every boot reports `app_area_available: true`, and
`app_area_used` becomes nonzero with the application loaded, with no cannot-start
screen or reservation error. Removing USB with a connected battery is not a cold
boot. Without a power-disconnect fixture, record cold boots as untested.

L10 passes when handoff either explicitly refuses while busy with main still usable,
or reaches recovery within a minute. Retry refused handoffs once quiet; complete all
ten successful cycles. Recovery must stay reachable, accept the intact candidate,
and return to that image in main with probation complete within three minutes,
installed packages/settings retained, `app_area_available: true` and nonzero
`app_area_used` after opening an application. Capture handoff/refusal, recovery,
upload and probation results with logs; sample main-only memory fields in main.

## Failure

The failures of stability.md, and:

- the page stops answering for more than a minute while the device still runs;
- failure reporting that violates the request contract: immediate `http_send` refusals
  return `HOST_ERR_*`; accepted requests report network failures through `HOST_FAILED_*`;
  polls that cannot start may leave background applications unloaded; abandoned streams
  need not receive a final callback;
- quiet-state `free_memory` or `largest_free_block` lower after a scenario than before
  it by more than the measurement's own spread, taken from two quiet readings;
- an application that shows old data as current after the network returns;
- an installation left half done after L4;
- a TLS/certificate failure at L7's valid endpoints, a queued request lost without
  the contract's failure reporting, or a catalog/install action that cannot recover
  once the load stops;
- any L8-L10 pass criterion missed, any failed allocation, or an unexpected restart
  during these scenarios. Expected memory/busy refusals are recorded, not failures
  when they follow the contract and recover after the load stops.

Slower is not a failure: one request at a time is the design. Record the longest wait
between a press and its frame in L3, and the longest delay of a poll in L1 and L6.

## Record

A dated file beside this one: the image ID, the application set, each scenario's
readings before and after, the worst values during it, every reset or refusal with
its log lines, and what was changed on the device by hand.

No run of this plan is recorded yet.
