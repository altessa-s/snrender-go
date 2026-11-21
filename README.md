# snrender-go

Render Supernote `.note` notebooks and `.mark` annotations to premultiplied RGBA,
straight gray/alpha, PNG, SVG and PDF, or draw a `.mark` over its source PDF.
Extract source PDF highlight text and
render Atelier `.spd` drawings to RGBA, PNG, PDF and SVG.
The C23 snrender-lib, snnote-lib and snspd-lib sources
are vendored here. There is one Go package, `snrender`, and no Go dependencies
beyond the standard library. No separate reader or renderer installation is needed.

## Requirements and installation

Go 1.24 or newer with cgo, and a C23 compiler supporting typed enums and
`constexpr`: current Xcode clang, Clang 19 or newer, or GCC 15 as used upstream.
The build uses the draft spelling `-std=c2x`, hidden C visibility and disabled
floating-point contraction, matching the upstream compilation model.

| Platform | Backend and dependencies |
|---|---|
| macOS | CoreGraphics, CoreText, CoreFoundation, ImageIO and PDFKit from the SDK |
| Linux | Cairo >= 1.18, cairo-ft, FreeType, GdkPixbuf and zlib through pkg-config; PDFium SDK and shared library |
| Windows | MSYS2 UCRT64 GCC, pkgconf, cairo, freetype, gdk-pixbuf2 and zlib packages; PDFium SDK and DLL |

Linux package names include `libcairo2-dev libfreetype-dev libgdk-pixbuf-2.0-dev
zlib1g-dev pkg-config`. On Windows use all packages from the same UCRT64 prefix;
MSVC is unsupported. Distribute the native dependencies' DLLs and GdkPixbuf loaders
with applications. Other operating systems and `CGO_ENABLED=0` produce an explicit
build error.

macOS can also use the Cairo backend, installed with Homebrew:

```sh
brew install cairo freetype gdk-pixbuf pkgconf
```

All Cairo builds enable PDFium, including macOS with `snrender_cairo`. The upstream
CMake option is `SNRENDER_ENABLE_PDFIUM`; this binding enables it unconditionally
on Cairo, as rmrender-go does, so the overlay API works on every supported backend.
There is no additional PDFium build tag. Apple uses CoreGraphics for previews and
PDFKit for text, and needs no PDFium. Overlay PDF export itself uses the vendored PDF editor, not PDFium.

The pinned, checksum-verified SDK fetcher is vendored from snrender-lib. Supply
its include/library paths through cgo and an rpath on macOS/Linux, as in rmrender-go.
For example, from this checkout on macOS arm64 (use `mac-x64`, `linux-x64` or
`linux-arm64` for the corresponding host):

```sh
python3 internal/clib/snrender/scripts/fetch-pdfium.py --platform mac-arm64 --output "$HOME/pdfium"
export CGO_CFLAGS="-I$HOME/pdfium/include"
export CGO_LDFLAGS="-L$HOME/pdfium/lib -Wl,-rpath,$HOME/pdfium/lib"
go test -tags snrender_cairo ./...
```

On Linux omit the tag. On Windows fetch `win-x64` (Python 3.12 or newer), then
use Windows-form paths for cgo and put the DLL on the MSYS2 runtime search path:

```sh
pdfium="$(cygpath -m "$HOME")/pdfium"
python internal/clib/snrender/scripts/fetch-pdfium.py --platform win-x64 --output "$pdfium"
export CGO_CFLAGS="-I$pdfium/include"
export CGO_LDFLAGS="-L$pdfium/lib"
export PATH="$(cygpath -u "$pdfium")/bin:$PATH"
go test ./...
```

The package links `-lpdfium` on Linux/macOS Cairo and `-lpdfium.dll` on Windows
to find the SDK's `pdfium.dll.lib`. These are hard build and runtime dependencies
for every Cairo consumer, even when rendering notebooks alone.
Ship `libpdfium.so`, `libpdfium.dylib` or `pdfium.dll` with its `LICENSE` and full
`licenses/` directory alongside the other native dependencies. Fetching is an
explicit setup step; ordinary Go builds do not download native libraries.

Install the binding with Go:

```sh
go get github.com/altessa-s/snrender-go@latest
```

```go
import "github.com/altessa-s/snrender-go"
```

## Rendering

