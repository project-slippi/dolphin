// Node-only convenience for the headless smoke/determinism runs: expose the
// host filesystem read-only-by-convention at /host so callers can pass
// --exec /host/<absolute path>. Inert in browsers.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  if (typeof process === 'object' && typeof process.versions === 'object' &&
      process.versions.node) {
    try {
      FS.mkdir('/host');
      FS.mount(NODEFS, { root: '/' }, '/host');
    } catch (e) {
      err('pre.js: NODEFS mount failed: ' + e);
    }
  }
});
