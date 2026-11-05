import type { Match, ProgressEvent, ScanOptions, ScanResult } from "./types";

const PROGRESS_INTERVAL = 1024 * 1024;

export function foldAscii(text: string): string {
    let folded = "";
    for (let index = 0; index < text.length; ++index) {
        const code = text.charCodeAt(index);
        folded += code >= 65 && code <= 90 ? String.fromCharCode(code + 32) : text[index];
    }
    return folded;
}

export function isAsciiWordUnit(code: number): boolean {
    return (
        (code >= 48 && code <= 57) ||
        (code >= 65 && code <= 90) ||
        (code >= 97 && code <= 122) ||
        code === 95
    );
}

function hasWordBoundaries(text: string, start: number, end: number): boolean {
    if (start > 0 && isAsciiWordUnit(text.charCodeAt(start - 1))) {
        return false;
    }
    if (end < text.length && isAsciiWordUnit(text.charCodeAt(end))) {
        return false;
    }
    return true;
}

export function scanLiteral(
    text: string,
    pattern: string,
    options: ScanOptions,
    isCancelled: () => boolean = () => false,
    onProgress: (event: ProgressEvent) => void = () => undefined,
): ScanResult {
    const matches: Match[] = [];

    if (pattern.length === 0 || options.maxMatches === 0) {
        onProgress({ processed: text.length, total: text.length });
        return { matches, cancelled: false, processed: text.length };
    }
    if (isCancelled()) {
        onProgress({ processed: 0, total: text.length });
        return { matches, cancelled: true, processed: 0 };
    }

    const haystack = options.caseSensitive ? text : foldAscii(text);
    const needle = options.caseSensitive ? pattern : foldAscii(pattern);
    let searchFrom = 0;
    let lastProgress = 0;
    let cancelled = false;

    while (matches.length < options.maxMatches) {
        if (isCancelled()) {
            cancelled = true;
            break;
        }

        const index = haystack.indexOf(needle, searchFrom);
        if (index < 0) {
            break;
        }

        const end = index + needle.length;
        if (!options.wholeWord || hasWordBoundaries(text, index, end)) {
            matches.push({ start: index, length: needle.length });
            searchFrom = end;
        } else {
            searchFrom = index + 1;
        }

        if (searchFrom - lastProgress >= PROGRESS_INTERVAL) {
            lastProgress = searchFrom;
            onProgress({ processed: Math.min(searchFrom, text.length), total: text.length });
        }
    }

    const processed = cancelled ? Math.min(searchFrom, text.length) : text.length;
    onProgress({ processed, total: text.length });
    return { matches, cancelled, processed };
}
