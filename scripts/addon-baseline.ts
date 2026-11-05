import { readFileSync, statSync } from "node:fs";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import process from "node:process";

const require = createRequire(import.meta.url);

interface Options {
    addon: string;
    file: string;
    pattern: string;
    runs: number;
    json: boolean;
}

interface ScanSample {
    matches: number;
    medianMs: number;
    mibPerSecond: number;
}

interface CancellationSample {
    cancelled: number;
    medianLatencyMs: number;
    medianProcessed: number;
    runs: number;
}

function usage(): never {
    console.error(
        "Usage: node --experimental-strip-types scripts/addon-baseline.ts --file FILE --pattern TEXT [options]\n" +
            "\n" +
            "Options:\n" +
            "  --addon PATH   Path to the built addon (default: build/addon/rangeedit.node)\n" +
            "  --runs N       Timed scans (default: 5)\n" +
            "  --json         Emit machine-readable JSON",
    );
    process.exit(2);
}

function parseCount(text: string): number {
    const value = Number(text);
    if (!Number.isSafeInteger(value) || value < 0) {
        usage();
    }
    return value;
}

function parseOptions(argv: string[]): Options {
    const options: Options = {
        addon: "build/addon/rangeedit.node",
        file: "",
        pattern: "",
        runs: 5,
        json: false,
    };

    for (let i = 0; i < argv.length; ++i) {
        const argument = argv[i];
        const value = (): string => {
            if (i + 1 >= argv.length) {
                usage();
            }
            return argv[++i];
        };

        switch (argument) {
            case "--addon":
                options.addon = value();
                break;
            case "--file":
                options.file = value();
                break;
            case "--pattern":
                options.pattern = value();
                break;
            case "--runs":
                options.runs = parseCount(value());
                break;
            case "--json":
                options.json = true;
                break;
            default:
                usage();
        }
    }

    if (options.file.length === 0 || options.pattern.length === 0) {
        usage();
    }
    if (options.runs === 0) {
        console.error("runs must be greater than zero");
        process.exit(2);
    }

    return options;
}

function median(values: number[]): number {
    const sorted = [...values].sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    if (sorted.length % 2 === 0) {
        return (sorted[middle - 1] + sorted[middle]) / 2;
    }
    return sorted[middle];
}

function scanJs(text: string, pattern: string, maxMatches: number): number {
    if (pattern.length === 0 || maxMatches === 0) {
        return 0;
    }

    let count = 0;
    let position = 0;

    while (count < maxMatches) {
        const index = text.indexOf(pattern, position);
        if (index < 0) {
            break;
        }
        ++count;
        position = index + pattern.length;
    }

    return count;
}

function measureJs(text: string, options: Options, bytes: number): {
    first: ScanSample;
    scan: ScanSample;
} {
    let sink = scanJs(text, options.pattern, Number.MAX_SAFE_INTEGER);

    const firstSamples: number[] = [];
    const samples: number[] = [];
    for (let run = 0; run < options.runs; ++run) {
        let started = process.hrtime.bigint();
        sink += scanJs(text, options.pattern, 1);
        firstSamples.push(Number(process.hrtime.bigint() - started) / 1e6);

        started = process.hrtime.bigint();
        sink += scanJs(text, options.pattern, Number.MAX_SAFE_INTEGER);
        samples.push(Number(process.hrtime.bigint() - started) / 1e6);
    }

    if (sink < 0) {
        throw new Error("unreachable");
    }

    const mib = bytes / (1024 * 1024);
    const matches = scanJs(text, options.pattern, Number.MAX_SAFE_INTEGER);
    return {
        first: {
            matches: scanJs(text, options.pattern, 1),
            medianMs: median(firstSamples),
            mibPerSecond: mib / (median(firstSamples) / 1000),
        },
        scan: {
            matches,
            medianMs: median(samples),
            mibPerSecond: mib / (median(samples) / 1000),
        },
    };
}

