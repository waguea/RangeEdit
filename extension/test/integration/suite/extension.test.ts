import { deepStrictEqual, ok, strictEqual } from "node:assert";
import * as vscode from "vscode";
import type { EditSummary, ScanSummary } from "../../../src/types";

async function openDocument(content: string): Promise<vscode.TextDocument> {
    const document = await vscode.workspace.openTextDocument({
        content,
        language: "plaintext",
    });
    await vscode.window.showTextDocument(document);
    return document;
}

async function executeScan(pattern: string, extra: object = {}): Promise<ScanSummary> {
    const result = await vscode.commands.executeCommand<ScanSummary | undefined>(
        "rangeedit.scan",
        { pattern, silent: true, scope: "document", ...extra },
    );
    ok(result, "scan command returned a result");
    return result;
}

async function executeEdit(
    command: "rangeedit.replace" | "rangeedit.delete",
    pattern: string,
    replacement: string | undefined,
    extra: object = {},
): Promise<EditSummary> {
    const result = await vscode.commands.executeCommand<EditSummary | undefined>(command, {
        pattern,
        replacement,
        apply: true,
        silent: true,
        scope: "document",
        ...extra,
    });
    ok(result, `${command} returned a result`);
    return result;
}

async function executeScanLines(
    pattern: string,
    startLine: number,
    endLine: number,
): Promise<ScanSummary> {
    const result = await vscode.commands.executeCommand<ScanSummary | undefined>(
        "rangeedit.scanLines",
        { pattern, silent: true, scope: "lines", startLine, endLine },
    );
    ok(result, "scanLines command returned a result");
    return result;
}

async function executeEditLines(
    command: "rangeedit.replaceLines" | "rangeedit.deleteLines",
    pattern: string,
    replacement: string | undefined,
    startLine: number,
    endLine: number,
    extra: object = {},
): Promise<EditSummary> {
    const result = await vscode.commands.executeCommand<EditSummary | undefined>(command, {
        pattern,
        replacement,
        startLine,
        endLine,
        apply: true,
        silent: true,
        scope: "lines",
        ...extra,
    });
    ok(result, `${command} returned a result`);
    return result;
}

