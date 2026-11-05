import * as vscode from "vscode";
import { findAddonPath, loadAddon, scanWithAddon, type AddonModule } from "./addon";
import { scanLiteral } from "./fallback";
import { planEdits } from "./plan";
import type {
    Algorithm,
    CommandArgs,
    EditSummary,
    Match,
    NativeMode,
    ProgressEvent as EngineProgress,
    ScanOptions,
    ScanResult,
    ScanSummary,
    TargetRange,
} from "./types";

const OUTPUT_LIMIT = 1000;
const UNLIMITED = Number.MAX_SAFE_INTEGER;

let outputChannel: vscode.OutputChannel | undefined;

function currentOutput(): vscode.OutputChannel {
    outputChannel ??= vscode.window.createOutputChannel("RangeEdit");
    return outputChannel;
}

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function readDefaults(): ScanOptions {
    const configuration = vscode.workspace.getConfiguration("rangeedit");
    const configuredMax = configuration.get<number>("maxMatches", 0);
    return {
        regex: configuration.get<boolean>("defaultRegex", false),
        caseSensitive: configuration.get<boolean>("defaultCaseSensitive", true),
        wholeWord: configuration.get<boolean>("defaultWholeWord", false),
        maxMatches:
            Number.isSafeInteger(configuredMax) && configuredMax > 0
                ? configuredMax
                : UNLIMITED,
        algorithm: configuration.get<Algorithm>("algorithm", "bmh"),
    };
}

function withOverrides(defaults: ScanOptions, args: CommandArgs): ScanOptions {
    const maxMatches =
        args.maxMatches === undefined
            ? defaults.maxMatches
            : Number.isSafeInteger(args.maxMatches) && args.maxMatches > 0
              ? args.maxMatches
              : UNLIMITED;
    return {
        regex: args.regex ?? defaults.regex,
        caseSensitive: args.caseSensitive ?? defaults.caseSensitive,
        wholeWord: args.wholeWord ?? defaults.wholeWord,
        maxMatches,
        algorithm: args.algorithm ?? defaults.algorithm,
    };
}

async function pickOptions(defaults: ScanOptions): Promise<ScanOptions | undefined> {
    type OptionItem = vscode.QuickPickItem & {
        key: "regex" | "caseSensitive" | "wholeWord";
    };

    const items: OptionItem[] = [
        {
            label: "Regex (RE2)",
            description: "Use the native RE2 engine",
            key: "regex",
            picked: defaults.regex,
        },
        {
            label: "Case sensitive",
            description: "Disable ASCII/Unicode case folding",
            key: "caseSensitive",
            picked: defaults.caseSensitive,
        },
        {
            label: "Whole word",
            description: "Require word boundaries",
            key: "wholeWord",
            picked: defaults.wholeWord,
        },
    ];

    const picked = await vscode.window.showQuickPick(items, {
        canPickMany: true,
        title: "RangeEdit options",
        placeHolder: "Select matching options, then press Enter",
    });
    if (!picked) {
        return undefined;
    }

    const selected = new Set(picked.map((item) => item.key));
    return {
        ...defaults,
        regex: selected.has("regex"),
        caseSensitive: selected.has("caseSensitive"),
        wholeWord: selected.has("wholeWord"),
    };
}

async function resolvePattern(args: CommandArgs): Promise<string | undefined> {
    if (args.pattern !== undefined) {
        return args.pattern;
    }
    const pattern = await vscode.window.showInputBox({
        title: "RangeEdit pattern",
        prompt: "Literal text or RE2 pattern",
        ignoreFocusOut: true,
    });
    if (pattern === undefined) {
        return undefined;
    }
    if (pattern.length === 0) {
        void vscode.window.showWarningMessage("RangeEdit: pattern must not be empty.");
        return undefined;
    }
    return pattern;
}

async function resolveReplacement(args: CommandArgs): Promise<string | undefined> {
    if (args.replacement !== undefined) {
        return args.replacement;
    }
    return vscode.window.showInputBox({
        title: "RangeEdit replacement",
        prompt: "Replacement text (empty deletes matches)",
        value: "",
        ignoreFocusOut: true,
    });
}

function resolveTarget(editor: vscode.TextEditor, args: CommandArgs): TargetRange {
    const document = editor.document;
    const selection = editor.selection;
    const useSelection =
        args.scope === "selection" || (args.scope === undefined && !selection.isEmpty);
    const range = useSelection
        ? new vscode.Range(selection.start, selection.end)
        : new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length));
    return {
        text: document.getText(range),
        offset: document.offsetAt(range.start),
    };
}

