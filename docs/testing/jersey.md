# Jersey replacement study

Status: closed on 2026-11-21. Tiny5 was tried in real screens and rejected by the owner;
Jersey stays. The study below is kept as the record of what was compared and why.
Cyrillic support is mandatory; candidates without it are excluded.

The owner observed that the Air app's cramped labels and bottom spacing depend
on the selected font. Fix the app's layout for both current fonts; investigate
a replacement for Jersey separately. Do not change the system font as part of
the Air layout correction.

Compare Jersey and possible replacements with Departure Mono as a reference:

- Render the same English and Russian screens, including Air's icons, labels,
  values and classifications. Check capitals, baselines, line spacing and the
  actual visible bounds, not just nominal font sizes.
- Check legibility on the physical 400×300 panel at every supported text size.
  Include digits, punctuation, units and long Cyrillic labels.
- Separate layout mistakes from font-metric or rasterization problems. Record
  whether correcting the existing metrics is sufficient before replacing it.
- Check Cyrillic coverage, redistribution license, generated asset size and
  compatibility with existing font selections and SDK size identifiers.

Produce side-by-side previews and a short recommendation. A font replacement
needs its own implementation and device review; this note does not select one.

## Initial comparison, 2026-11-18

Tiny5 Regular is the strongest candidate for a separate implementation trial.
Its downloaded font contains all 66 Russian letters, including Yo, and all 115
codepoints in the current Cyrillic pack. It is proportional and uses a native
pixel grid. It looks more angular than Jersey, especially at large sizes.

Handjet also covers the complete pack, but its small monochrome letters are too
thin in this comparison. Tiny5 Duo is too wide for the current small labels.
PixelCode Bold covers Russian but lacks 19 extended Cyrillic/number-sign characters
from the existing pack and is monospaced. Neither is recommended as a replacement.

The checked Tiny5, Handjet and PixelCode repositories provide SIL OFL 1.1 licenses.
Retain their copyright and license notices if distributing font assets. Check the
chosen exact file and any naming restrictions again before integration.

