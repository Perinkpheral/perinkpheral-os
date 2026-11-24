# Catalog capability results, 2026-11-24

Catalog commit: `df1840fe0b2f4ef8a60864a2e7f3a3b38b41085d`. The requested checkout was tested read-only, but other agents merged fixes while the run was in progress. To make these final results coherent, the complete run below uses a temporary `git archive` of this commit. Uncommitted calendar/sun edits in the catalog checkout are excluded. No catalog file, Git index or branch was changed by this work. SDK branch: `feat/app-check-failure-paths`.

The live checkout was checked with `WASI_SDK_PATH=$HOME/opt/wasi-sdk`, `PERINKPHERAL=/tmp/wt-checks` and `/tmp/wt-checks/tools/perinkpheral app-check /Users/grakovne/projects/perinkpheral-project/perinkpheral-apps/apps`. Reproduce the pinned full run with the same environment and a fresh archive:

```sh
catalog_snapshot=$(mktemp -d)
git -C /Users/grakovne/projects/perinkpheral-project/perinkpheral-apps archive df1840fe0b2f4ef8a60864a2e7f3a3b38b41085d | tar -x -C "$catalog_snapshot"
"$PERINKPHERAL/tools/perinkpheral" app-check "$catalog_snapshot/apps"
```

The complete run checked 19 applications and 179 applicable capability/logic groups. 31 groups failed across 18 applications: 29 capability groups and two application-owned logic tests. This complete run includes immediate response-save/unload comparisons, single-member JSON mutations, raw-protocol numeric mutations and expanded clock navigation paths. Calendar and Sun fail their own `reliability.c` because queue refusal followed by failure of both receipt stores loses notices after unload, in all four receipt/update combinations. Clock survival/reload checks passed for all 19 applications. The specimen and SDK self-tests pass independently of these catalog failures.

## Failures by application

| Application | Failing checks and reason |
| --- | --- |
| air | `store`: refused response saves silently revert immediately after unload (both NO_SPACE and FAILED); `network`: generated `numeric-overflow`, `picked-null`, `wrong-types` changes the good/failed-answer picture or saved cache; `network`: requests during Retry-After (2 requests with 120 s deadlines, 2 requests with 300 s deadlines) |
| calendar | `store`: refused response saves silently revert immediately after unload (both NO_SPACE and FAILED); `logic reliability.c`: queue refusal plus failed receipt rollback never recovers for KV/journal receipts, both with and without update; `recovered` assertion fails |
| clock | All applicable checks pass |
| donut | `sessions`: allocation refused; resume state lost without reporting it (e.g. `U`) |
| earthquakes | `sessions`: allocation refused; resume state lost without reporting it (e.g. `D`); `store`: refused response saves silently revert immediately after unload (both NO_SPACE and FAILED); `store`: refused button saves silently revert after unload (e.g. `Do, Uo, do, uo`; both NO_SPACE and FAILED); `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error; `network`: generated `empty-lists`, `missing-record`, `picked-null` changes the good/failed-answer picture or saved cache |
| ha | `network`: requests during Retry-After (2 requests with 120 s deadlines, 2 requests with 300 s deadlines) |
| iss | `network`: requests during Retry-After (2 requests with 120 s deadlines, 2 requests with 300 s deadlines) |
| keenetic | `sessions`: allocation refused; resume state lost without reporting it (e.g. `D`); `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error; `network`: requests during Retry-After (2 requests with 120 s deadlines, 2 requests with 300 s deadlines) |
| kit | `sessions`: allocation refused; resume state lost without reporting it (e.g. `D`) |
| life | `sessions`: allocation refused; resume state lost without reporting it (e.g. `D`) |
| metals | `store`: unchanged answers with elapsed time and Cache-Control: max-age=60: 3 flash writes in three minutes; `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error |
| pomodoro | `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error |
| rates | `sessions`: allocation refused; resume state lost without reporting it (e.g. `D`); `store`: refused response saves silently revert immediately after unload (both NO_SPACE and FAILED); `network`: generated `numeric-overflow`, `picked-null`, `wrong-types` changes the good/failed-answer picture or saved cache |
| space-launch | `sessions`: allocation refused; resume state lost without reporting it (e.g. `D`); `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error; `network`: generated `numeric-overflow`, `picked-null`, `wrong-types` changes the good/failed-answer picture or saved cache |
| stocks | `store`: refused response saves silently revert immediately after unload (both NO_SPACE and FAILED); `network`: generated `numeric-overflow`, `picked-null`, `wrong-types` changes the good/failed-answer picture or saved cache |
| stopwatch | `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error |
| sun | `logic reliability.c`: queue refusal plus failed receipt rollback never recovers for KV/journal receipts, both with and without update; `recovered` assertion fails |
| timer | `timers`: alarm allocation refused; healthy schedule absent after 60 seconds of timer retries, with no visible error |
| weather | `store`: refused response saves silently revert immediately after unload (both NO_SPACE and FAILED) |

## Interpretation and coverage

Picture comparisons use draw records. The malformed-response oracle is deliberately strict: a legitimate optional-field fallback or empty feed can produce a different correct picture. Such cases require a documented `responses.*` exception and application-specific tests; a mismatch alone does not prove the provider data is semantically invalid. No exceptions were added to the read-only catalog.

No catalog application declares `clock_probes`, so the green clock groups establish survival/reload equality, not correct event-time content across DST. The specimen has UTC and floating-local cross-season probes; broken copies that substitute the current offset for either conversion are rejected. Calendar, HA and Metals use non-JSON protocols; raw numeric substitutions assert sanitizer safety, not a universal semantic format validator.

Calendar, Earthquakes and Space Launch did not reach a due notice under their supplied inputs. The notice harness reports that coverage gap. Stocks has incomplete mock coverage for some tickers and generated longest-list entries. These notes are not evidence that those behaviors are correct.

Keys and limits are documented in `sdk/check/README.md`; the complete class inventory and reasons for semantic limits are in `docs/testing/app-check-2026-11-24.md`.

## SDK verification

All checks below passed in the firmware worktree:

- `make -C firmware/test/host test`, with wasi-sdk enabled, including the frozen ABI,
  real interpreter, simulator jobs and layer checks.
- `tools/perinkpheral sim-test firmware/test/sim`: 8 scenarios, 34 pictures.
- `tools/cli/gradlew -p tools/core check`: the specimen passes, all 50 broken
  copies are caught, and all 9 unusual but valid variants pass. This includes UTC
  and floating-date offset defects, receipt/queue failures, immediate save reversions,
  malformed JSON and raw numeric overflows, visible error declarations and policy exceptions.
- `tools/perinkpheral check-generated`: committed assets current.
- `tools/perinkpheral check-api`: 35 routes agree with the API description.
- `tools/perinkpheral contract check`: 690 ABI entries and 11 fixture hashes agree;
  frozen inputs were not regenerated.
- `tools/cli/gradlew -p tools/cli pageTest flashManifestTest simTestMocksTest`.
- The three browser suites: firmware updates, application requests and settings.
- Both guide languages rebuilt with Chromium headless and the added pages visually
  reviewed. `guide-units missing ru` produces empty stdout.
- Additional focused specimen checks verify raw Retry-After metadata and bounded
  recording of very long draw sequences.

The catalog run intentionally exits unsuccessfully because it exposes the 31 failure
groups above. No catalog source was repaired or exception added, no remote branch was
pushed or merged, and no device was contacted. Tests that simulate publication use
local temporary Git repositories.
