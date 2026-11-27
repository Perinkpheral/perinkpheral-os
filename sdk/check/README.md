# Capability checks

`tools/perinkpheral app-check <folder>` builds every application of a folder, then runs
non-system applications against a stand-in
host (`standin.c`, the contract built for this computer with the sanitizers) and holds it to what
every application must do: keep its session across unloads, use the store sparingly,
handle refused writes by preserving state across unloading or displaying a failure,
keep its timers and alarms, handle every answer and failure of the network, and
draw a frame of its own. Which checks apply is read from the module's imports and exports and from
the manifest. `apps/specimen` passes them all; the tools' test breaks copies of it and requires
each break to be caught, and keeps unusual but right copies that must pass. Applications with
`"system": true` skip both capability checks and their own `test/*.c` tests.

Put application-specific inputs in `test/check.json`: `at`, `tz`, `store` and
`mocks`. Mocks may be an object or a JSON filename relative to that file. The command
also discovers and runs `test/*.c`, compiled with the stand-in and sanitizers.

For button-triggered requests, declare `"request_presses": "U"` (short `UODB`, long
`uodb`). The checker presses this sequence before foreground response rounds and
when preparing a foreground visit for background delivery. A network check with
manual polls requires at least one delivered manual response; no delivery is a
coverage failure, with a request to supply the trigger.

Named `tz` values in native checks and simulator scenarios resolve to the Settings
city table's recurring POSIX rules, including past dates, rather than IANA history.
City names, their listed IANA aliases, fixed-offset zones and explicit POSIX month
rules are accepted. Lord Howe uses `LHST-10:30LHDT-11,M10.1.0,M4.1.0` for clock probes.

Mock responses may include `"headers": {"ETag": "v1", "Set-Cookie": ["a=1", "b=2"]}`.
String values are single fields; arrays preserve repeated fields in order. Names are
case-insensitive HTTP tokens. `answer_header()` uses the firmware's storage and lookup:
1024 bytes including names and separators, no partial values, and every lookup fails
with `HOST_ERR_NO_SPACE` after aggregate overflow or an invalid received field name.
`answer_info()` uses the firmware's fixed metadata parser independently of that storage;
Content-Length comes from the mock header, or is -1 when absent or outside 0..INT32_MAX. Headers are scoped to one
answer, available in every data chunk, picked delivery and document callback. Direct native tests can call
`standin_answer_headers(fields, count)` before the next delivery; without it there are no headers.

Response and mutation files are read completely, with explicit errors for read or allocation
failures. More than 64 mocks are supported. Delivery applies the 256 KB picked and 4 MB
streamed ceilings. An oversized streamed fixture ends in `HOST_DATA_FAILED` with
`HOST_FAILED_TOO_LARGE`; its prefix never receives a successful `HOST_DATA_LAST`.
Nonempty `pick` always uses JSON delivery, including for non-JSON HTTP refusals.
Direct raw HEAD fixtures set `standin_answer_method(HOST_HTTP_HEAD)` before
`standin_data()`. The checker carries the dequeued method across background loads.
Each delivery snapshots that method before any application callback can queue another
request with the same tag. Without an explicit method a raw fixture uses GET, and a
poll uses its declared method.

The stand-in arena uses the application's declared `APP_ALLOC_ARENA` array with
its actual size and eight-byte alignment. ASan can observe that array's boundary.
Allocation position and zero-initialized arena bytes reset at each load. Without
an arena declaration, no arena allocation succeeds.

`standin_set()` injects raw fixture bytes from a terminated string. It does not
validate manifest setting types, user-only destinations, printable text or the
secret-clearing rules of a destination change. Empty text stores an empty value;
NULL deletes it. Use firmware/web tests to verify settings admission.

Screen helpers model handler exits, scene-session closure, load-time inversion and QUESTION
press generations. `standin_button_event(button, action)` delivers one event without drawing,
so tests can hold OK across a question change. `standin_draw()` marks its frame as shown.
Notification validation and copying follow the firmware. `pending_notice_count` counts the
four retained slots; `notices` counts all acceptances. `standin_notice_dismiss()` releases the
oldest slot and `standin_notices_clear()` releases all slots, without changing acceptance counts.
Header comparisons and visible-failure checks use scene-left precedence, kept-right precedence
and fullscreen suppression; hidden text cannot satisfy a visible-failure requirement.

## Known differences from the device

The stand-in is not the device. A finding that rests only on one of these differences is not a
defect of the checks:

- A frame is a hash of the drawing calls, not of pixels: ink in the paper's colour, clipping and
  outlines off the surface count as drawing.
- Text is measured as Departure Mono only; the fonts' scales per size are not pinned here (the
  simulator's pictures hold them). Ink bounds and QR module geometry are approximations;
  they do not establish actual placement or QR capacity.
- Standard presses deliver KEYDOWN/KEYUP/PRESSED or KEYDOWN/LONG_PRESSED/KEYUP
  before drawing. The checks do not render between these events. Refresh timing
  and focus-request acceptance are not modeled.
