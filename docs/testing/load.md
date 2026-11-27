# Load testing

Run load testing after [endurance testing](stability.md) has passed on the same image.
The scenarios measure resource limits, responsiveness and recovery under sustained load.

`tools/perinkpheral stress-run` implements the short A-G stress ramps below.
The feature freeze covers firmware features, not test tools. The L1-L10 duration
and qualification plan remains separate; stress-run does not claim to complete it.
The prototype run is recorded in [stress-2026-11-27.md](stress-2026-11-27.md).

## Manual stress runner

```sh
tools/perinkpheral stress-run --out <empty-directory> --host <device> \
    --bench <computer-LAN-address> --axes a,b,c,d,e,f,g --serial <port> --quick
```

`--out` is required. `--host` defaults to `PERINKPHERAL_HOST`. The remaining options
are optional. When an enabled C-F workload needs the bench server, omitting
`--bench` requires exactly one eligible LAN IPv4 address. The hostile server binds
that address on an automatically selected port; the device must be able to reach it.
Java 21 is required. C-F build their bench packages with wasi-sdk, through the same
builder as `run`. B generates a Latin glyph pack locally with a separate bench name.
No packages or glyph packs are downloaded by the computer.

| Axis | Quick | Full |
|---|---|---|
| A: web server | 4 and 12 parallel GETs, 2 screenshots, 4 slow clients, 6 idle sockets; one client per oversized header, URI and body case | GETs 4/8/12/16/24/32/48/64; screenshots 1/2/4/6/8/12; slow clients 4/8/12/16/24; idle sockets 6/12/16/24/32; malformed clients 1/4/8, declared 1 GB bodies 1/4 |
| B: Perinkpheral OS memory | Two 5 s levels: catalog/install/glyph/page workers 1/1/1/2 and 2/2/2/4 | Seven 150 s levels: each heavy operation alone, 1/1/1/0, 2/2/2/2, 4/4/4/8, 8/8/8/16 |
| C: hostile polls | Eight cases: pick oversized/nesting/lying length; document oversized/endless; XML 65 attributes; raw 5 MB/endless; 1.5 s hold after observed request | All 51 cases; ordinary holds 30 s, trickle/stall 70 s, endless 90 s, 4/5 MB streams 120 s; document abandonment and long-BACK stream release timing |
| D: application faults | All 12 modes/cases, 1.5 s observation each | Same cases, 60 s each: queue refill, stalled queue, file/KV quota, 1 ms timers/redraw, recursion, bad pointer, endless handler/data handler, 62.2 KB memory (builder report: 8.0 KB stack and 54.2 KB data) |
| E: navigation | Open, UP/DOWN and mixed actions; gaps 200/0 ms, 1.5 s per level | Same actions; gaps 2000/1000/500/200/50/0 ms, 120 s per level; fixed seed |
| F: storage | Up to three approximately 42 KB modules, remaining application slots, or the first confirmed storage refusal | Modules until confirmed storage refusal or the remaining slots of the 24-entry table; reload and open after each successful upload |
| G: clock/restarts | +1 day, -1 day, 2099; two single restarts, two concurrent, one during catalog read if reachable | +1 day, -1 day, +1 year, 2099, 2026, +5 hours, current time; eight single restarts, two concurrent, one during catalog read |

Quick mode reduces loads and observation periods; it usually takes a few minutes.
Device scheduling, request timeouts, package reloads and recovery can extend it.
A, B and G work on release firmware. C-F are skipped with a diagnostic when
`dev_tools` is absent or false. They also require a captured, restorable remembered
application or `--allow-remembered-change`. A step records status before and after,
responses, elapsed time and console fault lines. Each axis stops at its first failure; remaining
selected axes continue. Readiness requires status and the page to answer within
three minutes. Refused connections and HTTP refusals during A are load observations.
Memory/busy refusals during B are observations; HTTP 500, new failed allocations,
new panic records, unexpected restarts and console faults fail the axis. Requested
restarts require software reset reason 3 and a changed `X-Log-Boot` from `/api/log`.
Status and console checks also cover the
gaps between steps and cleanup; uptime must advance with elapsed monotonic time
(with two seconds of slack). The panic comparison ignores its starts-since counter,
but that counter must rise across a requested restart.

