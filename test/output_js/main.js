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

const Status = Object.freeze({
  ok: Object.freeze({ _case: "ok" }),
  warn: (_0) => Object.freeze({ _case: "warn", _0 }),
  error: (_0) => Object.freeze({ _case: "error", _0 }),
});
function describe(s) {
_sw1: {
  const _s1 = s;
  if (_s1 && _s1._case === "ok") {
    return "fine";
    break _sw1;
  }
  if (_s1 && _s1._case === "warn") {
    const msg = _s1._0;
    return ("warn: " + msg);
    break _sw1;
  }
  if (_s1 && _s1._case === "error") {
    const msg = _s1._0;
    return ("error: " + msg);
    break _sw1;
  }
}
}
print(describe(Status.ok));
print(describe(Status.warn("slow")));
print(describe(Status.error("boom")));
function classify(code) {
_sw2: {
  const _s2 = code;
  if (_s2 === 200) {
    return "OK";
    break _sw2;
  }
  if (_s2 === 301 || _s2 === 302) {
    return "Redirect";
    break _sw2;
  }
  if ((_s2 >= 400) && (_s2 <= 499)) {
    return "Client Error";
    break _sw2;
  }
  if ((_s2 >= 500) && (_s2 < 600)) {
    return "Server Error";
    break _sw2;
  }
  {
    return "Unknown";
  }
}
}
print(classify(200));
print(classify(302));
print(classify(404));
print(classify(503));
print(classify(100));
function Point(_args) {
  const { x, y } = _args;
  const _self = { x, y };
  _self.manhattan = function() {
    return (abs(_self.x) + abs(_self.y));
  };
  _self.scaled = function(k) {
    return Point({x: (_self.x * k), y: (_self.y * k)});
  };
  return Object.freeze(_self);
}
const p = Point({x: 3, y: 4});
print(`manhattan(p) = ${String(p.manhattan())}`);
const q = p.scaled(2);
print(`p = (${String(p.x)}, ${String(p.y)})`);
print(`q = (${String(q.x)}, ${String(q.y)})`);
const name = "World";
const greeting = `Hello, ${String(name)}!`;
print(greeting);
function fib(n) {
if ((n < 2)) { return n; }
return (fib((n - 1)) + fib((n - 2)));
}
print(`fib(10) = ${String(fib(10))}`);
const nums = [1, 2, 3, 4, 5];
const doubled = nums.map((($0) => { return ($0 * 2); }));
print(`doubled: ${String(doubled)}`);
const sum = nums.reduce((($0, $1) => { return ($0 + $1); }), 0);
print(`sum: ${String(sum)}`);
let total = 0;
for (const n of nums) { (total += n); }
print(`total: ${String(total)}`);
const nothing = null;
print((nothing ?? 42));
const squares = _range(0, 5).map((($0) => { return ($0 * $0); }));
print(`squares: ${String(squares)}`);
