import type * as vscode from "vscode";
import { disposeCommands, registerCommands } from "./commands";

export function activate(context: vscode.ExtensionContext): void {
    registerCommands(context);
}

export function deactivate(): void {
    disposeCommands();
}
