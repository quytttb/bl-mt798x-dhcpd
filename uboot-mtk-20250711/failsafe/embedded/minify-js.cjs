#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { minify } = require("terser");

function fail(message) {
    console.error(`[failsafe-minify] ${message}`);
    process.exit(1);
}

const [inputPath, outputPath] = process.argv.slice(2);

if (!inputPath || !outputPath) {
    fail("usage: node minify-js.cjs <input.js> <output.js>");
}

let source;
try {
    source = fs.readFileSync(inputPath, "utf8");
} catch (error) {
    fail(`read failed for ${inputPath}: ${error.message}`);
}

(async () => {
    try {
        const result = await minify(source, {
            compress: {
                passes: 2,
            },
            mangle: {
                toplevel: false,
            },
            format: {
                comments: false,
            },
        });

        if (result.error) {
            fail(`minify failed for ${inputPath}: ${result.error.message || result.error}`);
        }

        fs.mkdirSync(path.dirname(outputPath), { recursive: true });
        fs.writeFileSync(outputPath, result.code || "", "utf8");
    } catch (error) {
        fail(`minify failed for ${inputPath}: ${error.message || error}`);
    }
})();
