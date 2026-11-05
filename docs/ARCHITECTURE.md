# Architecture

## Shape of the system

```
VS Code extension (TypeScript)
  commands · preview · progress/cancel · WorkspaceEdit
       ├── TypeScript literal fallback
       └── Node-API addon (in-process, async)
                    │
                    ▼
       C++ engine (rangeedit_core)
         TextBuffer → MatchEngine → EditEngine → results / edit list
                    │
                    ├── literal search: naive, BMH, KMP, std::search, (SIMD later)
                    ├── regex search: RE2 behind the same matcher interface
                    ├── validation, cancellation, result caps
                    └── streaming / mmap file I/O
                    │
                    ▼
       CLI (same engine) + benchmark harness
```

The extension and CLI are adapters. Matching and edit semantics live in one place.

## Current implementation (M0-M6 done)

`rangeedit_core` currently exposes literal and regex matching through
`engine/include/rangeedit`:

- `types.hpp` — `Match`, `MatchList`, `MatchOptions`, `LiteralAlgorithm`, `CancellationToken`,
  `ProgressCallback`
- `literal.hpp` / `literal.cpp` — UTF-8 and UTF-16 literal search
- `regex.hpp` / `regex.cpp` — RE2-backed regex search with offset mapping
- `edit.hpp` / `edit.cpp` — edit planning, validation, dry-run stats, and application
- `stream.hpp` / `stream.cpp` — chunked literal scanning with overlap handling
- `addon/src/addon.cpp` — Node-API adapter for async scan/replace/delete, cancellation, and
  progress

All matchers share the same result contract:

- Matches are **non-overlapping**; after an accepted match the search resumes at its end.
- Empty patterns/needles produce no matches.
- `max_matches == 0` produces no matches.
- Results are `Match{start, length}` in code units of the input view.
- Literal case-insensitive matching and whole-word boundaries are **ASCII-only**; literal
  whole-word uses `[A-Za-z0-9_]` as word characters.
- Regex matching uses RE2's Unicode case folding and `\b` word boundaries.

### Algorithms

| Algorithm | Role | Worst case |
|---|---|---|
| Naive | Reference and test oracle | O(n·m) |
| Boyer-Moore-Horspool | Default; good on natural text | O(n·m) worst, sublinear typical |
| Knuth-Morris-Pratt | Linear-time comparison point | O(n + m) |
| `std::search` | Standard-library baseline | Implementation-defined |

BMH uses a bad-character table sized to the code unit. UTF-16 tables are 65,536 entries
(512 KiB with 64-bit `size_t`), which is acceptable for large scans but worth revisiting for
many small searches.

### Regex

`find_regex` uses RE2 for both UTF-8 and UTF-16 inputs:

- UTF-8 input is searched directly and returns byte offsets.
- UTF-16 input is converted to UTF-8, searched, then match byte offsets are mapped back to
  UTF-16 code units.
- Lone surrogates are replaced with `U+FFFD` before searching; offsets remain best-effort for
  malformed UTF-16.
- Empty patterns produce no matches, `max_matches == 0` produces no matches, and zero-length
  matches advance by one UTF-8 code point to guarantee progress.
- Whole-word mode wraps the pattern as `\b(?:pattern)\b`; case-insensitive mode uses RE2's
  Unicode case folding.
- RE2 memory is capped at 64 MiB per compiled expression. Unsupported syntax and resource
  errors are returned as strings rather than thrown.
- RE2 is compiled with a pinned, locally fetched Abseil to avoid system ABI/config mismatches.

### Streaming literal scan

`LiteralStreamScanner` is the first streaming path in the engine:

- `feed(chunk)` searches the retained carry plus the new chunk and returns newly accepted
  matches; `finish()` resolves whole-word matches deferred at end-of-input.
- The carry is at most `needle_length + 1` bytes, enough to detect matches crossing a chunk
  boundary and to inspect one character of whole-word context on each side.
- Whole-word matches ending exactly at a chunk boundary are deferred until the next chunk (or
  `finish`) supplies the following character. Deferred matches are resolved before later
  matches so `max_matches` still returns the earliest results.
- Match offsets are global byte offsets; the CLI maintains line/column while consuming chunks.
- Literal CLI `scan` streams in 64 KiB chunks. Regex scanning and replace/delete still use a
  whole-file buffer.

### Cancellation and progress

- `MatchOptions` carries an optional `CancellationToken*` and `ProgressCallback`.
- Literal matchers check cancellation every 16 KiB and report progress every 1 MiB.
- Regex matching checks cancellation between matches; a single long RE2 search is not
  interruptible.
- Cancellation is cooperative: partial match results are not returned by the native API; the
  operation reports `cancelled` with the last progress checkpoint.
- Progress callbacks are advisory and may throttle a scan while they run.

## Node-API addon

`addon/src/addon.cpp` is a plain Node-API C adapter, built directly with CMake when
`RANGEEDIT_BUILD_ADDON=ON`:

- `scan`, `replace`, and `delete` run on the libuv threadpool through `napi_create_async_work`,
  so the extension host stays responsive.
- Input strings are copied to native UTF-16. Literal matches use `char16_t` directly; regex
  matches convert to UTF-8 and map offsets back to UTF-16 code units.
- `createCancelToken()` returns a tagged native object. Tokens are referenced for the lifetime
  of an async operation and can be cancelled from JavaScript while the worker runs.
- Progress is posted with a `napi_threadsafe_function`. The worker waits for an explicit
  acknowledgment from the JS callback, so progress events, including the final event, are
  delivered before the operation promise settles.