- Native pointers are checked only where explicit stand-in validation exists.
  The stand-in does not validate wasm linear-memory offsets, retained-buffer
  ownership or interpreter execution budgets. ASan/UBSan complement the real
  interpreter's pointer and budget tests; they do not replace them.
- Background checks normally use a fresh instance per answer/event. Firmware
  can retain one instance across streamed chunks and interleave settings or
  alarms into it; those sequences require interpreter/simulator tests.
- The header is taken to show the clock, so a frame is drawn again every minute.
- Time-zone rules cover the full supported calendar-conversion range. Current-clock queries
  report unknown outside the separate 2026-2100 clock range; the raw test clock
  remains available to the harness.
- Mock headers describe the final response: no automatic headers, transfer decoding or
  Content-Length/body consistency check is performed.
- The network has no redirects, TLS or timeouts; the request queue holds only the checked
  application's requests. The late-failure scenario truncates picked JSON before its last
  closing bracket; response-mutation checks also truncate JSON halfway through its text.
  Periodic polls come when the checks give them, not at their pace. Ordinary
  capability rounds advance uptime to an applicable poll delay while keeping the
  comparison wall clock fixed. Timers and alarms still fire only as the scene directs.
- QR bounds and ink geometry are approximate. Focus permissions and WebAssembly memory
  offsets are not validated. Some sequences deliver several events before drawing;
  background delivery does not reproduce interleaving between different applications.
- `standin_set()` writes raw test state without manifest-setting validation or the
  destination-change secret clearing performed by the settings API.
- The system API (`sys_*`) is not stood in for.

Interpreter tests cover memory validation; simulator frames cover rendering; device tests
cover the shared application area, panel and timing. A checker process must exit zero even
when it printed successful results for every requested capability.

`standin()->refuse_reads` injects `HOST_ERR_FAILED` into KV/file reads and quota
accounting. `failed_read_bytes` permits a file read to copy a prefix before failure.
`refuse_restore_reads` fails KV reads and file size/read calls independently of
writes and quota accounting. The failed-restore check uses it during loading, so
destructive deletion or replacement after an unreadable restore remains observable.
`refuse_writes` combines with `write_failure_mode`: `STANDIN_WRITE_REFUSE` fails
before mutation, `STANDIN_WRITE_COMMIT` commits then fails, and
`STANDIN_WRITE_PARTIAL` copies half a file write (at least one byte) then fails.
KV retains a complete old/new value. These controls are independent of
`refuse_intervals` and `refuse_requests`. `external_repeating_alarms` models slots
owned by other applications; replacing an existing repeating alarm needs no new
slot. `standin_prepare_update()` removes the old package's ownership before the
replacement loads; its load/update callbacks may establish new ownership.
Lifecycle-marker read/write refusals leave install/update notification pending.
Scene closure keeps session writes available through blur and termination, then
discards the session. Inversion changes request a normal redraw with the same
QUESTION bookkeeping as firmware, preserving pending flags.

## Failure-path checks

The SDK checks invariants, using draw records and the same clock in the compared runs.
It never reads an application's statics. Coverage is bounded; a successful run does not
prove every possible input or button sequence. A coverage note identifies missing inputs.

| Capability | Failure paths |
| --- | --- |
| `sessions` | Refuse session allocation during blur; retained resume state or a diagnostic absent from the healthy run is required. |
| `store` | Inject failed restore reads and require durable data to remain unchanged. Exercise writes that commit then fail and partial file writes, as well as failures before mutation. Refuse the last press of each standard navigation path and all 64 pairs of short/long presses, with both NO_SPACE and FAILED. Refuse response saves as well. Unload immediately and compare the shown picture, unless it displayed an error. Test 50 identical answers at a fixed clock and three more a minute apart, including Cache-Control: max-age=60. |
| `timers` | Refuse shared repeating slots independently of ordinary alarms and fail interval persistence without changing it. Refuse alarm allocation, then allow it during 60 seconds of timer callbacks. The app must recover the healthy schedule or display or log failure. A repeating alarm must still be scheduled at its original interval after allocation recovery. Package replacement must restore recurring alarms; their background intervals may differ from the foreground schedule. One-shot alarms for canceled requests need not survive replacement; otherwise a one-shot alarm may be pending or already delivered. Log diagnostics are compared before recovery against the healthy run. |
| `notices` | Reach alarms/timers through the standard paths; compare accepted counts with queue rejection followed by acceptance and with writes refused immediately after acceptance. Retry for 60 seconds, then unload/replay; also deliver alarms into fresh background instances. No due notice reached is a coverage note. |
| `network` | Reject raw-request admission with NO_SPACE/FAILED, then permit retries. Derive malformed responses from good JSON mocks, compare the good/normal failed-response pictures and reloaded cache; observe new HTTP/poll requests during Retry-After, including lifecycle changes and chains of two refusals, with both 120- and 300-second deadlines. |
| `clock` | Cross both DST directions in Berlin, New York, Lord Howe (30-minute change), Auckland, New Year and 2090. Compare the live and reloaded picture. Optional event probes assert the per-date local HH:MM in draw records. |