```go
data, err := os.ReadFile("testdata/supernote-typescript/blank-a6x-3.15.27-two-pages.note")
if err != nil {
	panic(err)
}
notebook, err := snrender.Open(data)
if err != nil {
	panic(err)
}
defer notebook.Close()
png, err := notebook.PNG(context.Background(), 0, &snrender.Options{Scale: 0.125})
if err != nil {
	panic(err)
}
fmt.Printf("PNG signature: %x\n", png[:8])
```

`Open` parses synchronously and owns a C copy; the input slice may be reused after
it returns. Call `Close` explicitly: there are no finalizers. `Close` is idempotent
and returns immediately; active calls retain C storage until the last call finishes. A closed, nil or zero-value notebook returns `ErrClosed`.
A Notebook must not be copied. Its immutable C model supports concurrent rendering;
options and callback-owned state must be synchronized by the caller.

`PageCount` counts readable pages. `PageGeometry` returns oriented pixel dimensions,
DPI and PDF points, including per-page device overrides. Page indexes are zero-based.
`PDF(ctx, first, count, options)` exports an inclusive start and positive count;
`AllPages` means all remaining pages. `PagePDF` exports one page.

| Method | Result |
|---|---|
| `Bitmap` | `*image.RGBA`, Go-owned premultiplied RGBA, top row first |
| `Raster` | `*Raster` with `Rect` and separate `Gray` and `Alpha` planes; transparent gray is 255 |
| `PNG` | RGBA PNG |
| `SVG` | Standalone SVG 1.1 embedding a lossless PNG |
| `PagePDF`, `PDF` | PDF with page images, links, title outlines and optional recognition |

Raw output dimensions are `ceil(geometry.Width * scale)` and
`ceil(geometry.Height * scale)`; each raster plane contains width × height bytes.
Scale one preserves stored pixels; downscaling uses deterministic integer box
averaging in premultiplied space, enlargement uses nearest-neighbor sampling.
Scale changes raster resolution, never PDF physical sheet size.

`WritePNG`, `WriteSVG`, `WritePagePDF` and `WritePDF` take an `io.Writer`
immediately after context. PNG and PDF stream through the C output callback;
SVG holds its bounded embedded PNG before streaming base64. `Raster.GrayImage()`
and `Raster.AlphaImage()` return standard images sharing the corresponding planes.
Streaming output is batched in a 64 KiB bridge buffer, with the final partial
batch delivered only after rendering succeeds. Writer slices are Go-owned and
reused after each `Write`; writers must not retain them, as required by `io.Writer`.
A failed call may leave partial output; discard it, or write a temporary file and rename after success.

Writers, context `Err` and progress functions run on a helper
goroutine synchronously with the operation. A panic or `runtime.Goexit` stops C,
releases results and callback handles, then panics or exits on the calling
goroutine so its defers run. Goroutine-bound state such as `runtime.LockOSThread`
is not inherited. Callbacks may close the notebook or start independent calls,
but must not wait for their calling operation. Cancellation hooks inspect the
channel captured from `ctx.Done()` without a worker call; only cancellation
invokes `ctx.Err()` on the protected worker, once.

## Options and limits

Nil options and `Options{}` select library defaults. `DefaultOptions` and
`DefaultLimits` query the linked C library at runtime; returned limits are independent.

| Field | Effect |
|---|---|
| `Scale` | Zero selects the library default; other values are validated by C |
| `HideBackground` | Omits the stored template layer |
| `Background` | Optional straight sRGB canvas color; channels must be finite in [0,1] |
| `DisableRecognition` | Omits invisible PDF text |
| `StrictImages` | Aborts on damaged image layers instead of logging and skipping |
| `Title` | UTF-8 PDF metadata; empty selects the C default; embedded NUL is rejected |
| `Progress` | PDF completion counts, initially zero, then after each completed page |
| `Limits` | Value struct; each zero field uses its library default |

Counts, bytes, pixels, pages and per-page work use `int`, matching C `size_t` on
supported platforms; cumulative document work uses `uint64`, matching C `uint64_t`.
Negative and unrepresentable limits are rejected with the field name.
Limits cover output pixels, decoded source pixels, pages, per-page work, cumulative
PDF work, output bytes and minimum/maximum scale. Defaults in the pinned library
are 16,777,216 output and decoded pixels, 10,000 pages, 1,000,000,000 per-page work
units, 100,000,000,000 document work units, 512 MiB encoded output and scale 1/64
through 8. PDF preflights all pages before drawing. Image header dimensions are
charged before decoding. Reader parse budgets and its RLE ceiling remain additional
limits. Cairo limits each raster side to 32,767 pixels. Limits do not guarantee
that native codec allocations will succeed.

