# Decisions and studies

This directory preserves architecture decisions, rejected hypotheses, deferred work
and research findings. It was established by the owner on 2026-11-24 to keep
hypotheses and their evidence available. Consult the index before reopening a
question. [CLAUDE.md](../../CLAUDE.md) remains the governing specification.

## Record format

Use one Markdown file per decision or study, named `NNNN-short-title.md`.
Numbers are permanent identifiers, not a chronological ordering of decisions.
Each completed record contains:

- Number and title.
- Date in `YYYY-MM-DD` form.
- Status: `accepted`, `rejected`, `deferred` or `open`.
- Context: constraints and the question being evaluated.
- Decision or finding: the selected behavior or research result.
- Evidence: measured numbers with units, build/tool versions, scope, source
  references and upstream URLs where available. Distinguish measurements,
  estimates and untested conditions. Do not infer hardware acceptance from builds
  or host tests. State explicitly when measurements do not exist.
- Consequences: resource costs, behavior changes and outstanding validation.
- Reopen when: specific new evidence or changed constraints required for review.

`accepted` records an adopted decision, `rejected` a declined proposal,
`deferred` work postponed with a stated condition, and `open` an unresolved study.
Acceptance is not a test result. Preserve prior evidence when revising a record;
date material updates and link replacement records. Update this index with changes.
A newly registered open study may start as a single line until results exist.

Source-report names below identify owner-supplied research archives, not paths
inside this checkout. Repository links and branch/commit references identify
versioned evidence. Keep repository text in English, use a technical register,
and exclude machine-specific paths and secrets. KiB means 1,024 bytes; KB means
1,000 bytes unless a quoted interface or historical budget label says otherwise.

## Index

| Number | Decision or study | Date | Status |
| --- | --- | --- | --- |
| [0001](0001-interpreter-arena.md) | Interpreter arena: 64 to 80 KiB and memory guards | 2026-11-24 | accepted |
| [0002](0002-wamr-aot.md) | Replace wasm3 with WAMR AOT | 2026-11-24 | rejected |
| [0003](0003-streaming-glyph-uploads.md) | Stream glyph-pack uploads to flash after release | 2026-11-24 | deferred |
| [0004](0004-map-land-data.md) | Lossless one-arc-minute land data; no borders or names | 2026-11-23 | accepted |
| [0005](0005-installed-app-count.md) | Reduce installed applications to save RAM | 2026-11-24 | rejected |
| [0006](0006-battery-life.md) | Version 1 has no battery-life promise; ADC may follow in version 2 | 2026-11-24 | accepted |
| [0007](0007-ram-savings.md) | RAM savings study | 2026-11-24 | rejected |
| [0008](0008-moex-tls.md) | MOEX certificate parsing and application trust | 2026-11-24 | accepted |
| [0009](0009-screen-manager.md) | A screen manager integrated with the scene stack | 2026-11-24 | deferred |
| [0010](0010-plex-controller.md) | A Plex player controller | 2026-11-24 | deferred |
