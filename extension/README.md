# RangeEdit

Scan, replace, and delete text by range inside the active document or selection.

RangeEdit keeps the operation explicit: it scans first, reports match and edit statistics, then
applies the result through VS Code's native edit API. Replace and delete operations participate
in normal undo/redo, and cancelled scans never modify the document.

## Commands

| Command | Description |
| --- | --- |
| `RangeEdit: Scan` | Find matches and list their offsets in the RangeEdit output channel. |
| `RangeEdit: Replace` | Find matches, preview edit statistics, then replace them. |
| `RangeEdit: Delete` | Find matches, preview edit statistics, then delete them. |
| `RangeEdit: Scan in Line Range` | Same as Scan, restricted to a 1-based inclusive line range. |
| `RangeEdit: Replace in Line Range` | Same as Replace, restricted to a 1-based inclusive line range. |
| `RangeEdit: Delete in Line Range` | Same as Delete, restricted to a 1-based inclusive line range. |

If text is selected, the operation is limited to that selection. Otherwise it runs on the whole
active document. The line-range commands always ask for a start line and an end line; both are
1-based, and the end line's text is included without its trailing newline.

The options picker controls regex, case sensitivity, and whole-word matching. Regex uses the
bundled RE2-based native engine. Regex backreferences and lookaround are not supported.

## Settings

| Setting | Default | Description |
| --- | --- | --- |
| `rangeedit.nativeMode` | `auto` | `auto` uses the fast TypeScript path for literal scans and the native addon for regex. `always` requires the native addon. `never` disables it. |
| `rangeedit.algorithm` | `bmh` | Literal algorithm in the native engine: `naive`, `bmh`, `kmp`, or `std`. |
| `rangeedit.maxMatches` | `0` | Maximum matches per operation. `0` means unlimited. |
| `rangeedit.defaultRegex` | `false` | Start the options picker with regex enabled. |
| `rangeedit.defaultCaseSensitive` | `true` | Start the options picker with case-sensitive matching enabled. |
| `rangeedit.defaultWholeWord` | `false` | Start the options picker with whole-word matching enabled. |
| `rangeedit.addonPath` | empty | Optional explicit path to `rangeedit.node` for development builds. |

## Requirements

- VS Code 1.105 or newer.
- The Marketplace package includes the native addon for its target platform. Install the package
  matching your OS and CPU.

## Limitations

- Literal case-insensitive matching and word boundaries are ASCII-only.
- Regex input is converted to UTF-8 internally, so work scales with UTF-8 byte length.
- Regex backreferences and lookaround are not supported.
- The TypeScript literal fallback checks cancellation between matches; use
  `rangeedit.nativeMode=always` for cancellable large literal scans.
- Commands operate on one document or selection. Workspace-wide search and multi-file edits are
  not supported.
- The preview shows edit statistics, not a diff.

## Privacy

RangeEdit has no telemetry and makes no network requests.

## License

MIT
