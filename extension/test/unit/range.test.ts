import { equal } from "node:assert/strict";
import { test } from "node:test";
import { validateLineRange } from "../../src/range";

test("accepts valid line ranges", () => {
    equal(validateLineRange(5, 1, 3), undefined);
    equal(validateLineRange(5, 2, 2), undefined);
    equal(validateLineRange(5, 1, 5), undefined);
});

test("requires both endpoints", () => {
    equal(validateLineRange(5, undefined, 3), "startLine and endLine are required");
    equal(validateLineRange(5, 1, undefined), "startLine and endLine are required");
});

test("rejects non-integer line numbers", () => {
    equal(validateLineRange(5, 1.5, 3), "line numbers must be whole numbers");
    equal(validateLineRange(5, 1, Number.NaN), "line numbers must be whole numbers");
    equal(
        validateLineRange(5, 1, Number.POSITIVE_INFINITY),
        "line numbers must be whole numbers",
    );
});

test("rejects line numbers below one", () => {
    equal(validateLineRange(5, 0, 3), "line numbers must be 1 or greater");
    equal(validateLineRange(5, 1, -1), "line numbers must be 1 or greater");
});

test("rejects end lines past the document", () => {
    equal(validateLineRange(5, 1, 6), "endLine must be between 1 and 5");
});

test("rejects reversed ranges", () => {
    equal(validateLineRange(5, 4, 2), "startLine must not be greater than endLine");
});

test("rejects documents without lines", () => {
    equal(validateLineRange(0, 1, 1), "document has no lines");
});