function addonLookup(context: vscode.ExtensionContext): {
    extensionPath: string;
    workspaceRoots: string[];
    configuredPath: string;
} {
    return {
        extensionPath: context.extensionPath,
        workspaceRoots: (vscode.workspace.workspaceFolders ?? []).map((folder) => folder.uri.fsPath),
        configuredPath: vscode.workspace
            .getConfiguration("rangeedit")
            .get<string>("addonPath", ""),
    };
}

function resolveAddon(context: vscode.ExtensionContext): AddonModule | undefined {
    const mode = vscode.workspace
        .getConfiguration("rangeedit")
        .get<NativeMode>("nativeMode", "auto");
    if (mode === "never") {
        return undefined;
    }

    try {
        const addonPath = findAddonPath(addonLookup(context));
        if (addonPath === undefined) {
            if (mode === "always") {
                throw new Error(
                    "nativeMode is always but no rangeedit.node was found; set rangeedit.addonPath.",
                );
            }
            return undefined;
        }
        return loadAddon(addonPath);
    } catch (error) {
        if (mode === "always") {
            throw error;
        }
        return undefined;
    }
}

function progressReporter(
    progress: vscode.Progress<{ message?: string; increment?: number }>,
): (event: EngineProgress) => void {
    let lastProcessed = 0;
    return (event) => {
        const total = event.total > 0 ? event.total : 1;
        const increment = Math.max(
            0,
            Math.min(100, ((event.processed - lastProcessed) / total) * 100),
        );
        lastProcessed = event.processed;
        progress.report({
            increment,
            message: `${event.processed.toLocaleString()} / ${event.total.toLocaleString()} code units`,
        });
    };
}

async function scanText(
    context: vscode.ExtensionContext,
    text: string,
    pattern: string,
    options: ScanOptions,
    progress: vscode.Progress<{ message?: string; increment?: number }>,
    cancellation: vscode.CancellationToken,
): Promise<ScanResult> {
    const addon = resolveAddon(context);
    const mode = vscode.workspace
        .getConfiguration("rangeedit")
        .get<NativeMode>("nativeMode", "auto");
    const useAddon = addon !== undefined && (options.regex || mode === "always");

    if (useAddon) {
        return scanWithAddon(
            addon,
            text,
            pattern,
            options,
            cancellation,
            progressReporter(progress),
        );
    }

    if (options.regex) {
        throw new Error(
            "Regex matching requires the native addon. Build it with `cmake --preset addon` and set `rangeedit.addonPath` if needed.",
        );
    }

    return scanLiteral(
        text,
        pattern,
        options,
        () => cancellation.isCancellationRequested,
        progressReporter(progress),
    );
}

function writeMatches(
    document: vscode.TextDocument,
    targetOffset: number,
    matches: Match[],
): void {
    const channel = currentOutput();
    channel.clear();
    channel.appendLine(`RangeEdit: ${matches.length} match(es)`);

    const shown = matches.slice(0, OUTPUT_LIMIT);
    for (const match of shown) {
        const start = document.positionAt(targetOffset + match.start);
        channel.appendLine(
            `${start.line + 1}:${start.character + 1} ` +
                `(offset ${targetOffset + match.start}, length ${match.length})`,
        );
    }
    if (matches.length > shown.length) {
        channel.appendLine(`... ${matches.length - shown.length} more match(es)`);
    }
    channel.show(true);
}

async function runScan(
    context: vscode.ExtensionContext,
    args: CommandArgs,
): Promise<ScanSummary | undefined> {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        void vscode.window.showWarningMessage("RangeEdit: open a text editor first.");
        return undefined;
    }

    const pattern = await resolvePattern(args);
    if (pattern === undefined) {
        return undefined;
    }

    const defaults = readDefaults();
    const options =
        args.pattern !== undefined ? withOverrides(defaults, args) : await pickOptions(defaults);
    if (!options) {
        return undefined;
    }

    const target = resolveTarget(editor, args);
    const result = await vscode.window.withProgress(
        {
            location: vscode.ProgressLocation.Notification,
            title: options.regex ? "RangeEdit: regex scan" : "RangeEdit: scan",
            cancellable: true,
        },
        (progress, cancellation) =>
            scanText(context, target.text, pattern, options, progress, cancellation),
    );

    const summary: ScanSummary = {
        matches: result.matches,
        cancelled: result.cancelled,
        processed: result.processed,
        targetOffset: target.offset,
    };

    if (args.silent !== true) {
        if (result.cancelled) {
            void vscode.window.showInformationMessage(
                `RangeEdit: scan cancelled after ${result.processed.toLocaleString()} code units.`,
            );
        } else {
            writeMatches(editor.document, target.offset, result.matches);
            void vscode.window.showInformationMessage(
                `RangeEdit: found ${result.matches.length.toLocaleString()} match(es).`,
            );
        }
    }

    return summary;
}

