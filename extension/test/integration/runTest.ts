import { resolve } from "node:path";
import { runTests } from "@vscode/test-electron";

async function main(): Promise<void> {
    const extensionDevelopmentPath = resolve(__dirname, "../../..");
    const extensionTestsPath = resolve(__dirname, "./suite/index");
    const addonPath = resolve(extensionDevelopmentPath, "../build/addon/rangeedit.node");

    await runTests({
        extensionDevelopmentPath,
        extensionTestsPath,
        extensionTestsEnv: {
            RANGEEDIT_ADDON: addonPath,
        },
        launchArgs: ["--disable-extensions"],
    });
}

main().catch((error: unknown) => {
    console.error(error);
    process.exit(1);
});
