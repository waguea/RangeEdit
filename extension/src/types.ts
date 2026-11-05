export type Algorithm = "naive" | "bmh" | "kmp" | "std";

export type NativeMode = "auto" | "always" | "never";

export interface Match {
    start: number;
    length: number;
}

export interface ScanOptions {
    regex: boolean;
    caseSensitive: boolean;
    wholeWord: boolean;
    maxMatches: number;
    algorithm: Algorithm;
}

export interface ScanResult {
    matches: Match[];
    cancelled: boolean;
    processed: number;
}

export interface ProgressEvent {
    processed: number;
    total: number;
}

export interface EditPlan {
    editCount: number;
    removedUnits: number;
    insertedUnits: number;
    outputUnits: number;
}

export interface TargetRange {
    text: string;
    offset: number;
}

export interface CommandArgs {
    pattern?: string;
    replacement?: string;
    regex?: boolean;
    caseSensitive?: boolean;
    wholeWord?: boolean;
    maxMatches?: number;
    algorithm?: Algorithm;
    apply?: boolean;
    silent?: boolean;
    scope?: "document" | "selection" | "lines";
    startLine?: number;
    endLine?: number;
}

export interface ScanSummary {
    matches: Match[];
    cancelled: boolean;
    processed: number;
    targetOffset: number;
}

export interface EditSummary extends ScanSummary {
    applied: boolean;
    plan: EditPlan;
}