Sources: [Tiny5](https://github.com/Gissio/font_Tiny5),
[Tiny5 license](https://github.com/Gissio/font_Tiny5/blob/main/OFL.txt),
[Handjet](https://github.com/rosettatype/handjet),
[PixelCode](https://github.com/qwerasd205/PixelCode).

The comparison used the project's monochrome rasterizer with matched capital
heights. These are controlled font specimens, not application or panel screenshots:
[Air layout](jersey/air-comparison.png), [sizes](jersey/sizes-comparison.png).
Tiny5's generated ASCII plus Cyrillic trial has 210 glyphs and 1,037 bitmap bytes;
that is not a final firmware-size measurement. No production font was changed.

Next: trial Tiny5 in real application previews, check all six SDK sizes and the
physical panel, then decide whether to replace Jersey. Preserve existing font
settings and the SDK's size identifiers if proceeding.

## Tiny5 in real screens, 2026-11-21: rejected

Tiny5 Regular 3.002 (Gissio/font_Tiny5, commit 04d19ef, SIL OFL 1.1) was drawn in place of
Jersey in the simulator and the preview, with its own Cyrillic through a trial glyph pack:
Perinkpheral OS's screens and eleven catalog applications, English and Russian, beside
Departure Mono and Jersey - 49 sheets. Its capitals are 5 dots tall, so every SDK size has an
exact multiple of Jersey's capital height (10, 15, 25, 30, 50, 80 px) and no layout broke: no
new cut, wrap or text on a rule; Sun's cells alone fell back a size.

The owner rejected it for what is read:

- Digits on a 3x5 grid: 8 is a pinched block, and "82.4529" or a chart's "85.6542" need a
  second look where Jersey's are read at once.
- The two largest sizes: a dot is 10 and 16 px, and a clock's "15:30" is a scoreboard of blocks.
- Small Russian lowercase runs together; Latin "a" reads as "o".
- It does not relieve Air's labels, which began the study: they are as wide as Jersey's.

What it would have saved, estimated from generated data and not from a firmware build:
about 14 KB of the image (one bitmap for four faces) and 8 of the glyph partition's 16
sectors, with Greek and the largest size's Cyrillic missing from Jersey at the time of
the Tiny5 comparison. Flash room was not the constraint. Not tried: Tiny5 Bold and
Mono, and the physical panel.

The experiment is not in this repository: it was a throwaway substitution behind the Jersey
selection with scripts outside the Kotlin tools. No production font was changed.


## Cyrillic polish, 2026-11-21

Jersey remains the proportional family. Its Cyrillic now covers all 115 pack entries in
all four faces, including Jersey 20 for the largest SDK size. Only that face's extended
Latin remains omitted for space. ASCII headers, SDK size identifiers and font selection
are unchanged.

- Б has a square flag projecting beyond its bowl; the smallest б has a heavier flag and a clear neck.
- З/з have a smaller upper bowl, a larger lower bowl and flat entry terminals beside
  the rounded digit 3. Each face has independent capital and lowercase drawings.
- О has compact shoulders and a wider counter beside the narrow oval zero; its width
  follows Latin O with two added columns, rather than the capital height.
  Ө, Ә, Ю and Ф share that round construction.
- І has top and bottom crossbars to distinguish it from Latin l and 1; Ї shares the base.
- Ә has a clearer upper opening. Ұ/ұ have a separate bar below the fork of Ү/ү.
- Һ has a projecting left ascender, a shoulder below lowercase h's, and the width
  of a capital H. The shortened n shoulder retains the face's curve and stem weight.
- Lowercase б has a stronger flag at 15, 20 and 25; ж has fuller central joins, and и
  has a lighter diagonal. З/з use the face's stem weight in their bowl sides.
- Jersey 20 has its own lowercase masters, with four-pixel stems and corrected joins
  in в, ж, м and У. The other reviewed letters retain their established constructions,
  including Ч's vertical entry and rounded shoulder, and the tails of Қ and Ң.

Review the generated sheets and real text together. Set `WASI_SDK_PATH` to the
installed wasi-sdk directory:

```sh
JAVA_TOOL_OPTIONS=-Djava.awt.headless=true tools/perinkpheral glyph-packs --out /tmp/jersey-packs --sheet /tmp/jersey-sheets
tools/perinkpheral preview /tmp/jersey-preview --font jersey
tools/perinkpheral sim <application> --font jersey --language ru --glyphs /tmp/jersey-packs
```

The generator checks coverage, counters, selected confusable pairs and sector-rounded
partition use. Latin is 31,964 bytes (8 sectors), Cyrillic 24,796 (7), Greek 1,428 (1):
58,188 bytes of data occupying 65,536 bytes. There are no unallocated sectors when all
three packs are installed. The pack format and partition layout are unchanged.

The review included all four face sheets, Russian and Kazakh text, the weather, calendar,
status and kit applications, firmware previews, and a temporary specimen at all six SDK
sizes. Local socket restrictions required a scratch simulator harness using its real
Wasm interpreter, mock delivery, event loop and renderer with file-based controls.
`check-generated` and the host suite passed. Physical-panel acceptance remains to be
performed; no device was connected during this pass.

The owner rejected the first pass: З still resembled 3, О was too close to 0, and Б
was too close to 6. Round 1 was also rejected for З, an oversized О/Ю, uneven
lowercase weight and a low Һ shoulder. Round 2 was rejected for digit-like З/з
with conspicuous terminal hooks, and a Һ shoulder too close to the cap line.
Round 3 removes the hooks, changes the bowl proportions and lowers Һ's shoulder
to expose the left ascender. These are review candidates, not production acceptance.
Cross-script equivalents such as Ѕ/S and һ/h retain their shared forms; the proofs
include them explicitly.
