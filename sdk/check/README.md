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

Mock responses may include `"headers": {"ETag": "v1", "Set-Cookie": ["a=1", "b=2"]}`.
String values are single fields; arrays preserve repeated fields in order. Names are
case-insensitive HTTP tokens. `answer_header()` uses the firmware's storage and lookup:
1024 bytes including names and separators, no partial values, and every lookup fails
with `HOST_ERR_NO_SPACE` after aggregate overflow or an invalid received field name.
`answer_info()` uses the firmware's fixed metadata parser independently of that storage;
Content-Length comes from the mock header, or is -1 when absent. Headers are scoped to one
answer, available in every data chunk and in picked delivery. Direct native tests can call
`standin_answer_headers(fields, count)` before the next delivery; without it there are no headers.

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
- Mock headers describe the final response: no automatic headers, transfer decoding or
  Content-Length/body consistency check is performed.
- The network has no redirects, TLS or timeouts; the request queue holds only the checked
  application's requests; a picked answer is broken off only before its last closing bracket;
  polls come when the checks give them, not at their pace.
- The system API (`sys_*`) is not stood in for.
