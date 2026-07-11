#!/usr/bin/env node
// Headless-Chromium smoke runner: serves the wasm build with COOP/COEP (the
// real browser security context — SharedArrayBuffer, pthread workers, .data
// fetch), loads Tools/web-smoke.html, and waits for the SLIPPI_WEB_* markers
// the page POSTs back to /report.
//
// usage: node run-web-smoke-chrome.mjs <path/to/dolphin-web.js> <game> [fields]
// Prints the marker lines to stdout. Exit 0 iff the run booted, produced a
// nonzero MEM1 hash, raised zero alerts, and exited cleanly.

import http from 'node:http';
import { createReadStream, existsSync, mkdtempSync, rmSync } from 'node:fs';
import { spawn, execSync } from 'node:child_process';
import path from 'node:path';
import os from 'node:os';

const [, , jsPathArg, gameArg, fieldsArg = '600'] = process.argv;
if (!jsPathArg || !gameArg) {
  console.error('usage: run-web-smoke-chrome.mjs <dolphin-web.js> <game> [fields]');
  process.exit(1);
}
const binDir = path.dirname(path.resolve(jsPathArg));
const gamePath = path.resolve(gameArg);
const htmlPath = path.join(path.dirname(new URL(import.meta.url).pathname), 'web-smoke.html');
const timeoutMs = Number(process.env.SMOKE_TIMEOUT_MS || 20 * 60 * 1000);

for (const p of [gamePath, htmlPath, path.resolve(jsPathArg)]) {
  if (!existsSync(p)) {
    console.error(`missing: ${p}`);
    process.exit(1);
  }
}

function findChrome() {
  if (process.env.CHROME_BIN) return process.env.CHROME_BIN;
  for (const c of ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']) {
    try {
      execSync(`command -v ${c}`, { stdio: 'ignore', shell: '/bin/sh' });
      return c;
    } catch {}
  }
  console.error('no chrome/chromium binary found (set CHROME_BIN)');
  process.exit(1);
}

const MIME = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
};

const markers = [];
let finish; // resolves the run promise
const done = new Promise((resolve) => (finish = resolve));

const server = http.createServer((req, res) => {
  if (req.method === 'POST' && req.url === '/report') {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      console.log(body);
      markers.push(body);
      res.end('ok');
      // emscripten_force_exit tears down the wasm/pthread runtime but not the
      // surrounding page — there is no process to exit, so no SLIPPI_WEB_EXIT
      // marker follows under a real browser (only under node). ALERTS is the
      // last marker MainWeb.cpp prints before calling force_exit, so treat it
      // as terminal here; still finish early on any of the failure markers.
      if (body.startsWith('SLIPPI_WEB_ALERTS=') ||
          body.startsWith('SLIPPI_WEB_EXIT=') ||
          body.startsWith('SLIPPI_WEB_PAGE_ERROR') ||
          body.startsWith('SLIPPI_WEB_BOOT_FAILED') ||
          body.startsWith('SLIPPI_WEB_BOOT_TIMEOUT') ||
          body.startsWith('SLIPPI_WEB_RUN_TIMEOUT')) {
        finish();
      }
    });
    return;
  }
  const url = new URL(req.url, 'http://localhost');
  let file;
  if (url.pathname === '/' || url.pathname === '/web-smoke.html') file = htmlPath;
  else if (url.pathname === '/game.dol') file = gamePath;
  else file = path.join(binDir, path.normalize(url.pathname).replace(/^\/+/, ''));
  if (!file.startsWith(binDir) && file !== htmlPath && file !== gamePath) {
    res.writeHead(403).end();
    return;
  }
  if (!existsSync(file)) {
    res.writeHead(404).end();
    return;
  }
  res.writeHead(200, {
    'Content-Type': MIME[path.extname(file)] || 'application/octet-stream',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cache-Control': 'no-store',
  });
  createReadStream(file).pipe(res);
});

await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;
const pageUrl = `http://127.0.0.1:${port}/web-smoke.html?game=game.dol&frames=${fieldsArg}`;

const profile = mkdtempSync(path.join(os.tmpdir(), 'slippi-chrome-'));
const chrome = spawn(
  findChrome(),
  [
    '--headless=new',
    '--no-sandbox',
    '--disable-gpu',
    '--disable-dev-shm-usage',
    `--user-data-dir=${profile}`,
    pageUrl,
  ],
  { stdio: ['ignore', 'ignore', 'inherit'] }
);

const timer = setTimeout(() => {
  console.error('SMOKE_TIMEOUT');
  finish();
}, timeoutMs);

await done;
clearTimeout(timer);
chrome.kill('SIGKILL');
server.close();
rmSync(profile, { recursive: true, force: true });

const hash = markers.find((m) => m.startsWith('SLIPPI_WEB_MEM1_HASH='));
const alerts = markers.find((m) => m.startsWith('SLIPPI_WEB_ALERTS='));
const exitMarker = markers.find((m) => m.startsWith('SLIPPI_WEB_EXIT='));
const ok =
  markers.includes('SLIPPI_WEB_BOOTED') &&
  hash && !hash.endsWith('=0000000000000000') &&
  alerts === 'SLIPPI_WEB_ALERTS=0' &&
  // no EXIT marker under a real browser tab (see comment above); if one
  // *does* show up it must still be clean.
  (!exitMarker || exitMarker === 'SLIPPI_WEB_EXIT=0') &&
  !markers.some((m) => m.startsWith('SLIPPI_WEB_PAGE_ERROR'));
process.exit(ok ? 0 : 1);
