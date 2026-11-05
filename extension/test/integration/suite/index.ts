import { resolve } from "node:path";
import Mocha from "mocha";

export function run(): Promise<void> {
    const mocha = new Mocha({
        ui: "tdd",
        color: true,
        timeout: 30000,
    });
    mocha.addFile(resolve(__dirname, "extension.test.js"));

    return new Promise((resolveRun, rejectRun) => {
        mocha.run((failures) => {
            if (failures > 0) {
                rejectRun(new Error(`${failures} integration test(s) failed`));
                return;
            }
            resolveRun();
        });
    });
}
