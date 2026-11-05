import { execFileSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const extensionDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = resolve(extensionDir, "..");
const currentTarget = `${process.platform}-${process.arch}`;
const target = process.argv[2] ?? currentTarget;
const supportedTargets = new Set([
    "darwin-arm64",
    "darwin-x64",
    "linux-arm64",
    "linux-x64",
    "win32-arm64",
    "win32-x64",
]);

if (!supportedTargets.has(target)) {
    throw new Error(`unsupported VSIX target: ${target}`);
}
if (target !== currentTarget) {
    throw new Error(
        `cannot package ${target} from ${currentTarget}; build on a matching runner`,
    );
}

const configuredBinary = process.env.RANGEEDIT_ADDON_BINARY;
const addonSource = configuredBinary
    ? resolve(configuredBinary)
    : resolve(repoRoot, "build/addon/rangeedit.node");

if (!existsSync(addonSource)) {
    console.log(`addon not found at ${addonSource}; building it with CMake`);
    execFileSync("cmake", ["--preset", "addon"], { cwd: repoRoot, stdio: "inherit" });
    execFileSync("cmake", ["--build", "--preset", "addon", "--parallel"], {
        cwd: repoRoot,
        stdio: "inherit",
    });
}
if (!existsSync(addonSource)) {
    throw new Error(`addon binary not found: ${addonSource}`);
}

const nativeDir = join(extensionDir, "native");
const nativePath = join(nativeDir, "rangeedit.node");
mkdirSync(nativeDir, { recursive: true });
copyFileSync(addonSource, nativePath);

const require = createRequire(import.meta.url);
const addon = require(nativePath);
if (
    typeof addon.scan !== "function" ||
    typeof addon.createCancelToken !== "function"
) {
    throw new Error(`${nativePath} does not export the RangeEdit addon API`);
}

const manifest = JSON.parse(readFileSync(join(extensionDir, "package.json"), "utf8"));
const distDir = join(extensionDir, "dist");
mkdirSync(distDir, { recursive: true });
const output = join(distDir, `rangeedit-${manifest.version}-${target}.vsix`);
const vsce = join(
    extensionDir,
    "node_modules",
    ".bin",
    process.platform === "win32" ? "vsce.cmd" : "vsce",
);

execFileSync(
    vsce,
    ["package", "--target", target, "--out", output, "--allow-missing-repository"],
    { cwd: extensionDir, stdio: "inherit" },
);

console.log(`wrote ${output}`);
