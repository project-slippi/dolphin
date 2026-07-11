#!/usr/bin/env node
// Runs the Emscripten dolphin-web build headless under node.
// usage: node run-web-smoke.mjs <path/to/dolphin-web.js> <game on host fs> [frames]
//
// The build's pre.js mounts the host filesystem at /host under node, and
// classic emscripten node builds take argv from process.argv, so this is just
// a spawn wrapper that fixes up paths and the working directory.
import { spawnSync } from "node:child_process";
import path from "node:path";

const [, , jsPath, gamePath, frames = "600"] = process.argv;
if (!jsPath || !gamePath) {
  console.error("usage: run-web-smoke.mjs <dolphin-web.js> <game> [frames]");
  process.exit(2);
}

const jsAbs = path.resolve(jsPath);
const gameAbs = path.resolve(gamePath);

const result = spawnSync(
  process.execPath,
  [jsAbs, "--exec", `/host${gameAbs}`, "--frames", String(frames)],
  // cwd: the preload .data package is resolved relative to CWD in node.
  { cwd: path.dirname(jsAbs), stdio: "inherit" },
);
process.exit(result.status ?? 1);
