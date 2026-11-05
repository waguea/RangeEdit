# RangeEdit

A high-performance text transformation engine in C++ exposed through a first-class CLI and a
VS Code extension.

RangeEdit is deliberately not just "find and replace." The project is built around measurable
systems properties: matching algorithms, memory ceilings, streaming I/O, cancellation, and
safe range-based edits.

## Status

Milestone M0-M6 is complete at the scope recorded in the docs: repository skeleton, literal
and regex match engines, edit planning and application, CLI scan/replace/delete, streaming
literal scan, unit and CLI contract tests, microbenchmarks, the TypeScript decision gate, the
Node-API addon with async scan/edit, cancellation, and progress, and the VS Code extension with
literal fallback and addon-backed regex. The M4 gate found no C++ literal-throughput advantage
but a large C++ memory advantage (ADR-0007). The M5 addon gate confirmed that the TypeScript
path stays the literal default while the addon covers regex/edit/UTF-16 work and cancellable
long scans (ADR-0008). The M6 extension applies edits through VS Code `WorkspaceEdit`
(ADR-0010).

Implemented:

- Literal matching over UTF-8 (`char`) and UTF-16 (`char16_t`) code units
- Naive, Boyer-Moore-Horspool, Knuth-Morris-Pratt, and `std::search` implementations
- ASCII case-insensitive and whole-word options for literal matching
- RE2 regex matching with Unicode-aware case folding, whole-word anchors, and UTF-16 offset mapping
- Non-overlapping matches with configurable result caps
- Edit planning with bounds, ordering, overlap, and UTF boundary validation
- Edit application with dry-run stats (removed/inserted/output units)
- Streaming literal scanner with chunk-boundary and whole-word handling
- CLI `scan`, `replace`, and `delete` commands
- CLI `--dry-run`, `--json`, and atomic `--output` writing
- Differential and exhaustive unit tests plus automated CLI contract tests
- Google Benchmark microbenchmarks
- Cooperative cancellation and progress hooks in literal and regex matching
- Node-API addon with async `scan`/`replace`/`delete`, UTF-16 offsets, cancellation, and progress
- VS Code extension with scan, replace, and delete commands over the selection or document
- TypeScript literal fallback and addon-backed regex, both behind one command surface
- VS Code progress/cancellation, output-channel results, preview stats, and per-match
  `WorkspaceEdit` application

Known limitations:

- Literal case-insensitive matching and word boundaries are ASCII-only in v1.
- Regex matching converts UTF-16 input to UTF-8 internally, so regex work is proportional to
  the UTF-8 byte length of the input.
- RE2 does not support backreferences or lookaround; unsupported syntax is reported as an error.
- Cancellation is checked every 16 KiB during literal scans and between regex matches. A single
  long RE2 search cannot be interrupted mid-search.
- The addon copies Node strings to native UTF-16 before matching, so its memory use scales with
  the selected text and it is not a substitute for the streaming CLI on very large files.
- No prebuilt addon binaries; the addon is built against local Node headers.
- The extension is not packaged or published; it runs from source in an Extension Development
  Host.
- The extension preview is a confirmation with edit statistics, not a diff view.
- The TypeScript fallback checks cancellation between matches; a long single `indexOf` call is not
  interruptible. Use `rangeedit.nativeMode=always` for cancellable large literal scans.
- Extension commands operate on one document or selection; workspace-wide search and
  multi-file edits are out of scope for v1.

Not yet implemented (see the roadmap in `docs/ARCHITECTURE.md`): mmap I/O, incremental
rescanning, SIMD candidate scanning, and regex capture references. Literal CLI scans stream in
64 KiB chunks; regex scans and replace/delete still read the whole file.

## Requirements

- CMake 3.24+
- A C++20 compiler (Apple Clang, GCC, or MSVC)
- Network access on first configure (Abseil, RE2, GoogleTest, and Google Benchmark are fetched
  by CMake)
- Node.js and npm for the optional addon and VS Code extension

## Build and test

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DRANGEEDIT_WARNINGS_AS_ERRORS=ON
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

Or with presets:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Debug and sanitizer presets are available as `debug` and `asan`.

The Node-API addon is an optional target. It requires Node headers for the running Node version:

```sh
cmake --preset addon
cmake --build --preset addon
ctest --preset addon -R AddonTest --output-on-failure
```

If headers are not next to the `node` executable, pass
`-DRANGEEDIT_NODE_INCLUDE_DIR=/path/to/node/include/node`. Windows also needs
`-DRANGEEDIT_NODE_LIBRARY=/path/to/node/lib/node.lib` when it cannot be discovered.

## CLI

