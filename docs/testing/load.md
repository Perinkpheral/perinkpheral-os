# Load testing

Run load testing after [endurance testing](stability.md) has passed on the same image.
The scenarios measure resource limits, responsiveness and recovery under sustained load.

Nothing here is automated: `tools/cli` has no workload runner, and under the feature
freeze of 2026-11-20 none is added to the product. The scenarios use the existing
commands and a shell loop on the bench computer.

## Builds

Presses can be injected only into a development build (`/api/dev`, see stability.md).
A development build differs from the release in size and timing, so:

- scenarios without injected presses (L1, L2, L4, L5 and L6) run on the **release** image of
  the candidate;
- L3 runs on the development build of the same commit, and its result is about
  navigation and memory, not about the release image's timing.

Run on a bench device. The owner's shelf device is used only with the owner's agreement.

## What is measured

Before, during and after every scenario, once a minute, from `/api/status`:
`free_memory`, `least_free_memory`, `largest_free_block`, `memory_refusals`,
`failed_allocations`, `reset_reason`, `last_panic`, `uptime_s`, storage use.
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
| L6 | Everything | L1, L2 and L5's outages together | 2 h |

L2 and L5 exercise what a home network does by accident; they are not attacks, and
[SECURITY.md](../../SECURITY.md) promises no defence against a hostile home network.

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
- an installation left half done after L4.

Slower is not a failure: one request at a time is the design. Record the longest wait
between a press and its frame in L3, and the longest delay of a poll in L1 and L6.

## Record

A dated file beside this one: the image ID, the application set, each scenario's
readings before and after, the worst values during it, every reset or refusal with
its log lines, and what was changed on the device by hand.

No run of this plan is recorded yet.
