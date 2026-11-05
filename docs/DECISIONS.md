# Decision log

Architecture decisions are recorded here in a lightweight ADR format. New decisions should
include context, the decision, and consequences.

---

## ADR-0001: ASCII-only Unicode semantics in v1

- **Date:** 2026-11-04
- **Status:** Accepted

### Context

Correct Unicode case folding and word segmentation require ICU or equivalent tables and
substantially expand the correctness surface. The product's first goal is fast, safe literal
matching with predictable ranges.

### Decision

Case-insensitive literal matching and whole-word boundaries are ASCII-only in v1. Unicode
semantics are available through RE2 regex matching. Engine interfaces remain encoding-aware
so ICU support can be added without a breaking API change.

### Consequences

- v1 behavior is easy to specify and test.
- Users matching non-ASCII case variants must use regex mode.
- Documentation must state the limitation prominently.

---

## ADR-0002: Developer builds only

- **Date:** 2026-11-04
- **Status:** Accepted

### Context

Prebuilt native addons require per-platform build matrices, ABI compatibility, signing, and
release infrastructure. The engine and addon are not stable yet.

### Decision

Ship local developer builds only for now. CI compiles and tests on macOS, Linux, and Windows,
but does not publish binaries.

### Consequences

- Faster iteration and no release engineering yet.
- End users must have a compiler toolchain to build the addon.
- The distribution decision must be revisited before any public release.

---

## ADR-0003: CLI is a first-class surface

- **Date:** 2026-11-04
- **Status:** Accepted

### Context

The CLI is the cleanest place to exercise file I/O, streaming, memory ceilings, and
algorithm comparisons independently of the editor. It also makes the engine usable without
VS Code.

### Decision

The CLI supports scan, replace, delete, dry-run, and JSON output, and shares the engine with
the extension. It is not a benchmark-only harness.

### Consequences

- CLI behavior is part of the public contract and needs tests.
- Performance work can be validated outside the extension host.
- Additional argument parsing and output-format maintenance.

---

## ADR-0004: TypeScript fallback in the extension

- **Date:** 2026-11-04
- **Status:** Accepted; performance assumption refined by ADR-0007

### Context

A native addon can fail to load because of ABI mismatches, missing toolchains, or platform
differences. A fallback keeps the extension usable. A second implementation is also useful as
a differential-test oracle.

### Decision

The extension includes a small TypeScript implementation of literal matching and uses it when
the addon cannot load, with a visible warning. The fallback is not a full engine and is not
the performance path.

### Consequences

- The extension degrades gracefully instead of failing.
- Two implementations must agree on match semantics; differential tests enforce this.
- Some duplicated logic and test maintenance.

---

## ADR-0005: Non-overlapping matches in v1

- **Date:** 2026-11-04
- **Status:** Accepted

### Context

Find/replace workflows almost always want non-overlapping matches. Overlapping matches
require a different result model and different edit semantics.

### Decision

Accepted matches advance the search position to the end of the match. Overlap support is
deferred and will be an explicit option, not a change in default behavior.

### Consequences

- `"aa"` in `"aaaa"` yields matches at offsets 0 and 2, not 0, 1, 2.
- Edit planning is simpler and results are stable across algorithms.

---

## ADR-0006: Pin and fetch Abseil/RE2

- **Date:** 2026-11-04
- **Status:** Accepted

### Context

The first regex build linked a locally compiled RE2 against Homebrew's system Abseil. The
installed Abseil version and the headers RE2 was compiled with disagreed on internal
`raw_hash_set` layout, causing a segmentation fault inside `re2::DFA::Search` in sanitizer
builds. Relying on whichever Abseil happens to be installed also makes CI and developer builds
non-reproducible.

### Decision

Fetch pinned Abseil (`20250814.1`) and RE2 (`2025-11-05`) releases with CMake `FetchContent`
whenever `RANGEEDIT_BUILD_REGEX=ON`. Disable upstream installs and tests. Do not discover a
system RE2/Abseil for the core build.

### Consequences

- One dependency graph for all platforms and sanitizer configurations.
- First configure needs network access and builds more code.
- Dependency updates are explicit tag changes rather than ambient system upgrades.
- Third-party headers require suppressing Clang's `-Wnullability-extension` warning because
  the project treats warnings as errors.

---

## ADR-0007: M4 decision gate — no literal-throughput claim for C++

- **Date:** 2026-11-05
- **Status:** Accepted; M5 follow-up in ADR-0008

### Context

M4 required an honest comparison against a TypeScript baseline before committing further to the
native addon. On an Apple M4 with Node 24, literal scan throughput and peak memory were measured
for 1 MiB, 16 MiB, 100 MiB, and 1 GiB workloads (details in `docs/BENCHMARKS.md`).

The measurements showed:

- Node's native `String.indexOf` is 13-16% faster than C++ BMH for literal scan-only workloads.
- The C++ streaming CLI uses about 25x less peak RSS than Node streaming at 1 GiB and about
  50x less than Node whole-file at 100 MiB.
- C++ wins the 100 MiB end-to-end CLI comparison but not the 1 GiB streaming comparison.
- Regex, edit application, UTF-16 mapping, time to first result, and cancellation were not
  measured as part of this gate.

### Decision

