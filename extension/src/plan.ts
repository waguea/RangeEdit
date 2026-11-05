import type { EditPlan, Match } from "./types";

function validateMatches(inputLength: number, matches: Match[]): void {
    let previousStart = -1;
    let previousEnd = 0;

    for (const match of matches) {
        if (
            !Number.isSafeInteger(match.start) ||
            !Number.isSafeInteger(match.length) ||
            match.start < 0 ||
            match.length < 0 ||
            match.start + match.length > inputLength
        ) {
            throw new Error("match is out of bounds");
        }
        if (match.start <= previousStart || match.start < previousEnd) {
            throw new Error("matches must be strictly increasing and non-overlapping");
        }
        previousStart = match.start;
        previousEnd = match.start + match.length;
    }
}

export function planEdits(
    inputLength: number,
    matches: Match[],
    replacement: string,
    deleteMode: boolean,
): EditPlan {
    validateMatches(inputLength, matches);

    let removedUnits = 0;
    for (const match of matches) {
        removedUnits += match.length;
    }

    const insertedPerEdit = deleteMode ? 0 : replacement.length;
    const insertedUnits = insertedPerEdit * matches.length;
    return {
        editCount: matches.length,
        removedUnits,
        insertedUnits,
        outputUnits: inputLength - removedUnits + insertedUnits,
    };
}

export function applyEdits(
    text: string,
    matches: Match[],
    replacement: string,
    deleteMode: boolean,
): string {
    planEdits(text.length, matches, replacement, deleteMode);

    const pieces: string[] = [];
    let position = 0;
    for (const match of matches) {
        pieces.push(text.slice(position, match.start));
        if (!deleteMode) {
            pieces.push(replacement);
        }
        position = match.start + match.length;
    }
    pieces.push(text.slice(position));
    return pieces.join("");
}
