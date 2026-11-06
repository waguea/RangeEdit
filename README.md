

# RangeEdit

RangeEdit lets you scan, replace, and delete text in a specific part of a file.

A high-performance text transformation engine in C++ exposed through a first-class CLI and a
VS Code extension.

You can run an operation on the whole document, on a selected block of text, or on a line range you enter yourself.

For example, if you only want to replace `oldHandler` with `newHandler` between lines 40 and 85, use **Replace in Line Range** and enter those boundaries. The rest of the document stays untouched.

## Commands

| Command | What it does |
| --- | --- |
| `RangeEdit: Scan` | Find matching text |
| `RangeEdit: Replace` | Replace matching text |
| `RangeEdit: Delete` | Delete matching text |
| `RangeEdit: Scan in Line Range` | Find matches within specified lines |
| `RangeEdit: Replace in Line Range` | Replace matches within specified lines |
| `RangeEdit: Delete in Line Range` | Delete matches within specified lines |

If you have text selected, RangeEdit uses that selection. Otherwise, it operates on the active document.

Line ranges use 1-based, inclusive line numbers.

## Matching options

Each operation lets you configure:

- Regular expressions
- Case sensitivity
- Whole-word matching

Regex matching uses the bundled RE2-based native engine. Backreferences and lookaround are not supported.

## Under the hood

The text-processing engine is written in C++. The VS Code extension is written in TypeScript and connects to the engine through a Node-API addon.

The native engine supports several literal-search algorithms:

- Naive
- Boyer–Moore–Horspool
- Knuth–Morris–Pratt
- Standard library search

The algorithm can be selected in the extension settings.

## Settings

| Setting | Default | Description |
| --- | --- | --- |
| `rangeedit.nativeMode` | `auto` | Selects whether to use the native addon |
| `rangeedit.algorithm` | `bmh` | Literal-search algorithm |
| `rangeedit.maxMatches` | `0` | Maximum matches per operation; `0` means unlimited |
| `rangeedit.defaultRegex` | `false` | Default regex option |
| `rangeedit.defaultCaseSensitive` | `true` | Default case-sensitivity option |
| `rangeedit.defaultWholeWord` | `false` | Default whole-word option |
| `rangeedit.addonPath` | empty | Optional path to a native addon |

## Requirements

- VS Code 1.105 or newer.
- A package built for your operating system and CPU architecture when using the native addon.

## Limitations

- Case-insensitive literal matching and whole-word boundaries are ASCII-only.
- Regex backreferences and lookaround are unsupported.
- Regex input is converted to UTF-8 internally.
- The TypeScript literal fallback checks for cancellation between matches.
- Operations are limited to one document or selection. There is no workspace-wide or multi-file editing.
- The operation summary reports edit statistics, not a full diff.

## Privacy

RangeEdit does not collect telemetry or make network requests.

## License

MIT