```sh
./build/release/cli/rangeedit scan --pattern needle path/to/file.txt
./build/release/cli/rangeedit scan --pattern needle --ignore-case --whole-word --max 10 file.txt
./build/release/cli/rangeedit scan --pattern needle --algorithm kmp file.txt
./build/release/cli/rangeedit scan --regex --pattern 'needle|haystack' --ignore-case file.txt
./build/release/cli/rangeedit scan --pattern needle --json file.txt
./build/release/cli/rangeedit replace --pattern needle --replacement thread file.txt
./build/release/cli/rangeedit delete --pattern '\s+$' --regex file.txt
./build/release/cli/rangeedit replace --pattern needle --replacement thread --dry-run --json file.txt
./build/release/cli/rangeedit replace --pattern needle --replacement thread --output file.txt file.txt
```

Use `--algorithm naive|bmh|kmp|std` to compare literal implementations. Add `--regex` to use
RE2 instead. Literal scans stream the file; regex scans and edit commands read it whole for
now. Scan output is line/column plus byte offsets for UTF-8 input.

`--dry-run` reports edit-count and removed/inserted/output unit stats plus edit ranges without
writing output. `--json` emits a JSON array for scans and a JSON plan for dry-run edits. It is
only valid for replace/delete together with `--dry-run`. `--output` writes through a temporary
file and atomic rename, including when the input and output paths are the same. Invalid option
combinations and invalid regular expressions exit with status 2.

The edit API is also available to embedders through `rangeedit/edit.hpp`.

## Node-API addon

The addon exposes asynchronous operations over UTF-16 text. Offsets are UTF-16 code units, which
matches VS Code positions.

```js
const { createCancelToken, replace, scan } = require("./build/addon/rangeedit.node");

const token = createCancelToken();
const result = await scan(text, "needle", {
    caseSensitive: false,
    wholeWord: true,
    maxMatches: 1000,
    algorithm: "bmh",
    cancelToken: token,
    onProgress: ({ processed, total }) => console.log(processed, total),
});

const edit = await replace(text, /needle/gi.source, "thread", {
    regex: true,
    dryRun: true,
});
```

`scan` resolves to `{ matches, cancelled, processed }`. `replace` and `delete` add
`{ edits, stats, text? }`; `text` is omitted for `dryRun` and cancellation. Options are
`regex`, `caseSensitive`, `wholeWord`, `maxMatches`, `algorithm`, `cancelToken`, `onProgress`,
and (for edits) `dryRun`. `algorithm` is `naive|bmh|kmp|std` and defaults to `bmh`. Invalid option
values throw synchronously; invalid regexes reject the promise. Cancellation resolves with
`cancelled: true` rather than rejecting. Progress callbacks are delivered before the operation
promise resolves.

## VS Code extension

The extension is a separate npm package under `extension/`. It uses the TypeScript literal
fallback by default (ADR-0008) and requires the native addon for regex.

```sh
cd extension
npm install
npm run compile
npm run test:unit
npm run test:integration
```

`test:integration` downloads a VS Code build on first use and launches an Extension Development
Host. Set `rangeedit.addonPath` if the addon is not in a discoverable build directory. When
running the extension from this repository, the addon is found at `build/addon/rangeedit.node`
after `cmake --preset addon`.

The commands are:

- `RangeEdit: Scan` — show match positions in the RangeEdit output channel.
- `RangeEdit: Replace` — preview edit statistics, then apply through `WorkspaceEdit`.
- `RangeEdit: Delete` — delete matches after the same preview.

With a non-empty selection the commands operate on the selection; otherwise they operate on the
whole document. `rangeedit.nativeMode` controls native use: `auto` uses the TypeScript fallback
for literal matching and the addon for regex, `always` forces the addon, and `never` disables it.
`rangeedit.algorithm` and `rangeedit.maxMatches` tune the native path.

## Benchmarks

```sh
./build/release/engine/rangeedit_bench
node scripts/generate-corpus.ts --output /tmp/rangeedit-m4/random-100.bin --size-mib 100
node scripts/ts-baseline.ts --file /tmp/rangeedit-m4/random-100.bin --pattern needle --runs 5
node --experimental-strip-types scripts/addon-baseline.ts \
  --file /tmp/rangeedit-m4/random-100.bin --pattern needle --runs 5
```

Benchmark methodology, M4/M5 results, the M6 integration note, and reporting expectations live
in `docs/BENCHMARKS.md`.

## Design decisions

Locked decisions and their consequences are recorded in `docs/DECISIONS.md`. The short
version:

- ASCII-only case folding and word boundaries in v1
- Developer builds only; no prebuilt addon binaries yet
- CLI is a supported product surface
- The VS Code extension will include a small TypeScript fallback that also serves as a
  differential-test oracle
- The C++ core must justify itself with measurements, not language choice alone
- The addon is built directly with CMake and the Node-API C ABI; no cmake-js or node-addon-api
- The extension uses the TypeScript path for literal matching and the addon for regex; edits are
  applied per match through `WorkspaceEdit` for native undo