## Source PDF overlays

Open the `.mark` with `Open`, then supply its source PDF as an immutable `[]byte`
for the duration of each call, including callbacks. All indexes address the source
document, not the mark's sparse sheet array. `PageCount` still counts stored sheets.

| Method | Result |
|---|---|
| `HasAnnotations(page)` | Whether a sheet or highlight names that source page; no PDF bytes needed |
| `OverlayBitmap(ctx, pdf, page, options)` | Go-owned premultiplied RGBA source page with annotations |
| `OverlayPNG(ctx, pdf, page, options)` | PNG preview |
| `OverlayPDF(ctx, pdf, first, count, options)` | Source page objects with transparent raster ink; positive count or `AllPages` |
| `WriteOverlayPNG`, `WriteOverlayPDF` | The corresponding output to `io.Writer`, immediately after context |

Nil options and `OverlayOptions{}` select library defaults. `DefaultOverlayOptions`
queries the linked library. `Render` is the existing `Options` struct: its limits,
context cancellation, progress, errors, writer ownership and abnormal callback
cleanup follow the existing methods. Preview pixels are `ceil(displayed rotated
CropBox points * Render.Scale)`; PDF ink density uses sheet pixels and preserves
the source's physical size. Source colors and visible normal annotation appearances
are drawn in previews; mark ink multiplies the underlying color.

`SourceKind` is `DocumentPDF` by default. `DocumentEPUB` returns `ErrUnsupported`;
the mark reader cannot infer PDF versus EPUB. `Strict` rejects annotations beyond
the source and paired highlight geometry disagreeing by more than three sheet
pixels. Default mode logs warnings and counts omissions. Matching geometry cannot
prove source identity, and marks without pairs provide no such evidence.

`Limits.MaximumSourceBytes` defaults to 512 MiB; zero selects that default, negative
values fail. Parser scratch is bounded by max(1 MiB, 16 * source bytes). `Render.Limits`
bounds pages, pixels, decoded images, work and encoded output. Source page boxes
must be finite, positive and at most 14,400 points per side.

`Result` optionally points to an `OverlayResult`, replaced after C returns even on
failure. Validation rejected in Go leaves it unchanged. It reports
`AnnotatedPagesDrawn`, `SheetsDropped`, `HighlightsDropped`, `WorstPairedResidual`
(sheet pixels), `PagesCheckedByPairs`, `SignatureFields`, `AllAnnotationsDropped`
and `NoPairsAgree`. Use separate result storage for concurrent calls. Whole-document
PDF exports preserve the original byte prefix and append changes; page ranges
rewrite selected pages. Signature fields are reported and warned about: appended
changes follow signing, while range exports drop signatures. Discard partial writer
output on error. EPUB reflow, OCR and signature validation are not provided.

PDFium is process-wide. Upstream serializes this package's Cairo previews internally;
no lock installation is needed when it is the only PDFium user. When sharing
libpdfium with rmrender-go or any other user, call `SetPDFiumLock(mu *sync.Mutex)`
once before the first preview and guard **every external PDFium operation** with
that same mutex, including initialization and cleanup. For rmrender-go this means
wrapping calls that can use PDFium; its private C mutex does not synchronize with
snrender-lib's private mutex. Keep PDFium initialized while either binding can
still be used. Do not hold `mu` when calling an snrender preview or
reenter a preview from an external callback while holding it. PDF exports do not
take this lock. Logs must still not reenter rendering. Lock callbacks operate only
on the concrete mutex, so they run no application code inside C. A successful
installation retains the mutex for the process lifetime; nil, late or repeated
installation returns `ErrInvalidArgument`. Apple returns `ErrUnsupported` for a
non-nil mutex because it uses no PDFium.

## Highlight text

`PageText(ctx, pdf, page, rects, options)` extracts text without opening a
notebook. Page indexes are zero-based. `Rect` uses PDF points, measured right
and down from the displayed rotated CropBox intersected with MediaBox. When
using snnote-go highlights, copy `DocumentRects` into `[]snrender.Rect` and
subtract one from the stored `PageNumber`; screen `Rects` use a different space.

