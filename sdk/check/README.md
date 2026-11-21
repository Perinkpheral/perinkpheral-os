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
