import { deepEqual, equal } from "node:assert/strict";
import { test } from "node:test";
import {
    scanWithAddon,
    type AddonModule,
    type AddonScanOptions,
    type CancellationLike,
} from "../../src/addon";
import type { ScanOptions } from "../../src/types";

const OPTIONS: ScanOptions = {
    regex: true,
    caseSensitive: false,
    wholeWord: true,
    maxMatches: 12,
    algorithm: "kmp",
};

function fakeCancellation(): CancellationLike & {
    fire(): void;
    disposed: boolean;
} {
    let listener: (() => void) | undefined;
    const source = {
        isCancellationRequested: false,
        disposed: false,
        onCancellationRequested(next: () => void) {
            listener = next;
            return {
                dispose() {
                    source.disposed = true;
                },
            };
        },
        fire() {
            listener?.();
        },
    };
    return source;
}

test("maps options and bridges cancellation", async () => {
    let captured: AddonScanOptions | undefined;
    let cancelled = 0;
    const addon: AddonModule = {
        createCancelToken() {
            return {
                cancel() {
                    cancelled += 1;
                },
            };
        },
        async scan(text, pattern, options) {
            captured = options;
            equal(text, "text");
            equal(pattern, "pattern");
            return { matches: [], cancelled: false, processed: 4 };
        },
    };

    const cancellation = fakeCancellation();
    const events: { processed: number; total: number }[] = [];
    const result = await scanWithAddon(
        addon,
        "text",
        "pattern",
        OPTIONS,
        cancellation,
        (event) => events.push(event),
    );

    equal(captured?.regex, true);
    equal(captured?.caseSensitive, false);
    equal(captured?.wholeWord, true);
    equal(captured?.maxMatches, 12);
    equal(captured?.algorithm, "kmp");
    equal(typeof captured?.onProgress, "function");
    deepEqual(result, { matches: [], cancelled: false, processed: 4 });
    equal(cancellation.disposed, true);

    cancellation.fire();
    equal(cancelled, 1);
    captured?.onProgress?.({ processed: 2, total: 4 });
    deepEqual(events, [{ processed: 2, total: 4 }]);
});

test("omits unlimited maxMatches", async () => {
    let captured: AddonScanOptions | undefined;
    const addon: AddonModule = {
        createCancelToken() {
            return { cancel() {} };
        },
        async scan(_text, _pattern, options) {
            captured = options;
            return { matches: [], cancelled: false, processed: 0 };
        },
    };

    await scanWithAddon(
        addon,
        "",
        "x",
        { ...OPTIONS, maxMatches: Number.MAX_SAFE_INTEGER },
        fakeCancellation(),
        () => undefined,
    );

    equal(captured?.maxMatches, undefined);
});
