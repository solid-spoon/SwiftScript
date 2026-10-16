(function () {
  "use strict";

  function fmt(x) {
    if (x === null || x === undefined) return "nil";
    if (Array.isArray(x)) return "[" + x.map(fmt).join(", ") + "]";
    if (typeof x === "object") { try { return JSON.stringify(x); } catch { return String(x); } }
    return String(x);
  }

  function runInSandbox(js) {
    const out = [];
    const fake = {
      log: (...a) => out.push({ kind: "out", text: a.map(fmt).join(" ") }),
      error: (...a) => out.push({ kind: "err", text: "err: " + a.map(fmt).join(" ") }),
      warn: (...a) => out.push({ kind: "warn", text: "warn: " + a.map(fmt).join(" ") }),
    };
    try { new Function("console", js)(fake); }
    catch (e) { out.push({ kind: "err", text: "✖ " + (e && e.message ? e.message : String(e)) }); }
    return out;
  }

  window.SwiftScript = window.SwiftScript || {};
  window.SwiftScript.runInSandbox = runInSandbox;
})();