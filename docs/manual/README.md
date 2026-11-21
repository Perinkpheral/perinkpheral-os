# User manual

The English original is `source/manual.html`. Russian lives beside it in
`source/manual.ru.txt`, using the application guide's translation pairs.
The application guide and user manual use `docs/guide/source/print.css` and
`cover.html`, extracted unchanged from the guide. `Guide.kt` expands the stylesheet and cover before
translation and uses the same numbered headings, linked contents and two-pass
Chromium printing. The manual's own CSS only arranges its pictures and page breaks.

From the repository root:

```sh
tools/perinkpheral manual
tools/perinkpheral manual --language ru
tools/perinkpheral manual --out /tmp/perinkpheral-manual.pdf
tools/perinkpheral manual-units list
tools/perinkpheral manual-units missing ru
```

PDFs and generated Russian HTML are ignored. Reviewed pictures live in
`source/pictures/`, like the guide's screens. CI prints both languages and
includes the PDFs in the documentation artifact; the release job attaches
all documentation PDFs. Printing refuses missing pictures.

## Rebuilding pictures

Requirements: the normal tools environment
(Java 21+, C compiler, wasi-sdk), Python 3, Node.js, Blender, Playwright with
Chromium, and a Python environment with `tools/case/requirements.txt` installed.
The Python used for CAD is separate from Blender's bundled Python.

```sh
export CAD_PYTHON=/path/to/cad-environment/bin/python
export MANUAL_APPS=/path/to/perinkpheral-apps
export PLAYWRIGHT_MODULE=/path/to/node_modules/playwright
# Optional: BLENDER, CHROMIUM_EXECUTABLE, CC.
docs/manual/pictures.sh /tmp/manual-review-round
```

Use a new review folder for each round. Intermediates, logs, native previews,
BMP captures and rendered PNG/SVG files all stay under that folder. Final
pictures are also copied beside the manual source. Review them before keeping
them. Before copying, the script checks every PNG against the accepted source
dimensions and writes `picture-dimensions.json`; a size change stops the build.
Renders and browser captures depend on tool and font versions, so reruns may
change committed pictures; commit only changes that have been visually reviewed.
The normal PDF build and CI consume these pictures; they do not require
Blender or silently regenerate reviewed artwork.

- `export_model.py` uses the enclosure exporter's tessellator and the named
  STEP solids, in their assembly positions.
- `render_device.py` renders that assembly in Blender and projects the real
  button centres. `hardware_pictures.cjs` adds callout lines and button outlines:
  dashed for a press, solid for a hold. There are no fingers. Numbers 1-4 and
  the outline convention are explained in both languages in the manual.
  Raster dimensions and quality are preserved; SVGs embed the original PNG bytes.
  The base and USB-port PNG renders stay in the review folder as SVG inputs.
- `screen_pictures.py` runs the real Settings and Weather applications through
  `tools/perinkpheral sim`, captures `/api/dev/screen`, and uses
  `tools/perinkpheral press` for navigation. Weather answers are fictional,
  generated with seed 417. Other requests are refused by a catch-all mock.
  The response status is checked before taking each weather picture, so a loading
  screen cannot stand in for the unconfigured state. Each capture uses a separate,
  fresh state folder. Native system previews still come from `tools/perinkpheral preview`.
- The simulator does not supply `sys_info`, which the setup screen requires.
  `capture_setup.c` therefore runs the actual setup application's drawing code
  with the native graphics library, the real translated strings and three
  fictional system information answers. It does not duplicate the screen layout.
- `capture_recovery.c` calls recovery's actual screen renderer with the release
  version in `fixtures/release.json`, which also supplies the browser's update.
  This leaves the guide's existing preview fixtures unchanged.
- `web_pictures.cjs` assembles this checkout's actual device page. Its local API
  uses public application metadata and fictional settings, addresses, networks
  and releases. Browser requests to outside services are intercepted; unexpected
  requests fail the capture. The list illustration ends after its row controls;
  unrelated settings below it are outside the capture. Recovery's upload form is captured without surrounding
  page whitespace. Its page is decoded directly from
  `recovery/main/page.h`. That page is currently English in both languages, so
  the English and Russian manual editions use the same English capture.
  Playwright selects a dummy `perinkpheral.bin` with `setInputFiles` before capture. The machine-localized native
  chooser button is clipped out for this illustration of the step after file selection.
  The selected filename and real Install firmware button stay visible. The original
  input height is retained and the form dimensions must match before and after the
  crop. The caption in both languages describes the file as already selected.
  Nothing is uploaded. The capture does not change the OS
  language or pass browser language arguments. The catalog uses Pomodoro for Install because
  it is not preinstalled; Stocks and Weather demonstrate Update.
  Dialog captures use an opaque paper-coloured backdrop so underlying page text
  cannot protrude at a fractional border coordinate. Dialog sizes stay unchanged.
- `review_pictures.cjs` makes PNG copies of the SVG overlays for review. These
  copies are not PDF inputs.

After rebuilding the pictures, print both manual editions with the commands
above, then rasterize their pages and the SVG overlays for review:

```sh
pdftoppm -scale-to 1400 -png docs/manual/perinkpheral-manual.pdf /tmp/manual-review-round/manual-en
pdftoppm -scale-to 1400 -png docs/manual/perinkpheral-manual.ru.pdf /tmp/manual-review-round/manual-ru
node docs/manual/review_pictures.cjs /tmp/manual-review-round
```

Inspect every page in both languages as well as the individual pictures.
Automatic checks cannot establish whether an outline, port leader or page
break is clear.
