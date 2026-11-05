import { createReadStream, readFileSync, statSync } from "node:fs";
import { resolve } from "node:path";
import process from "node:process";
import { StringDecoder } from "node:string_decoder";

interface Options {
    file: string;
    pattern: string;
    runs: number;
    maxMatches: number;
    stream: boolean;
    json: boolean;
}

interface Measurement {
    mode: "whole-file" | "stream";
    file: string;
    pattern: string;
    bytes: number;
    runs: number;
    matches: number;
    medianMs: number;
    mibPerSecond: number;
}

function usage(): never {
    console.error(
        "Usage: node scripts/ts-baseline.ts --file FILE --pattern TEXT [options]\n" +
            "\n" +
            "Options:\n" +
            "  --runs N       Timed whole-file scans (default: 5)\n" +
            "  --max N        Maximum matches (default: unlimited)\n" +
            "  --stream       Scan incrementally with a bounded carry\n" +
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
        file: "",
        pattern: "",
        runs: 5,
        maxMatches: Number.MAX_SAFE_INTEGER,
        stream: false,
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
            case "--file":
                options.file = value();
                break;
            case "--pattern":
                options.pattern = value();
                break;
            case "--runs":
                options.runs = parseCount(value());
                break;
            case "--max":
                options.maxMatches = parseCount(value());
                break;
            case "--stream":
                options.stream = true;
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

function scanCount(text: string, pattern: string, maxMatches: number): number {
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

function median(values: number[]): number {
    const sorted = [...values].sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    if (sorted.length % 2 === 0) {
        return (sorted[middle - 1] + sorted[middle]) / 2;
    }
    return sorted[middle];
}

function measureWholeFile(options: Options, bytes: number): Measurement {
    const text = readFileSync(options.file, "utf8");
    const matches = scanCount(text, options.pattern, options.maxMatches);
    let sink = matches;

    const samples: number[] = [];
    for (let run = 0; run < options.runs; ++run) {
        const started = process.hrtime.bigint();
        sink += scanCount(text, options.pattern, options.maxMatches);
        const elapsed = Number(process.hrtime.bigint() - started) / 1e6;
        samples.push(elapsed);
    }

    if (sink < 0) {
        throw new Error("unreachable");
    }

    const medianMs = median(samples);
    const seconds = medianMs / 1000;
    return {
        mode: "whole-file",
        file: resolve(options.file),
        pattern: options.pattern,
        bytes,
        runs: options.runs,
        matches,
        medianMs,
        mibPerSecond: bytes / (1024 * 1024) / seconds,
    };
}

async function scanStream(options: Options): Promise<number> {
    if (options.pattern.length === 0 || options.maxMatches === 0) {
        return 0;
    }

    const patternLength = options.pattern.length;
    const carryLength = patternLength + 1;
    const decoder = new StringDecoder("utf8");

    let carry = "";
    let streamOffset = 0;
    let acceptedEnd = 0;
    let count = 0;

    for await (const chunk of createReadStream(options.file, {
        highWaterMark: 64 * 1024,
    })) {
        const text = decoder.write(chunk);
        if (text.length === 0) {
            continue;
        }

        const buffer = carry + text;
        const bufferStart = streamOffset - carry.length;
        let searchFrom = 0;

        while (count < options.maxMatches) {
            const index = buffer.indexOf(options.pattern, searchFrom);
            if (index < 0) {
                break;
            }

            const globalStart = bufferStart + index;
            const globalEnd = globalStart + patternLength;

            if (globalEnd <= streamOffset) {
                searchFrom = index + 1;
                continue;
            }
            if (globalStart < acceptedEnd) {
                searchFrom = index + patternLength;
                continue;
            }

            ++count;
            acceptedEnd = globalEnd;
            searchFrom = index + patternLength;
        }

        streamOffset += text.length;
        const keep = Math.min(carryLength, buffer.length);
        carry = buffer.slice(buffer.length - keep);
    }

    const tail = decoder.end();
    if (tail.length > 0) {
        const buffer = carry + tail;
        const bufferStart = streamOffset - carry.length;
        let searchFrom = 0;

        while (count < options.maxMatches) {
            const index = buffer.indexOf(options.pattern, searchFrom);
            if (index < 0) {
                break;
            }

            const globalStart = bufferStart + index;
            const globalEnd = globalStart + patternLength;

            if (globalEnd <= streamOffset) {
                searchFrom = index + 1;
                continue;
            }
            if (globalStart < acceptedEnd) {
                searchFrom = index + patternLength;
                continue;
            }

            ++count;
            acceptedEnd = globalEnd;
            searchFrom = index + patternLength;
        }
    }

    return count;
}

function measureStream(options: Options, bytes: number): Promise<Measurement> {
    return (async () => {
        const started = process.hrtime.bigint();
        const matches = await scanStream(options);
        const medianMs = Number(process.hrtime.bigint() - started) / 1e6;
        const seconds = medianMs / 1000;
        return {
            mode: "stream",
            file: resolve(options.file),
            pattern: options.pattern,
            bytes,
            runs: 1,
            matches,
            medianMs,
            mibPerSecond: bytes / (1024 * 1024) / seconds,
        };
    })();
}

function printMeasurement(measurement: Measurement, json: boolean): void {
    if (json) {
        console.log(JSON.stringify(measurement));
        return;
    }

    console.log(
        `${measurement.mode}: ${measurement.bytes} bytes, ` +
            `${measurement.matches} matches, ` +
            `${measurement.medianMs.toFixed(3)} ms, ` +
            `${measurement.mibPerSecond.toFixed(1)} MiB/s`,
    );
}

const options = parseOptions(process.argv.slice(2));
const bytes = statSync(options.file).size;

const measurement = options.stream
    ? await measureStream(options, bytes)
    : measureWholeFile(options, bytes);

printMeasurement(measurement, options.json);
