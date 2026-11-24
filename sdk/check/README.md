# Capability checks

`tools/perinkpheral app-check <folder>` builds every application of a folder, then runs
non-system applications against a stand-in
host (`standin.c`, the contract built for this computer with the sanitizers) and holds it to what
every application must do: keep its session across unloads, use the store sparingly and retry a
refused write, keep its timers and alarms, handle every answer and failure of the network, and
draw a frame of its own. Which checks apply is read from the module's imports and exports and from
the manifest. `apps/specimen` passes them all; the tools' test breaks copies of it and requires
each break to be caught, and keeps unusual but right copies that must pass. Applications with
`"system": true` skip both capability checks and their own `test/*.c` tests.

Put application-specific inputs in `test/check.json`: `at`, `tz`, `store` and
`mocks`. Mocks may be an object or a JSON filename relative to that file. The command
also discovers and runs `test/*.c`, compiled with the stand-in and sanitizers.

The stand-in arena uses eight-byte alignment, as does the device allocator.

## Known differences from the device

The stand-in is not the device. A finding that rests only on one of these differences is not a
defect of the checks:

- A frame is a hash of the drawing calls, not of pixels: ink in the paper's colour, clipping and
  outlines off the surface count as drawing.
- Text is measured as Departure Mono only; the fonts' scales per size are not pinned here (the
  simulator's pictures hold them).
- The header is taken to show the clock, so a frame is drawn again every minute.
- Time-zone rules cover two years before to three years after the checked time.
- The network has no redirects, TLS or timeouts; the request queue holds only the checked
  application's requests; a picked answer is broken off only before its last closing bracket;
  polls come when the checks give them, not at their pace.
- The system API (`sys_*`) is not stood in for.

## Failure-path checks

The SDK checks invariants, using draw records and the same clock in the compared runs.
It never reads an application's statics. Coverage is bounded; a successful run does not
prove every possible input or button sequence. A coverage note identifies missing inputs.

| Capability | Failure paths |
| --- | --- |
| `sessions` | Refuse session allocation during blur; retained resume state or a diagnostic absent from the healthy run is required. |
| `store` | Refuse the last press of each standard navigation path and all 64 pairs of short/long presses, with both NO_SPACE and FAILED. Refuse response saves as well. Unload immediately and compare the shown picture, unless it displayed an error. Test 50 identical answers at a fixed clock and three more a minute apart, including Cache-Control: max-age=60. |
| `timers` | Refuse alarm allocation, then allow it during 60 seconds of timer callbacks. The healthy schedule must recover, its one-shot action must be delivered, or the app must display or log failure. Log diagnostics are compared before recovery against the healthy run. |
| `notices` | Reach alarms/timers through the standard paths; compare accepted counts with queue rejection followed by acceptance and with writes refused immediately after acceptance. Retry for 60 seconds, then unload/replay; also deliver alarms into fresh background instances. No due notice reached is a coverage note. |
| `network` | Derive malformed responses from good JSON mocks, compare the good/normal failed-response pictures and reloaded cache; observe new HTTP/poll requests during Retry-After, including lifecycle changes and chains of two refusals, with both 120- and 300-second deadlines. |
| `clock` | Cross both DST directions in Berlin, New York, Lord Howe (30-minute change), Auckland, New Year and 2090. Compare the live and reloaded picture. Optional event probes assert the per-date local HH:MM in draw records. |

`headers` in a mock is an object of response headers. `answer_header()` exposes them
case-insensitively. Numeric Retry-After headers, including injected 429s, also expose `retry_after` in `answer_info()`.
The stand-in observes early requests instead of refusing them, so an ignored deadline fails.
Host-scheduled polling and real HTTP pacing remain simulator concerns.

`clock_probes` is an array of `{ "now": <epoch>, "event": <epoch>, "presses": "O" }`.
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

`exceptions` is an object mapping check keys to nonempty reasons. Each is printed:

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
JSON mutations include individual null, string and number members (at most 64 variants
of each kind, generated lazily using the first record in nested arrays), as well as changing all leaves.
String mutations retain up to 24 UTF-8 bytes of the original prefix, so an invalid identity
does not prevent reaching a long displayed name. Numeric mutations also preserve the other
fields. Optional nulls and empty feeds can legitimately change fallback text. Declare exact
alternate JSON bodies in `valid_responses`, keyed by the matching mock address:

```json
{"valid_responses": {"https://example.test/feed": ["{\"value\":21,\"description\":null}", "{\"value\":21,\"rows\":[]}"]}}
```

Only mutations equal to a declared JSON body may render a different picture; their
fallback picture must survive unloading. Required fields and every other mutation
remain checked. Addresses must match supplied mocks; variants must be JSON objects
or arrays encoded as strings. This avoids exempting an entire mutation kind for one
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
