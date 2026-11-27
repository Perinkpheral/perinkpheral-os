# Test plan for an unattended agent: perinkpheral-os

This plan is for a coding agent that works alone for one or two days. Each task is small
and stands alone. Do the tasks in order. Finish one task, write its report, then start the
next. Do not plan several tasks at once.

## Rules

- Do not change product code: `firmware/main`, `firmware/components`, `firmware/system`,
  `recovery`, `sdk`, `tools/core/src/main`, `tools/cli/src/main`. You may add new files
  under `firmware/test/fuzz/` and `tools/core/src/test/` only. Test-task registration
  in `tools/core/build.gradle.kts` may be added for the simulator runner.
- Do not commit, push, tag or open pull requests.
- Do not contact any device, router or service on the network. Everything runs on this
  computer.
- Do not fix a bug you find. Report it. A person checks each report and fixes it.
- Keep each command under one hour of wall time unless the task says otherwise.
- After each task, append its report to `REPORT.md` in the report folder you were given.
  If no folder was given, use `firmware/test/fuzz/REPORT.md`.

## Report format, for every finding

```
### <task number>.<finding number> <one-line title>
File: <path>:<line>
Input: <the smallest input that shows it, as hex or as a file under firmware/test/fuzz/crashes/>
What happens: <crash / sanitizer message / wrong result / hang>
Expected: <what the code or CLAUDE.md promises>
How to reproduce: <one command>
```

If a task finds nothing, write `### <task number> nothing found` and the inputs and run
time you used. A report without a reproduction command is not useful.

## Install the toolchain first

There is no device. Everything runs on this computer, in the simulator. Install these
before any task, and check each with the command after it:

- Git: `git --version`.
- Java 21 or later (Temurin is fine): `java -version`.
- A C compiler. For fuzzing you need clang with libFuzzer: on Linux the distribution's
  `clang` and `llvm`; on macOS Homebrew's `llvm` (Apple's clang has no libFuzzer), used as
  `$(brew --prefix llvm)/bin/clang`. Check: `clang --version`.
- wasi-sdk 34 from https://github.com/WebAssembly/wasi-sdk/releases (the archive for this
  computer's system and processor). Unpack it and `export WASI_SDK_PATH=<its folder>`.
  Check: `$WASI_SDK_PATH/bin/clang --version`.
- Node.js 22 and npm: `node --version`.
- ffmpeg: `ffmpeg -version`.
- Optional, for checks that match the Linux CI: Docker with the `gcc:14` image.

ESP-IDF is not needed: nothing here builds or flashes device firmware.

Get the repositories side by side in one folder, from https://github.com/Perinkpheral:
`perinkpheral-os`, `perinkpheral-apps`, `perinkpheral-hacs`, `perinkpheral-dev-portal`,
`perinkpheral-clion-plugin`. The tools find each other as siblings.

Then build the tools and the simulator once and run an application in it:

```sh
cd perinkpheral-os
tools/perinkpheral                        # builds the Kotlin tools on first use, prints the commands
make -C firmware/test/host test           # host tests; needs WASI_SDK_PATH
tools/perinkpheral sim-test firmware/test/sim
simulator_mocks=$(mktemp)
printf '{}\n' > "$simulator_mocks"
tools/perinkpheral sim sdk/apps/hello --mocks "$simulator_mocks" # stop with Ctrl-C
```

All four must work before Task 1. If one fails, write the exact error into the report and
stop: do not change the repository to make it pass.

## Setup

Read `CLAUDE.md` sections "Perinkpheral OS API", "Execution model" and "Text is bytes".
Build the existing tests once to learn the flags:

```sh
export WASI_SDK_PATH=<wasi-sdk directory>
make -C firmware/test/host test
```

Use clang with `-fsanitize=fuzzer,address,undefined -g -O1`. Copy the include paths and
stub folders from the matching rule in `firmware/test/host/Makefile`.

## Task 1. Fuzz the JSON picker

Target: `sdk/lib/json_pick.c` (`json_pick.h`). It reads untrusted HTTP bodies in pieces.

1. Write `firmware/test/fuzz/fuzz_json_pick.c`. Split the input in two at its first byte:
   the paths (lines, at most 8) and the body. Feed the body in random-size pieces (1 to
   1024 bytes) to the picker. Use paths like `a.b`, `a[0]`, `a[*]`, `a.b[3].c`.