- Invalid option values throw synchronously; regex compilation and native failures reject the
  promise. Cancellation resolves with `cancelled: true`.
- Edits are planned only after matching completes, so a cancelled scan never applies partial
  replacements.

## VS Code extension

`extension/` is a TypeScript extension with one command surface over the engine:

- `rangeedit.scan` shows match positions in a RangeEdit output channel.
- `rangeedit.replace` and `rangeedit.delete` scan first, show edit statistics, then apply through
  `WorkspaceEdit`.
- A non-empty selection scopes the operation to that range; otherwise the whole document is used.
- `rangeedit.nativeMode=auto` uses the TypeScript literal fallback and the addon for regex.
  `always` forces the addon; `never` disables it.
- The addon path comes from `RANGEEDIT_ADDON`, `rangeedit.addonPath`, or discovery of
  `build/addon/rangeedit.node` and `build-addon/rangeedit.node`.
- VS Code progress maps to engine progress, and VS Code cancellation cancels the addon token.
  Cancelled scans never apply edits.
- Edits are applied per match rather than replacing the whole document, preserving native undo
  and avoiding changes outside the target range.
- Integration tests run in an Extension Development Host through `@vscode/test-electron`.

### Extension fallback

The TypeScript fallback implements literal matching with the same v1 semantics as the engine:
non-overlapping matches, no empty-pattern matches, ASCII-only case folding, ASCII whole-word
boundaries, result caps, and UTF-16 code-unit offsets. It does not implement regex; regex
requires the native addon. Cancellation is checked between matches, so a long single
`String.indexOf` call is not interruptible. Edit statistics and per-match planning are shared by
both paths.

## Encoding boundary

VS Code positions are UTF-16 code-unit offsets. RE2 operates on UTF-8. The literal path stays
native to each code unit, while the regex path converts UTF-16 to UTF-8 and returns offsets
mapped back to UTF-16 code units. The engine never assumes byte offsets equal character
offsets.

Addon path:

1. Accept UTF-16 text from Node-API.
2. Run literal matching directly on `char16_t`.
3. For regex, convert to UTF-8, run RE2, and map match byte offsets back to UTF-16 offsets.
4. Reject edits that split surrogate pairs.

## Components and roadmap

### Match engine

- `find_literal` (done)
- `find_regex` via RE2 (done)
- Cooperative cancellation and progress (done)
- Options for overlap policy and start offset
- Optional SIMD candidate scanning only after measurements justify it

### Edit engine

- `plan_edits(input, matches, replacement, mode) -> EditPlan` (done for replace/delete)
- `apply_edits(input, plan.edits, replacement) -> new text` (done)
- Modes: replace and delete done; keep-first, keep-last, and regex capture references planned
- Validation: strictly increasing starts, non-overlapping, in-bounds, UTF-8 sequence and
  UTF-16 surrogate-pair safe
- Dry run returns all edit ranges plus removed/inserted/output unit counts without mutating
- Future: apply through VS Code `WorkspaceEdit` for native undo; CLI uses temp file + atomic
  rename

### I/O

- Chunked literal scanning is implemented; the CLI streams literal `scan` in 64 KiB chunks
- Whole-file reads remain for regex scan and replace/delete
- Add mmap read-only scanning as a benchmark/implementation comparison
- M4 decision gate recorded in ADR-0007; M5 addon gate recorded in ADR-0008
- Retain memory/throughput benchmarks as regressions
- Cap match counts and stream results to avoid building millions of edits

### Concurrency

- Implemented: native work runs on the libuv threadpool through `napi_create_async_work`
- Implemented: cooperative cancellation through an atomic token checked every 16 KiB of literal
  input or between regex matches
- Implemented: progress via a thread-safe function with synchronous delivery acknowledgment
- Watch libuv threadpool starvation; consider a dedicated engine thread for long scans

### Extension follow-ups

- Diff-style preview instead of the current confirmation with statistics
- Workspace-wide multi-file search and edit (explicitly out of v1 scope)
- Packaged extension and addon binaries
- Extension-host performance measurements before making further default-path claims

### Incremental processing (stretch)

- Use document change ranges to rescan dirty regions
- Expand each dirty region by `needle_length - 1` on both sides
- Maintain a persistent match index; invalidate only affected entries

## Repository layout

```
engine/include/rangeedit/   public headers
engine/src/                 implementation
engine/tests/               GoogleTest suites
engine/bench/               Google Benchmark suites
cli/                        first-class CLI
addon/                      Node-API addon (M5, done)
extension/                  VS Code extension (M6, done)
scripts/                    corpus generation, TS baseline, addon baseline
docs/                       architecture, benchmarks, decisions
```

## Milestones

| Milestone | Scope | Status |
|---|---|---|
| M0 | Skeleton, CMake, CI, docs | Done |
| M1 | Literal engine, tests, microbenchmarks | Done |
| M2 | RE2 regex + offset mapping | Done |
| M3 | Edit engine + validation + randomized property tests | Done |
| M4 | CLI I/O modes + TS baseline + decision gate | Done; cancellation/FTFR measured in M5 |
| M5 | Node-API addon + async/cancel/progress | Done |
| M6 | VS Code extension + fallback + integration tests | Done |
| M7 | Incremental rescan and measured SIMD | Stretch |

## Non-goals for v1

- Workspace-wide multi-file search
- Full PCRE feature parity
- Unicode case folding and word segmentation for literal matching (ICU is a later decision)
- Multi-cursor-aware edit transformation
