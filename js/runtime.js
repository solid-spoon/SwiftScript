(function () {
  "use strict";

  const RUNTIME = String.raw`
const _force = (x) => { if (x === null || x === undefined) throw new Error("Force unwrap of nil"); return x; };
const print = (...args) => console.log(...args);
const String = (x) => {
  if (x === null || x === undefined) return "nil";
  if (Array.isArray(x)) return "[" + x.map(String).join(", ") + "]";
  if (typeof x === "object") {
    const pairs = Object.entries(x).map(([k, v]) => k + ": " + String(v)).join(", ");
    return "{" + pairs + "}";
  }
  return "" + x;
};
const Int = (x) => { const n = parseInt(x, 10); return Number.isNaN(n) ? null : n; };
const Double = (x) => { const n = parseFloat(x); return Number.isNaN(n) ? null : n; };
const Bool = (x) => Boolean(x);
const abs = Math.abs, min = Math.min, max = Math.max;
const round = Math.round, floor = Math.floor, ceil = Math.ceil;
const _range   = (a, b) => { const r = []; for (let i = a; i <  b; i++) r.push(i); return r; };
const _rangeIn = (a, b) => { const r = []; for (let i = a; i <= b; i++) r.push(i); return r; };
`;

  window.SwiftScript = window.SwiftScript || {};
  window.SwiftScript.RUNTIME = RUNTIME;
})();