Do not claim that C++ is the faster literal scanner. Keep the C++ engine and CLI as the
memory-bounded path and as the home for regex, edits, UTF-16 mapping, and large-file streaming.
Keep the TypeScript fallback as an acceptable default for small literal scans. Defer the final
extension default-path decision until M5 can measure the actual addon end-to-end, including
cancellation and time to first result.

### Consequences

- Benchmark reporting must lead with memory and non-literal engine capabilities, not literal
  throughput.
- The C++ literal algorithms are still useful for embedding and memory ceilings, but are not a
  blanket performance justification.
- M5 addon benchmarks must include the extension host path and cancellation, not only engine
  microbenchmarks.
- If the addon does not show an end-to-end advantage, the extension should keep using the
  TypeScript path for literal operations.
- M5 follow-up: ADR-0008 measured the addon in a Node process, not inside VS Code. The actual
  extension-host path remains an M6 validation step.

---

## ADR-0008: M5 addon gate — TypeScript remains the literal default

- **Date:** 2026-11-05
- **Status:** Accepted; extension split implemented in ADR-0010

### Context

ADR-0007 deferred the extension default-path decision until the actual Node-API addon could be
measured end-to-end, including time to first result and cancellation. The M5 addon now exposes
async `scan`, `replace`, and `delete` over UTF-16 text. On the 100 MiB corpus (in-process text,
5 runs), `scripts/addon-baseline.ts` measured:

- Node `String.indexOf` scan: 110.3 ms, 906.9 MiB/s.
- Addon scan: 147.0 ms, 680.4 MiB/s (includes the mandatory Node-API UTF-16 copy and match
  result materialization).
- First match: Node 0.006 ms; addon 8.846 ms because the addon copies the whole string before
  scanning.
- Cancellation after the first 1 MiB progress checkpoint: 0.025 ms median latency, 5/5 runs
  cancelled.

### Decision

Keep the TypeScript path as the default for in-memory literal search, especially when time to
first result matters. Use the addon for regex, edit application, UTF-16 offset mapping, and
long literal scans where cooperative cancellation or native memory behavior is the reason.

### Consequences

- The extension should not route every literal query through the addon.
- Addon benchmarks must report both whole-scan throughput and first-result latency, not only
  engine-only throughput.
- The streaming C++ CLI remains the large-file path; the addon's input copy is a real cost.
- M6 implemented this split. Extension-host performance measurement remains a follow-up before
  any further default-path claim.

---

## ADR-0009: Build the addon directly on Node-API with CMake

- **Date:** 2026-11-05
- **Status:** Accepted

### Context

The addon needs to build on developer machines without introducing a second build system or a
JavaScript package dependency at configure time. The project already uses CMake, and the
extension is not yet packaged. `cmake-js` and `node-addon-api` would add layers that are not
needed for three async entry points and a cancel token.

### Decision

Build the addon as an optional CMake target using the Node-API C ABI:

- Detect the running `node` executable and derive its include directory (with an override for
  non-standard layouts).
- Compile `addon/src/addon.cpp` with `NAPI_VERSION=8` into `rangeedit.node`.
- Use `napi_create_async_work` for background execution, `napi_threadsafe_function` for progress,
  and a tagged native object for cancellation tokens.
- Keep the addon off by default; `RANGEEDIT_BUILD_ADDON=ON` enables it.
- No prebuilt binaries and no npm package yet.

### Consequences

- The addon ABI follows the developer's Node version; rebuild after Node upgrades.
- The addon build shares the C++ core and compiler flags with the rest of the project, including
  warnings-as-errors.
- Packaging and prebuilt binary distribution remain M6+ work.
- Progress delivery is deterministic but the worker blocks until the JS callback returns, so a
  slow `onProgress` handler throttles the scan.

---

## ADR-0010: Extension command surface and WorkspaceEdit application

- **Date:** 2026-11-05
- **Status:** Accepted

### Context

M6 needs a VS Code command surface that works before packaging, preserves native undo, and can
be tested without driving input boxes. The addon can return an entire edited string, but replacing
a document wholesale would discard per-edit undo granularity and could overwrite concurrent
changes outside the target range. The TypeScript fallback also needs to stay behaviorally aligned
with the engine so it remains a useful oracle.

### Decision

- Implement `rangeedit.scan`, `rangeedit.replace`, and `rangeedit.delete` over the active
  selection when non-empty, otherwise the whole document.
- Use the TypeScript literal fallback by default; require the native addon for regex. Allow
  `rangeedit.nativeMode` to force `always` or `never`.
- Scan first, then apply one `WorkspaceEdit.replace` per match. Never apply edits after a
  cancelled scan.
- Show removed/inserted/output code-unit statistics in a modal confirmation before applying,
  unless structured command arguments request `apply: true`.
- Expose structured `CommandArgs` on the commands so integration tests can run without UI
  prompts; `silent: true` suppresses notifications.
- Test the command surface inside an Extension Development Host with `@vscode/test-electron`,
  including addon literal, fallback literal, regex, selection scope, replace, and delete.

### Consequences

- Native undo works at match granularity and edits stay inside the chosen range.
- Documents with very large match counts can produce large workspace edits; `rangeedit.maxMatches`
  is the escape hatch.
- Regex capture references are still unavailable because the engine does not expose captures.
- The extension is source-run only; packaging and prebuilt addon distribution remain follow-ups.
- The fallback and engine must keep matching semantics aligned; unit tests cover the shared
  literal rules and edit statistics.