```go
pdf, err := os.ReadFile("testdata/pdf-text/r0-offset0.pdf")
if err != nil {
	panic(err)
}
rects := []snrender.Rect{{X: 19, Y: 16, Width: 100, Height: 18}}
text, err := snrender.PageText(context.Background(), pdf, 0, rects, nil)
if err != nil {
	panic(err)
}
fmt.Println(text)
```

The union selects each character once when its box centre is inside any rectangle
(left/top inclusive, right/bottom exclusive). Rectangle order does not affect
reading order. Nil, null, empty and outside rectangles select nothing. Negative
sizes or nonfinite non-null rectangles return `ErrInvalidArgument`.

Whitespace collapses to spaces or LF and invisible characters are removed. Hard
hyphens at wraps are retained and their wrap removed; soft hyphens and their wraps
are removed when exposed by the engine. No OCR or dictionary dehyphenation occurs.
PDFKit and PDFium can differ for ligatures, soft hyphens, columns and unusual
mappings. Keep source bytes immutable until return. Cairo text uses the same
`SetPDFiumLock` as previews; install a shared lock before either API is first used.

Nil `TextOptions` and zero fields select defaults. `DefaultTextOptions` queries
C: 512 MiB source bytes, 4,096 rectangles, 1,048,576 engine characters. `Limits`
applies pages, per-page/document work and output bytes (including the trailing
C NUL). Work covers parsing and character/rectangle tests. Cancellation runs during
parsing/scans; opaque native engine loading cannot be interrupted or fully metered.

## Atelier drawings

`DrawingBitmap`, `DrawingPNG`, `DrawingPDF` and `DrawingSVG` take context, `.spd`
bytes and `*DrawingOptions`. `WriteDrawingPNG`, `WriteDrawingPDF` and
`WriteDrawingSVG` take `io.Writer` immediately after context. Each call opens with
the embedded `spd_open`, renders, and frees the model; snspd-go objects never cross
module boundaries. Results belong to Go. Writer errors, panic/Goexit cleanup,
partial output and cancellation follow the notebook API.

```go
data, err := os.ReadFile("testdata/spd/valid.spd")
if err != nil {
	panic(err)
}
img, err := snrender.DrawingBitmap(context.Background(), data, nil)
if err != nil {
	panic(err)
}
fmt.Println(img.Bounds())
```

Nil options and `DrawingOptions{}` use `DrawingPaintedBounds`: selected stored
tiles, including transparent/damaged tiles, moved to the top-left; an empty union
produces 1×1. `DrawingCanvas` clips to canvas dimensions centred on the saved
viewport. **Centre anchoring is an inference from one real export.** Tile offsets
and size use saved viewport scale; canvas size stays fixed. Layers composite in
file order on white. `HideReferenceLayer` omits layer id 9999; unknown visibility
flags are not guessed. `Background` accepts a straight sRGB `Color`; alpha zero
selects transparency. `StrictTiles` rejects damaged/unsupported PNG tiles instead
of logging and skipping them. Limits, allocation failures and cancellation abort.

`Scale` changes raster resolution; nonzero `DPI` overrides it with DPI/canvas PPI
(missing PPI defaults to 300). PDF is one raster page, page pixels × 72 / PPI
points, independent of resolution, with a 3-point minimum and 14,400-point maximum
per side. SVG embeds a PNG in page pixel coordinates.

`DefaultDrawingOptions` queries C. `Limits` bounds pixels, decoded pixels, work,
output bytes and scale. `MaximumTiles` defaults to 262,144 (including excluded
reference tiles). `MaximumDecodedBytes` defaults to 256 MiB for cumulative RGBA
tiles plus source/output buffers; opaque codec scratch is excluded. All selected
tiles are checked, including those outside the canvas. Accepted tiles are 128×128
8-bit gray/alpha PNGs, noninterlaced or Adam7, with only IHDR, consecutive IDAT,
and IEND chunks. No new image decoder is installed.

`Parse` independently controls the private SQLite reader. `Strict` rejects unknown
format versions; `RequirePNG128` checks signatures/IHDR before rendering.
`DefaultDrawingParseOptions` reports file, layer, tile, config, heap and work
ceilings. Defaults include 256 MiB input/total tile bytes, 768 MiB live reader heap
and 1 GiB work. Zero Go option fields select defaults; negative counts are invalid.
Parsing is synchronous and cannot be cancelled midway; context is checked before
parsing and throughout rendering. `DrawingParseError` retains signed SQLite
`Value`, normalized `ParseStatus`, and matches the existing reader/resource
sentinels. Disabled upstream SPD builds return `ErrUnsupported`; this vendored
build always enables SPD.