async function runEdit(
    context: vscode.ExtensionContext,
    args: CommandArgs,
    deleteMode: boolean,
): Promise<EditSummary | undefined> {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        void vscode.window.showWarningMessage("RangeEdit: open a text editor first.");
        return undefined;
    }

    const pattern = await resolvePattern(args);
    if (pattern === undefined) {
        return undefined;
    }
    const replacement = deleteMode ? "" : await resolveReplacement(args);
    if (replacement === undefined) {
        return undefined;
    }

    const defaults = readDefaults();
    const options =
        args.pattern !== undefined ? withOverrides(defaults, args) : await pickOptions(defaults);
    if (!options) {
        return undefined;
    }
    const target = resolveTarget(editor, args);
    const result = await vscode.window.withProgress(
        {
            location: vscode.ProgressLocation.Notification,
            title: deleteMode ? "RangeEdit: delete" : "RangeEdit: replace",
            cancellable: true,
        },
        (progress, cancellation) =>
            scanText(context, target.text, pattern, options, progress, cancellation),
    );

    const plan = planEdits(target.text.length, result.matches, replacement, deleteMode);
    const summary: EditSummary = {
        matches: result.matches,
        cancelled: result.cancelled,
        processed: result.processed,
        targetOffset: target.offset,
        applied: false,
        plan,
    };

    if (result.cancelled) {
        if (args.silent !== true) {
            void vscode.window.showInformationMessage(
                `RangeEdit: cancelled after ${result.processed.toLocaleString()} code units.`,
            );
        }
        return summary;
    }

    if (result.matches.length === 0) {
        if (args.silent !== true) {
            void vscode.window.showInformationMessage("RangeEdit: no matches.");
        }
        return summary;
    }

    if (args.apply !== true) {
        const action = deleteMode ? "Delete" : "Replace";
        const answer = await vscode.window.showInformationMessage(
            `RangeEdit: ${action.toLowerCase()} ${plan.editCount.toLocaleString()} match(es)? ` +
                `${plan.removedUnits.toLocaleString()} removed, ` +
                `${plan.insertedUnits.toLocaleString()} inserted, ` +
                `${plan.outputUnits.toLocaleString()} output code units.`,
            { modal: true },
            action,
        );
        if (answer !== action) {
            return summary;
        }
    }

    const edit = new vscode.WorkspaceEdit();
    for (const match of result.matches) {
        const start = editor.document.positionAt(target.offset + match.start);
        const end = editor.document.positionAt(target.offset + match.start + match.length);
        edit.replace(editor.document.uri, new vscode.Range(start, end), replacement);
    }

    const applied = await vscode.workspace.applyEdit(edit);
    summary.applied = applied;
    if (args.silent !== true) {
        if (applied) {
            void vscode.window.showInformationMessage(
                `RangeEdit: ${deleteMode ? "deleted" : "replaced"} ` +
                    `${plan.editCount.toLocaleString()} match(es).`,
            );
        } else {
            void vscode.window.showErrorMessage("RangeEdit: the workspace edit was rejected.");
        }
    }

    return summary;
}

async function guarded<T>(action: () => Promise<T>): Promise<T | undefined> {
    try {
        return await action();
    } catch (error) {
        void vscode.window.showErrorMessage(`RangeEdit: ${errorMessage(error)}`);
        return undefined;
    }
}

export function registerCommands(context: vscode.ExtensionContext): void {
    context.subscriptions.push(
        vscode.commands.registerCommand("rangeedit.scan", (args: CommandArgs = {}) =>
            guarded(() => runScan(context, args)),
        ),
        vscode.commands.registerCommand("rangeedit.replace", (args: CommandArgs = {}) =>
            guarded(() => runEdit(context, args, false)),
        ),
        vscode.commands.registerCommand("rangeedit.delete", (args: CommandArgs = {}) =>
            guarded(() => runEdit(context, args, true)),
        ),
    );
}

export function disposeCommands(): void {
    outputChannel?.dispose();
    outputChannel = undefined;
}
