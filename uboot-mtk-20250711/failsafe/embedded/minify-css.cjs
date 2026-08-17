#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const CleanCSS = require("clean-css");

function fail(message) {
    console.error(`[failsafe-minify-css] ${message}`);
    process.exit(1);
}

const [inputPath, outputPath] = process.argv.slice(2);

if (!inputPath || !outputPath) {
    fail("usage: node minify-css.cjs <input.css> <output.css>");
}

let source;
try {
    source = fs.readFileSync(inputPath, "utf8");
} catch (error) {
    fail(`read failed for ${inputPath}: ${error.message}`);
}

const result = new CleanCSS({ level: 2, inline: false }).minify(source);

if (result.errors && result.errors.length) {
    fail(`minify failed for ${inputPath}: ${result.errors.join('; ')}`);
}

try {
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    fs.writeFileSync(outputPath, result.styles || "", "utf8");
} catch (error) {
    fail(`write failed for ${outputPath}: ${error.message}`);
}