suite("RangeEdit extension", () => {
    test("activates and registers commands", async () => {
        const extension = vscode.extensions.getExtension("rangeedit.rangeedit-vscode");
        ok(extension, "extension is loaded");
        await extension.activate();

        const commands = await vscode.commands.getCommands(true);
        ok(commands.includes("rangeedit.scan"));
        ok(commands.includes("rangeedit.replace"));
        ok(commands.includes("rangeedit.delete"));
        ok(commands.includes("rangeedit.scanLines"));
        ok(commands.includes("rangeedit.replaceLines"));
        ok(commands.includes("rangeedit.deleteLines"));
    });

    test("scans literal text with the fallback by default", async () => {
        const document = await openDocument("alpha needle beta needle");
        const result = await executeScan("needle");

        strictEqual(result.cancelled, false);
        deepStrictEqual(result.matches, [
            { start: 6, length: 6 },
            { start: 18, length: 6 },
        ]);
        strictEqual(result.targetOffset, 0);
        strictEqual(document.getText(), "alpha needle beta needle");
    });

    test("replaces all matches through WorkspaceEdit", async () => {
        const document = await openDocument("alpha needle beta needle");
        const result = await executeEdit("rangeedit.replace", "needle", "thread");

        strictEqual(result.applied, true);
        strictEqual(result.plan.editCount, 2);
        strictEqual(document.getText(), "alpha thread beta thread");
    });

    test("deletes matches and updates stats", async () => {
        const document = await openDocument("one needle two");
        const result = await executeEdit("rangeedit.delete", "needle", undefined);

        strictEqual(result.applied, true);
        strictEqual(result.plan.removedUnits, 6);
        strictEqual(result.plan.insertedUnits, 0);
        strictEqual(document.getText(), "one  two");
    });

    test("uses RE2 for regex scans", async () => {
        await openDocument("needle beta needle");
        const result = await executeScan("needle|beta", { regex: true });

        deepStrictEqual(result.matches, [
            { start: 0, length: 6 },
            { start: 7, length: 4 },
            { start: 12, length: 6 },
        ]);
    });

    test("replaces regex matches through the addon", async () => {
        const document = await openDocument("a1b22c");
        const result = await executeEdit("rangeedit.replace", "\\d+", "#", { regex: true });

        strictEqual(result.applied, true);
        strictEqual(result.plan.editCount, 2);
        strictEqual(document.getText(), "a#b#c");
    });

    test("limits edits to the selection", async () => {
        const document = await openDocument("needle needle needle");
        const editor = vscode.window.activeTextEditor;
        ok(editor);
        editor.selection = new vscode.Selection(0, 0, 0, 13);

        const result = await executeEdit("rangeedit.replace", "needle", "X", {
            scope: "selection",
        });

        strictEqual(result.applied, true);
        strictEqual(result.matches.length, 2);
        strictEqual(document.getText(), "X X needle");
    });

    test("scans only the requested line range", async () => {
        await openDocument("needle\nneedle\nneedle\nneedle");
        const result = await executeScanLines("needle", 2, 3);

        strictEqual(result.targetOffset, 7);
        deepStrictEqual(result.matches, [
            { start: 0, length: 6 },
            { start: 7, length: 6 },
        ]);
    });

    test("replaces only within the requested line range", async () => {
        const document = await openDocument("needle\nneedle\nneedle\nneedle");
        const result = await executeEditLines(
            "rangeedit.replaceLines",
            "needle",
            "X",
            2,
            3,
        );

        strictEqual(result.applied, true);
        strictEqual(result.plan.editCount, 2);
        strictEqual(document.getText(), "needle\nX\nX\nneedle");
    });

    test("deletes only within the requested line range", async () => {
        const document = await openDocument("needle\nneedle\nneedle");
        const result = await executeEditLines(
            "rangeedit.deleteLines",
            "needle",
            undefined,
            2,
            2,
        );

        strictEqual(result.applied, true);
        strictEqual(result.plan.removedUnits, 6);
        strictEqual(document.getText(), "needle\n\nneedle");
    });

    test("includes the end line and excludes the next line", async () => {
        const document = await openDocument("a needle\nneedle b\nneedle");
        await executeEditLines("rangeedit.replaceLines", "needle", "X", 1, 2);

        strictEqual(document.getText(), "a X\nX b\nneedle");
    });

    test("supports a range on the last line without a trailing newline", async () => {
        const document = await openDocument("needle\nneedle");
        const result = await executeEditLines(
            "rangeedit.replaceLines",
            "needle",
            "X",
            2,
            2,
        );

        strictEqual(result.applied, true);
        strictEqual(document.getText(), "needle\nX");
    });

    test("line range overrides an existing selection", async () => {
        const document = await openDocument("needle\nneedle\nneedle");
        const editor = vscode.window.activeTextEditor;
        ok(editor);
        editor.selection = new vscode.Selection(0, 0, 2, 6);

        await executeEditLines("rangeedit.replaceLines", "needle", "X", 2, 2);

        strictEqual(document.getText(), "needle\nX\nneedle");
    });

    test("uses RE2 for line-range replacements", async () => {
        const document = await openDocument("a1\nb22\nc333");
        const result = await executeEditLines(
            "rangeedit.replaceLines",
            "\\d+",
            "#",
            1,
            2,
            { regex: true },
        );

        strictEqual(result.applied, true);
        strictEqual(document.getText(), "a#\nb#\nc333");
    });

    test("rejects invalid line ranges", async () => {
        await openDocument("a\nb\nc");
        const result = await vscode.commands.executeCommand<EditSummary | undefined>(
            "rangeedit.replaceLines",
            {
                pattern: "a",
                replacement: "x",
                startLine: 2,
                endLine: 1,
                apply: true,
                silent: true,
            },
        );

        strictEqual(result, undefined);
    });

    test("can force literal line-range edits through the native addon", async () => {
        const configuration = vscode.workspace.getConfiguration("rangeedit");
        await configuration.update("nativeMode", "always", vscode.ConfigurationTarget.Global);
        try {
            const document = await openDocument("cat\ncat\ncat");
            await executeEditLines("rangeedit.replaceLines", "cat", "dog", 1, 2);
            strictEqual(document.getText(), "dog\ndog\ncat");
        } finally {
            await configuration.update("nativeMode", "auto", vscode.ConfigurationTarget.Global);
        }
    });

    test("can force literal scans through the native addon", async () => {
        const configuration = vscode.workspace.getConfiguration("rangeedit");
        await configuration.update("nativeMode", "always", vscode.ConfigurationTarget.Global);
        try {
            await openDocument("cat cat");
            const result = await executeScan("cat");
            deepStrictEqual(result.matches, [
                { start: 0, length: 3 },
                { start: 4, length: 3 },
            ]);
        } finally {
            await configuration.update("nativeMode", "auto", vscode.ConfigurationTarget.Global);
        }
    });

    test("uses the TypeScript fallback when nativeMode is never", async () => {
        const configuration = vscode.workspace.getConfiguration("rangeedit");
        await configuration.update("nativeMode", "never", vscode.ConfigurationTarget.Global);
        try {
            await openDocument("cat cat scatter");
            const result = await executeScan("cat", { wholeWord: true });
            deepStrictEqual(result.matches, [
                { start: 0, length: 3 },
                { start: 4, length: 3 },
            ]);
        } finally {
            await configuration.update("nativeMode", "auto", vscode.ConfigurationTarget.Global);
        }
    });
});