2. Seed corpus: the JSON bodies in `firmware/test/sim/*/scenario.json` mocks.
3. Run 4 hours. Done when it has run 4 hours or found a crash.
4. Also check: the same body fed in one piece and in 1-byte pieces gives the same picked
   values. Report any difference.

## Task 2. Fuzz the manifest parser

Target: `firmware/components/appstore/app_manifest.c` with `json_tree.c`.

1. Write `fuzz_app_manifest.c` that calls the manifest parser on the input.
2. Seeds: every `manifest.json` under `sdk/abi/3/` and `sdk/apps/`.
3. Run 4 hours. Check that a manifest accepted once is accepted again with the same
   result (parse twice, compare).

## Task 3. Fuzz the catalog stream

Target: `firmware/components/appstore/catalog_stream.c` (automatic updates parse catalog
indexes with it).

1. Feed the input in random pieces. Seed with a real `catalog.json` from the catalog
   repository if a copy is next to this one, else with a small hand-written index.
2. Run 4 hours.

## Task 4. Fuzz templates

Target: `firmware/components/appstore/text_template.c` (`{{key}}` in URLs, headers,
bodies). The store lookup can be a stub that returns fixed values, including values with
line feeds, carriage returns, UTF-8 and empty values.

1. Check the limits in CLAUDE.md: body at most 1024 bytes after expansion, keys 1 to
   15 characters. Carriage returns fail header, server-prefix and body substitutions.
   Other URL substitutions percent-encode them.
2. Run 2 hours.

## Task 5. Fuzz firmware image intake

Target: `firmware/components/net/ota_stream.c` and `ota_health.c`.

1. Feed a firmware image in random pieces with random corruptions: header magic, project
   name, length, layout id. A good image from a local build is the seed; if there is no
   build, write a minimal header by hand from the code.
2. Invalid sizes and incompatible headers must be rejected before writing. Truncated
   or corrupt bodies may already have been written; they must fail completion or
   final verification and must not be selected for boot. The pure stream test uses a
   writer stub; final image verification is the recovery writer's `esp_ota_end()` call.
3. Run 2 hours.

## Task 6. Fuzz answer headers and the poller's response handling

Targets: `firmware/components/appsys/answer_headers.c`, and `firmware/main/poller_net.c`
through `test_poller_net.c`'s stubs.

1. Random header names and values, repeated fields, more than 1024 bytes, empty values.
2. Run 2 hours.

## Task 7. Monkey in the simulator

Goal: random use of every application without a person.

1. Read `docs/testing/stability.md` and `tools/simulator/README.md` for the supported
   simulator API and command options.
2. Implement the simulator test runner in Kotlin under `tools/core/src/test/`.
   Start every application with `--mocks <file>`. Supply fixtures for its expected
   requests; unmatched requests remain simulated failures. For 20 minutes, generate
   short and long presses of each button and read the log. BACK-held chords require
   separate native input tests or device testing. Advance time through
   `POST /api/sim/forward?seconds=N`, with positive N. Start separate runs with
   `--at <unix>` for different initial dates; the simulator API does not expose
   arbitrary backward clock jumps.
3. Record: simulator exit or crash, sanitizer output in the log, "trap", "stopped",
   "over budget", a frame that does not change for 5 minutes while presses arrive.
4. Run every application of `sdk/apps` and, if the catalog repository is next to this
   one, every application in `../perinkpheral-apps/apps`. 20 minutes each.

## Task 8. Network faults in the simulator

1. For each application in Task 7, use file mocks for HTTP 500, 404 and 304, empty
   bodies, truncated document contents, invalid JSON and oversized bodies (2 MiB).
   Transport interruption and delayed delivery require a separate local fixture or
   test harness; file mocks do not provide them.
2. Expected: the application shows one of the standard reasons (Loading, Not set up,
   No network, Access denied, No answer) or its last good data. It never crashes and never
   shows garbage text. Take a picture (`shot`) of each case and list the ones that look
   wrong, with the picture path.

## When all tasks are done

Write a short summary at the top of `REPORT.md`: the number of findings per task, and the
three findings you think matter most. Then stop.
