import { deepEqual, equal } from "node:assert/strict";
import { test } from "node:test";
import { scanLiteral } from "../../src/fallback";
import type { ScanOptions } from "../../src/types";

const BASE: ScanOptions = {
    regex: false,
    caseSensitive: true,
    wholeWord: false,
    maxMatches: Number.MAX_SAFE_INTEGER,
    algorithm: "bmh",
};

test("finds non-overlapping literal matches", () => {
    const result = scanLiteral("aaaa", "aa", BASE);
    deepEqual(result.matches, [
        { start: 0, length: 2 },
        { start: 2, length: 2 },
    ]);
    equal(result.cancelled, false);
    equal(result.processed, 4);
});

test("honors maxMatches", () => {
    const result = scanLiteral("cat cat cat", "cat", { ...BASE, maxMatches: 2 });
    deepEqual(result.matches, [
        { start: 0, length: 3 },
        { start: 4, length: 3 },
    ]);
});

test("empty patterns and zero caps produce no matches", () => {
    deepEqual(scanLiteral("cat", "", BASE).matches, []);
    deepEqual(scanLiteral("cat", "cat", { ...BASE, maxMatches: 0 }).matches, []);
});

test("case folding is ASCII-only", () => {
    const result = scanLiteral("Ä cat CAT", "cat", { ...BASE, caseSensitive: false });
    deepEqual(result.matches, [
        { start: 2, length: 3 },
        { start: 6, length: 3 },
    ]);
});

test("whole-word mode rejects embedded matches", () => {
    const result = scanLiteral("cat scatter cat", "cat", {
        ...BASE,
        wholeWord: true,
    });
    deepEqual(result.matches, [
        { start: 0, length: 3 },
        { start: 12, length: 3 },
    ]);
});

test("reports UTF-16 code-unit offsets", () => {
    const result = scanLiteral("😀 needle", "needle", BASE);
    deepEqual(result.matches, [{ start: 3, length: 6 }]);
});

test("cancellation returns partial matches", () => {
    let checks = 0;
    const result = scanLiteral(
        "cat cat",
        "cat",
        BASE,
        () => checks++ >= 2,
    );
    equal(result.cancelled, true);
    deepEqual(result.matches, [{ start: 0, length: 3 }]);
    equal(result.processed, 3);
});

test("reports progress at the end of a scan", () => {
    const events: { processed: number; total: number }[] = [];
    scanLiteral("cat", "cat", BASE, () => false, (event) => events.push(event));
    deepEqual(events, [{ processed: 3, total: 3 }]);
});