async function measureAddon(
    addon: {
        createCancelToken: () => { cancel: () => void };
        scan: (
            text: string,
            pattern: string,
            options?: {
                cancelToken?: unknown;
                maxMatches?: number;
                onProgress?: (progress: { processed: number; total: number }) => void;
            },
        ) => Promise<{ matches: { length: number }[]; cancelled: boolean; processed: number }>;
    },
    text: string,
    options: Options,
    bytes: number,
): Promise<{ cancellation: CancellationSample; first: ScanSample; scan: ScanSample }> {
    let sink = (await addon.scan(text, options.pattern)).matches.length;
    await addon.scan(text, options.pattern, { maxMatches: 1 });

    const firstSamples: number[] = [];
    const samples: number[] = [];
    for (let run = 0; run < options.runs; ++run) {
        let started = process.hrtime.bigint();
        const first = await addon.scan(text, options.pattern, { maxMatches: 1 });
        firstSamples.push(Number(process.hrtime.bigint() - started) / 1e6);
        sink += first.matches.length;

        started = process.hrtime.bigint();
        const result = await addon.scan(text, options.pattern);
        samples.push(Number(process.hrtime.bigint() - started) / 1e6);
        sink += result.matches.length;
    }

    if (sink < 0) {
        throw new Error("unreachable");
    }

    const latencies: number[] = [];
    const processedSamples: number[] = [];
    let cancelled = 0;
    const absent = "__rangeedit_absent_pattern__";
    for (let run = 0; run < options.runs; ++run) {
        const token = addon.createCancelToken();
        let cancelAt = 0n;
        const result = await addon.scan(text, absent, {
            cancelToken: token,
            onProgress: (progress) => {
                if (cancelAt === 0n && progress.processed >= 1024 * 1024) {
                    cancelAt = process.hrtime.bigint();
                    token.cancel();
                }
            },
        });
        if (result.cancelled) {
            ++cancelled;
        }
        if (cancelAt !== 0n) {
            latencies.push(Number(process.hrtime.bigint() - cancelAt) / 1e6);
            processedSamples.push(result.processed);
        }
    }

    const mib = bytes / (1024 * 1024);
    return {
        first: {
            matches: (await addon.scan(text, options.pattern, { maxMatches: 1 })).matches.length,
            medianMs: median(firstSamples),
            mibPerSecond: mib / (median(firstSamples) / 1000),
        },
        scan: {
            matches: (await addon.scan(text, options.pattern)).matches.length,
            medianMs: median(samples),
            mibPerSecond: mib / (median(samples) / 1000),
        },
        cancellation: {
            cancelled,
            medianLatencyMs: latencies.length > 0 ? median(latencies) : 0,
            medianProcessed: processedSamples.length > 0 ? median(processedSamples) : 0,
            runs: options.runs,
        },
    };
}

const options = parseOptions(process.argv.slice(2));
const addonPath = resolve(options.addon);
const addon = require(addonPath);
const bytes = statSync(options.file).size;
const text = readFileSync(options.file, "utf8");

const js = measureJs(text, options, bytes);
const native = await measureAddon(addon, text, options, bytes);

if (options.json) {
    console.log(
        JSON.stringify({
            addon: addonPath,
            file: resolve(options.file),
            pattern: options.pattern,
            bytes,
            runs: options.runs,
            node: js,
            addon: native,
        }),
    );
} else {
    console.log(`file: ${resolve(options.file)} (${bytes} bytes)`);
    console.log(`pattern: ${JSON.stringify(options.pattern)}, runs: ${options.runs}`);
    console.log("");
    console.log(`node scan:  ${js.scan.medianMs.toFixed(3)} ms, ${js.scan.mibPerSecond.toFixed(1)} MiB/s`);
    console.log(`addon scan: ${native.scan.medianMs.toFixed(3)} ms, ${native.scan.mibPerSecond.toFixed(1)} MiB/s`);
    console.log(`node first:  ${js.first.medianMs.toFixed(3)} ms`);
    console.log(`addon first: ${native.first.medianMs.toFixed(3)} ms`);
    console.log(
        `addon cancel: ${native.cancellation.medianLatencyMs.toFixed(3)} ms ` +
            `(${native.cancellation.cancelled}/${native.cancellation.runs} cancelled, ` +
            `processed ${native.cancellation.medianProcessed})`,
    );
}
