import { deepEqual, equal, throws } from "node:assert/strict";
import { test } from "node:test";
import { applyEdits, planEdits } from "../../src/plan";

test("plans replacement stats in code units", () => {
    const plan = planEdits(
        20,
        [
            { start: 2, length: 3 },
            { start: 10, length: 4 },
        ],
        "xy",
        false,
    );
    deepEqual(plan, {
        editCount: 2,
        removedUnits: 7,
        insertedUnits: 4,
        outputUnits: 17,
    });
});

test("delete plans insert nothing", () => {
    const plan = planEdits(10, [{ start: 1, length: 3 }], "ignored", true);
    deepEqual(plan, {
        editCount: 1,
        removedUnits: 3,
        insertedUnits: 0,
        outputUnits: 7,
    });
});

test("applies replacements and deletions", () => {
    equal(
        applyEdits(
            "one two three",
            [
                { start: 0, length: 3 },
                { start: 8, length: 5 },
            ],
            "X",
            false,
        ),
        "X two X",
    );
    equal(applyEdits("one two", [{ start: 3, length: 4 }], "", true), "one");
});

test("applies zero-length insertions", () => {
    equal(applyEdits("ab", [{ start: 1, length: 0 }], "-", false), "a-b");
});

test("rejects out-of-bounds matches", () => {
    throws(() => planEdits(3, [{ start: 2, length: 2 }], "", false), /out of bounds/);
});

test("rejects overlapping and duplicate zero-length matches", () => {
    throws(
        () =>
            planEdits(
                10,
                [
                    { start: 1, length: 3 },
                    { start: 3, length: 2 },
                ],
                "",
                false,
            ),
        /non-overlapping/,
    );
    throws(
        () =>
            planEdits(
                10,
                [
                    { start: 1, length: 0 },
                    { start: 1, length: 0 },
                ],
                "",
                false,
            ),
        /non-overlapping/,
    );
});