`headers` in a mock is an object of response headers. `answer_header()` exposes them
case-insensitively. Decimal-seconds and IMF-fixdate GMT Retry-After headers expose normalized `retry_after` in `answer_info()`, using the same parser as firmware and simulator.
The stand-in observes early raw requests instead of refusing them, so an ignored raw-request deadline fails. Manifest-poll requests coalesce and wait for the accepted-call window and server cooldown.
Host-scheduled polling and real HTTP pacing remain simulator concerns.

`clock_probes` is an array of `{ "now": <epoch>, "event": <epoch>, "presses": "O" }`.
`now` is zero or in `HOST_CLOCK_MIN..HOST_CLOCK_MAX`; `event` is in
`HOST_TIME_MIN..HOST_TIME_MAX`, including negative seconds and zero.
Exactly one of `event` or `local` is required. For floating calendar times, use
`{ "now": <epoch>, "local": "2027-03-29T12:00:00", "presses": "O" }`: its date
is converted with `time_from_local()` independently in each zone before checking the
rendered HH:MM. The fixtures must make the named event visible after those presses (`UODB` short,
`uodb` long). Every probe runs in each of the four zones. Its expected HH:MM comes
from that zone's rules at **event**, not at **now**. This declaration identifies data,
not an application: no provider, app name or parser format is built into the checker.

`failure_text` lists nonempty strings identifying a custom visible error in draw/header
records. The standard `Storage error.`, `Not enough space.`, `Could not save.` and
`Schedule error.` and a drawn `ui_draw_reason()` are recognized without declarations.
A log line is not a visible error.

In the Retry-After probe, the stand-in defers settings-triggered and sibling
manifest-poll deliveries. Calling `poll_run()` during a cooldown is valid;
only raw-request cooldowns are the application's responsibility. Manual polls
are delivered only after `poll_run()` or a reconnect retry of requested work. The firmware host poller tests cover deadline and
list rescheduling, including reconnect and delays longer than a day.

`exceptions` maps check keys to nonempty reasons. The report lists every declaration for the
capabilities being checked, independently of coverage notes:

- `store.identical-answers`: durable request leases or equivalent metadata must be written even when successful response data is identical, including as time elapses. Only those write-count assertions are exempted; failed-answer and lifecycle write counts, refused-save recovery and sanitizer checks still run. A nonempty reason is required.
- `store.response`: a deliberately volatile answer cache.
- `store.button`: changes in this view deliberately do not survive an unload.
- `timers.refusal`: this schedule has another recovery policy.
- `notices.retry`, `notices.receipt`: a different declared delivery policy.
- `network.backoff`: independently paced services need a different request-scope policy.
  The default scenario observes the application as one budget; keep per-service tests beside it.
- `network.recovery`: backoff legitimately postpones the old immediate-recovery
  picture assertions; sanitizer, drawing and other network checks still run.
- `responses.truncated`, `responses.wrong-types`, `responses.empty-lists`,
  `responses.nulls`, `responses.missing-record`, `responses.long-values`,
  `responses.numeric-overflow`, `responses.picked-null`, `responses.numeric-tokens`: the shape mutation is valid
  for this application. An empty feed may legitimately clear the view, for example.

Unknown keys and blank reasons fail. Do not use an exception to bless a defect.
A mutation can reach only a request ready and permitted to receive the HTTP 200 mock
that generated it. Mock selection follows the regular first-match order, and HEAD
responses have no body to mutate.

JSON mutations include individual null, string and number members (at most 64 variants
of each kind, generated lazily using the first record in nested arrays), as well as changing all leaves.
String mutations retain up to 24 UTF-8 bytes of the original prefix, so an invalid identity
does not prevent reaching a long displayed name. Numeric mutations also preserve the other
fields. Optional nulls and empty feeds can legitimately change fallback text. Declare exact
alternate JSON bodies in `valid_responses`, keyed by the matching mock address:

```json
{"valid_responses": {"https://example.test/feed": ["{\"value\":21,\"description\":null}", "{\"value\":21,\"rows\":[]}"]}}
```

For JSON mutations other than long strings, undeclared variants must produce the
healthy or failed-response picture. Declared valid variants may produce another
picture, which must survive unloading. Long-string mutations use the separate
truncation-marker check described below. Addresses must match supplied mocks;
variants must be JSON objects or arrays encoded as strings. This avoids exempting an entire mutation kind for one
optional field. For non-JSON protocols,
up to 64 decimal tokens are replaced independently with an
oversized integer; only sanitizer safety is asserted because another number can be valid
data. Semantic validation still needs application tests: truncating arbitrary text can
itself be a valid answer.
The long-string check observes picked responses only and flags an unmarked marker from
a known source-truncated string; it does not infer every possible wrapping or
truncation convention.

The notice comparison does not prove power-cut atomicity. A fresh instance and refused
receipt writes model unloads, not a crash between a reservation and posting. Event
identity, escalation rules, pagination completeness and truthful statistics require
application-supplied fixtures and logic tests. Layout, languages, fonts and refresh
quality still need simulator pictures and panel verification.