## Errors and logging

Renderer failures are `*Error` with `Status`, `Index`, `Message` and an optional
`Cause`. `errors.Is` matches `ErrInvalidArgument`, `ErrOutOfMemory`, `ErrLimit`,
`ErrUnsupported`, `ErrBackend` and `ErrCancelled`. Cancellation returns an `*Error`
that matches both `ErrCancelled` and the context error (`context.Canceled` or
`context.DeadlineExceeded`). Nil context is invalid.

Reader failures are `*ParseError` with their original `ParseStatus`, format
`Value` and diagnostic. Each reader failure has a sentinel, including
`ErrUnsupportedFormat`, `ErrUnexpectedEOF`, `ErrMalformed`, `ErrImageEncoding`,
`ErrUnsupportedEncoding` and the `ErrParseInvalidArgument`, `ErrParseOutOfMemory`,
`ErrParseLimit` variants. Common resource and unsupported failures also match the
corresponding renderer sentinel. Optional damaged content may be skipped by the
reader; arbitrary mutations do not necessarily make a notebook invalid.

The first failure wins. If a writer refuses first, its exact error is returned;
a short write without an error becomes `io.ErrShortWrite`. A cancellation or limit
found earlier stays the result, even if the writer refuses PDF closing bytes.
Library messages distinguish output callback, allocation and byte-limit failures.

`SetLogger(func(LogLevel, string))` installs a process-wide handler for all embedded
libraries. Nil silences it. Messages describe format facts, never notebook content.
The C bridges are installed once and retain no Go pointer. Handlers run on helper
goroutines and may run concurrently; panic or Goexit loses only the diagnostic.
Changing the logger is safe during rendering; an in-flight line may still arrive
after silence. Logging handlers must not reenter rendering or wait for their call.

## Known limitations

PDF and SVG carry each page as an image, not vector stroke contours. PDF adds title
outlines and supported internal/http/https/mailto links; keywords and document
highlights are not PDF annotations. External files, backlinks and targets outside
the exported range are omitted. A `.mark` renders its own sheets over transparency;
the overlay methods separately draw its annotations over a supplied source PDF.
EPUB layout remains unsupported.

Recognition uses existing stored words, performs no OCR and never paints over the
page. Invalid UTF-8 and invalid or outside boxes are omitted; controls, surrogates
and noncharacters are removed. Stored stream order, separate words and search on
their boxes are guaranteed for ordered nonoverlapping lines with pitch greater
than H/16 + 0.05W. Search tolerance is one quarter box height. Overlapping/nested
boxes, extreme height ratios, dense lines and mixed-script bidi order are outside
that guarantee. Geometric reading order belongs to the viewer. Very long dense
words need raw extraction; geometric extraction may suppress repeated characters.
Words are bounded to 65,536 UTF-8 bytes. Cairo permits 65,533 distinct scalar/chunk
entries per PDF; exceeding that ceiling returns `ErrLimit`.

Apple CoreText shaping may extract Arabic/Indic and other shaped scripts as
fragments; Hebrew remains intact. Apple omits color-font words. Cairo uses an
outline-free font and invisible mask; Poppler returns logical RTL words while
pypdf may expose visual order. No installed recognition font is needed on Cairo.

PNG/JPEG decoder acceptance can differ between backends. JPEG requires EOI and
rejects TEM. Damaged images are skipped by default; limit, cancellation and
allocation failures always abort. Cairo has a known residual crash risk inside
its PDF out-of-memory handling; checked library allocations do not eliminate it.
Native frameworks may retain process-wide caches.
PDFium's own allocation failures can crash during Cairo previews; the library's
allocation checks do not make PDFium out-of-memory handling safe.

Repeated SVG/PNG calls on a backend are deterministic. Native PDF encoders generate
IDs and timestamps, so separate exports need not be byte-identical; tests normalize
only those metadata fields when comparing buffer and writer output.

## Sources and validation

Binding version: **1.0.0**. `Version()` reports the runtime renderer version and:

