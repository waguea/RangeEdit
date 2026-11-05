import { existsSync } from "node:fs";
import { isAbsolute, resolve } from "node:path";
import type { ProgressEvent, ScanOptions, ScanResult } from "./types";

export interface AddonCancelToken {
    cancel(): void;
}

export interface AddonScanOptions {
    regex?: boolean;
    caseSensitive?: boolean;
    wholeWord?: boolean;
    maxMatches?: number;
    algorithm?: string;
    cancelToken?: AddonCancelToken;
    onProgress?: (event: ProgressEvent) => void;
}

export interface AddonModule {
    createCancelToken(): AddonCancelToken;
    scan(
        text: string,
        pattern: string,
        options?: AddonScanOptions,
    ): Promise<ScanResult>;
}

export interface AddonLookup {
    extensionPath: string;
    workspaceRoots: string[];
    configuredPath: string;
}

export interface CancellationLike {
    isCancellationRequested: boolean;
    onCancellationRequested(listener: () => void): { dispose(): void };
}

const ADDON_CANDIDATES = [
    "build/addon/rangeedit.node",
    "build-addon/rangeedit.node",
    "build/rangeedit.node",
];

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

export function findAddonPath(lookup: AddonLookup): string | undefined {
    if (process.env.RANGEEDIT_ADDON) {
        const fromEnvironment = resolve(process.env.RANGEEDIT_ADDON);
        if (existsSync(fromEnvironment)) {
            return fromEnvironment;
        }
        throw new Error(`RANGEEDIT_ADDON points at a missing file: ${fromEnvironment}`);
    }

    if (lookup.configuredPath.length > 0) {
        const base = lookup.workspaceRoots[0] ?? process.cwd();
        const configured = isAbsolute(lookup.configuredPath)
            ? lookup.configuredPath
            : resolve(base, lookup.configuredPath);
        if (existsSync(configured)) {
            return configured;
        }
        throw new Error(`rangeedit.addonPath points at a missing file: ${configured}`);
    }

    const candidates = [
        ...lookup.workspaceRoots.flatMap((root) =>
            ADDON_CANDIDATES.map((candidate) => resolve(root, candidate)),
        ),
        resolve(lookup.extensionPath, "../build/addon/rangeedit.node"),
        resolve(lookup.extensionPath, "../build-addon/rangeedit.node"),
        resolve(lookup.extensionPath, "native/rangeedit.node"),
    ];

    return candidates.find((candidate) => existsSync(candidate));
}

export function loadAddon(addonPath: string): AddonModule {
    try {
        const loaded = require(addonPath) as Partial<AddonModule>;
        if (
            typeof loaded.scan !== "function" ||
            typeof loaded.createCancelToken !== "function"
        ) {
            throw new Error("module does not export the RangeEdit addon API");
        }
        return loaded as AddonModule;
    } catch (error) {
        throw new Error(`failed to load RangeEdit addon at ${addonPath}: ${errorMessage(error)}`);
    }
}

export async function scanWithAddon(
    addon: AddonModule,
    text: string,
    pattern: string,
    options: ScanOptions,
    cancellation: CancellationLike,
    onProgress: (event: ProgressEvent) => void,
): Promise<ScanResult> {
    const token = addon.createCancelToken();
    const subscription = cancellation.onCancellationRequested(() => token.cancel());

    const addonOptions: AddonScanOptions = {
        regex: options.regex,
        caseSensitive: options.caseSensitive,
        wholeWord: options.wholeWord,
        algorithm: options.algorithm,
        cancelToken: token,
        onProgress,
    };
    if (options.maxMatches < Number.MAX_SAFE_INTEGER) {
        addonOptions.maxMatches = options.maxMatches;
    }

    try {
        return await addon.scan(text, pattern, addonOptions);
    } finally {
        subscription.dispose();
    }
}