B uses only the device's own catalog source 0. If unreachable, catalog/install work
is skipped and glyph/page work proceeds. Reinstalls use an initially absent, small,
non-system two-file catalog package; installed applications and retained stores are
excluded. The install part is skipped if no such package or application slot is
available, any retained stores exist, or the remembered application cannot be
restored and `--allow-remembered-change` is absent. Catalog, glyph and page work continues.
Application handlers may write KV and evict retained stores.
C and D are skipped when retained stores exist because configuration also writes KV.
C-F are skipped when the retained-store list has all 16 entries and may omit protected
names. Every temporary upload applies this guard. Installed applications keep
their existing polls and settings. No TLS endpoints or credentials
are created. Glyph uploads use `stressbench`, preserving installed packs; that part
is skipped if its name is present, spare glyph space is insufficient, or the 12-face
table cannot hold another pack. The current API omits face counts, so existing packs
are conservatively treated as potentially filling that table. Pack names and sizes
are recorded at the start and checked after cleanup. Full C includes a document of
exactly HOST_DOCUMENT_MAX (12,288 bytes) and the first byte over that limit.
Its connection-release checks require the bench server to observe the tested
connection closing within 120 seconds, below the streamed response's 300-second
timeout. The next ordinary poll is not part of this check. E opens the bench
application before each UP/DOWN press. Mixed actions only open and press UP/DOWN
inside that application. Sibling switching is covered by the firmware's host tests,
not by stress-run: the input queue can drop an injected DOWN while its HTTP route
returns 200. The runner sends no BACK-held chords or release retries.
F accepts the firmware's storage-refusal responses, including HTTP 500, only when
mounted-filesystem usage confirms insufficient free space for the module. Other
upload failures fail the axis.

The runner never calls factory reset or Wi-Fi routes and never injects OK. Settings
and focus activate the bench application's fault modes. It protects installed names
and retained application stores, removes every temporary package with `forget=1`,
removes its glyph pack, restores the current clock and original `time_auto` value,
and compares the installed application list, versions, exposed settings, glyph packs
and device settings after cleanup. Clock restoration runs first. Each cleanup has a
five-minute overall deadline, including requests; Ctrl-C shares the remaining cleanup
budget and the shutdown hook waits at most that budget plus 15 seconds. Every normal
request has an overall deadline covering upload and response reads; cancellation
closes its connection. Recording failures fail the run and do not stop cleanup.
A catalog install that outlives its request is allowed up to three minutes to settle
before removal; cleanup then checks that the name stays absent for five seconds.
B settles and removes its temporary installation before restoring the remembered
application and starting the next axis. The original
manual clock value is not preserved. A partial
development upload may require completing a small package and restarting before the
public removal route can remove it. Cleanup runs after failures and Ctrl-C; a device
that remains unreachable can prevent it. Unfinished cleanup is reported as failure
with the inventory retained for manual recovery. SIGKILL and power loss cannot run
process cleanup.

Outputs include `steps.jsonl`, `initial-status.json`, `initial-screen.bmp` and
`report.md`; `serial.log` requires `--serial`, and `cleanup.json` is created when
temporary applications or a glyph pack require cleanup. The initial screen is the
last frame shown by the panel, read from `/api/screen` before mutations.
Use a new empty output directory per run; keep it private because it contains device
and application settings. The exit code is 1 for an axis failure, interruption or
cleanup error, otherwise 0. Skipped coverage is listed in the report. Console capture
configures the supplied tty with `stty -f/-F <port> raw -echo clocal`, before capture
and while holding it open, on every reopen. If configuration fails, capture stops
and the report says "console not captured". An open with no bytes after ten seconds
is reported the same way. The runner never writes bytes to the port or explicitly
sets modem-control lines; opening and closing a tty can change DTR/RTS. It reopens
that path after disconnection. If enumeration changes its name, restart the
run with the new port. Without `--serial`, console-only faults cannot be detected.

Development open records the selected installed application as the remembered
application without restarting. The runner captures `last_app`, or `current_app`
when available, from initial status and accepts only an installed name. Cleanup
reopens that application after removing bench packages. Current firmware exposes
neither field, so C-F and B's install part are skipped by default. A menu frame or
an application bitmap alone does not identify the remembered name.
`--allow-remembered-change` permits these workloads when restoration is unavailable;
the report records that the remembered application may change and the owner must
open their application once to select it for subsequent boots. The initial screen
and status are recorded before mutations.
G's restarts re-arm automatic application updates when `app_auto_update` is on;
an update may change an installed
version and fail the final comparison.

These ramps do not qualify L1-L10, two-root TLS, controlled network outages, physical
buttons, power loss, panel behavior or long-term stability. No firmware is flashed.

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