- [snrender-lib v1.0.0](https://github.com/altessa-s/snrender-lib/tree/v1.0.0): `032b7048d29617c8b9861256b59c3cf9a7bf578f`.
- [snspd-lib v1.0.0](https://github.com/altessa-s/snspd-lib/tree/v1.0.0): `0ac7ba43dcf4b798d9ac5c845dc2eee2b7d45b3b`.
- [snnote-lib v1.0.0](https://github.com/altessa-s/snnote-lib/tree/v1.0.0): `2cab83674ba017abe0e0e42eb5b6e5558a27155f`.

`LibraryVersion` is generated from `internal/clib/VERSION` and checked by the
vendored digest test. The existing
`Version()` function also reports `DrawingReaderCommit`.
`internal/clib/VERSION` records these identities. `scripts/vendor.py` reads pinned
Git objects, copies production sources and licenses, removes allocation-test hooks,
regenerates one root translation unit per source, and writes a digest header and
Go version constant. Upstream `vendor/sqlite/*` files are copied byte-identically
to `internal/clib/snspd/sqlite/`, because Go module archives omit directories
named `vendor`. Tests reject those directories throughout the module's source
tree and check that local cgo include paths and files survive module packaging.
Vendored files are otherwise byte-identical to upstream.
It compiles both backends and checks every external symbol before and after
renaming with `snrgo_`, including the overlay editor's `spe_` and the drawing
reader's `spd_` helpers. SQLite stays static inside the parser and retains its
public-domain provenance. Upstream PDFKit code uses explicit `@finally` releases
and catches exceptions inside each autorelease pool under plain `-fobjc-arc`. Run it on
macOS with the Cairo stack and a fetched PDFium SDK installed. It reads immutable
Git objects, so no tag checkout or temporary worktree is necessary:

```sh
python3 scripts/vendor.py --snnote ../snnote-lib --snrender ../snrender-lib --snspd ../snspd-lib --pdfium "$HOME/pdfium"
go vet ./...
go test ./...
go test -race ./...
GOEXPERIMENT=cgocheck2 go test ./...
go test -run=FuzzRender .
```

The composition suite covers 32 fixtures (69 pages), including 63 exact gray/alpha goldens.
Two additional one-page marks exercise PDF overlays. Other tests cover PNG
pixel agreement, all output methods, writer equivalence, resource limits, lifetime,
cancellation, hostile input, concurrent calls and abnormal callback completion.
The upstream `expected-dumps.sha256` describes reader model dumps, not PNG files;
the rendering oracle is `goldens.tsv`. There are no upstream encoded PNG goldens.
README Go snippets are runnable examples. A bounded native Go fuzz target selects
one rendering operation per input and includes small committed notebook seeds.
Overlay tests reuse upstream N5 and N6 device marks and upstream-generated PDFs,
including paired geometry and blue source markers, cropped/rotated pages,
appearance/form seeds, strict diagnostics, source preservation, limits, cancellation,
writer failure, concurrent previews, callback panic/Goexit and lock installation.
See `testdata/README.md` for provenance and regeneration.

`internal/coexist` links snnote-go, snspd-go and rmrender-go alongside a live host
SQLite connection, and checks concurrent private-reader calls preserve host state.
The coexistence tests require host SQLite headers and a library (`-lsqlite3`).
To test this checkout alongside the published dependencies, use a temporary module file
from the repository root:

```sh
export GOWORK=off
mkdir -p build
cp internal/coexist/go.mod build/coexist.mod
cp internal/coexist/go.sum build/coexist.sum
go -C internal/coexist mod edit -modfile="$PWD/build/coexist.mod" -replace=github.com/altessa-s/snrender-go=../..
go -C internal/coexist test -modfile="$PWD/build/coexist.mod" -race ./...
```

Both Apple and `-tags snrender_cairo` are supported locally; add the PDFium
include path to `CGO_CFLAGS` and supply its library paths for Cairo as above.
The opt-in `TestRealDataSmoke` in the nested module reads only paths supplied via
`SNRENDER_SMOKE_PDF`, `SNRENDER_SMOKE_MARK`, `SNRENDER_SMOKE_SPD`. It reports only
highlight byte and rune counts and drawing PNG dimensions. Private inputs
and rendered outputs are never saved. `FuzzPageText` and `FuzzDrawing` include
synthetic seeds with bounded parser/render work and memory.

## Trademark

Supernote and Supernote Atelier are trademarks of Ratta Software. This project is not affiliated with, endorsed by or sponsored by Ratta Software.

## License

MIT, copyright ALTESSA SOLUTIONS INC. See [LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT).
Vendored libraries retain their licenses under `internal/clib/`; the Supernote
TypeScript fixtures retain their Apache-2.0 license. [Fixture origins and license](testdata/supernote-typescript/README.md).
