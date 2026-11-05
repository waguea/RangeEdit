import { closeSync, openSync, writeSync } from "node:fs";
import { resolve } from "node:path";
import process from "node:process";

interface Options {
    output: string;
    sizeBytes: number;
    needle: string;
    needleOffset: number;
    needleSpacing: number;
    seed: number;
}

function usage(): never {
    console.error(
        "Usage: node scripts/generate-corpus.ts --output FILE --size-mib N [options]\n" +
            "\n" +
            "Options:\n" +
            "  --size-bytes N       Exact corpus size in bytes\n" +
            "  --needle TEXT        Needle inserted into the corpus (default: needle)\n" +
            "  --needle-offset N    First needle offset (default: 1000)\n" +
            "  --needle-spacing N   Bytes between needle starts (default: 4096)\n" +
            "  --seed N             xorshift32 seed (default: 0x5EED)",
    );
    process.exit(2);
}

function parseSize(text: string): number {
    const value = Number(text);
    if (!Number.isSafeInteger(value) || value < 0) {
        usage();
    }
    return value;
}

function parseOptions(argv: string[]): Options {
    const options: Options = {
        output: "",
        sizeBytes: -1,
        needle: "needle",
        needleOffset: 1000,
        needleSpacing: 4096,
        seed: 0x5eed,
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
            case "--output":
                options.output = value();
                break;
            case "--size-mib":
                options.sizeBytes = parseSize(value()) * 1024 * 1024;
                break;
            case "--size-bytes":
                options.sizeBytes = parseSize(value());
                break;
            case "--needle":
                options.needle = value();
                break;
            case "--needle-offset":
                options.needleOffset = parseSize(value());
                break;
            case "--needle-spacing":
                options.needleSpacing = parseSize(value());
                break;
            case "--seed":
                options.seed = parseSize(value());
                break;
            default:
                usage();
        }
    }

    if (options.output.length === 0 || options.sizeBytes < 0) {
        usage();
    }
    if (options.needle.length === 0) {
        console.error("needle must not be empty");
        process.exit(2);
    }
    if (options.needleSpacing <= 0) {
        console.error("needle spacing must be positive");
        process.exit(2);
    }

    return options;
}

function generate(options: Options): void {
    const output = resolve(options.output);
    const needle = Buffer.from(options.needle, "utf8");
    const chunkSize = 1024 * 1024;
    const file = openSync(output, "w");
    let state = options.seed >>> 0;
    let pending: Buffer | null = null;
    let position = 0;

    const nextByte = (): number => {
        state ^= state << 13;
        state >>>= 0;
        state ^= state >>> 17;
        state ^= state << 5;
        state >>>= 0;
        return 97 + (state % 26);
    };

    try {
        while (position < options.sizeBytes) {
            const length = Math.min(chunkSize, options.sizeBytes - position);
            const buffer = Buffer.allocUnsafe(length);

            for (let i = 0; i < length; ++i) {
                buffer[i] = nextByte();
            }

            if (pending !== null) {
                const count = Math.min(pending.length, length);
                buffer.set(pending.subarray(0, count), 0);
                pending = count === pending.length ? null : Buffer.from(pending.subarray(count));
            }

            const chunkEnd = position + length;
            let needleIndex = Math.max(
                0,
                Math.ceil((position - options.needleOffset) / options.needleSpacing),
            );

            for (;;) {
                const needleStart =
                    options.needleOffset + needleIndex * options.needleSpacing;
                if (needleStart >= chunkEnd) {
                    break;
                }

                if (needleStart + needle.length <= options.sizeBytes) {
                    const localStart = needleStart - position;
                    const available = length - localStart;
                    const count = Math.min(needle.length, available);
                    buffer.set(needle.subarray(0, count), localStart);

                    if (count < needle.length) {
                        pending = Buffer.from(needle.subarray(count));
                    }
                }

                ++needleIndex;
            }

            writeSync(file, buffer);
            position = chunkEnd;
        }
    } finally {
        closeSync(file);
    }
}

const options = parseOptions(process.argv.slice(2));
generate(options);
console.error(`wrote ${options.sizeBytes} bytes to ${resolve(options.output)}`);
