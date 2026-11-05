export function validateLineRange(
    lineCount: number,
    startLine: number | undefined,
    endLine: number | undefined,
): string | undefined {
    if (!Number.isSafeInteger(lineCount) || lineCount < 1) {
        return "document has no lines";
    }
    if (startLine === undefined || endLine === undefined) {
        return "startLine and endLine are required";
    }
    if (!Number.isSafeInteger(startLine) || !Number.isSafeInteger(endLine)) {
        return "line numbers must be whole numbers";
    }
    if (startLine < 1 || endLine < 1) {
        return "line numbers must be 1 or greater";
    }
    if (endLine > lineCount) {
        return `endLine must be between 1 and ${lineCount}`;
    }
    if (startLine > endLine) {
        return "startLine must not be greater than endLine";
    }
    return undefined;
}
