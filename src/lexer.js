(function () {
  "use strict";

  const KEYWORDS = new Set([
    "let","var","func","return","if","else","for","in","while","break","continue",
    "struct","enum","class","protocol","extension","import","true","false","nil",
    "self","init","guard","defer","throws","try","catch","switch","case","default",
    "where","inout","async","await","public","private","internal","static","lazy",
  ]);

  class SSError extends Error {
    constructor(msg, line, col) {
      super(msg);
      this.name = "SSError";
      this.line = line; this.col = col;
    }
  }

  function tokenize(src) {
    const toks = [];
    let i = 0, line = 1, col = 1;

    const peek = (n = 0) => src[i + n];
    const adv = (n = 1) => {
      for (let k = 0; k < n; k++) {
        if (src[i] === "\n") { line++; col = 1; } else col++;
        i++;
      }
    };
    const push = (t, v, sl, sc) => toks.push({ t, v, line: sl, col: sc });

    while (i < src.length) {
      const c = src[i], sl = line, sc = col;

      if (/\s/.test(c)) { adv(); continue; }
      if (c === "/" && peek(1) === "/") { while (i < src.length && src[i] !== "\n") adv(); continue; }
      if (c === "/" && peek(1) === "*") {
        adv(2); while (i < src.length && !(src[i] === "*" && peek(1) === "/")) adv();
        if (i < src.length) adv(2);
        continue;
      }

      if (/\d/.test(c)) {
        const s = i;
        while (i < src.length && /[\d_]/.test(src[i])) adv();
        if (src[i] === "." && /\d/.test(peek(1))) { adv(); while (i < src.length && /[\d_]/.test(src[i])) adv(); }
        push("num", Number(src.slice(s, i).replace(/_/g, "")), sl, sc);
        continue;
      }

      if (c === '"' || c === "'") {
        const q = c;
        adv();
        let buf = "";
        while (i < src.length && src[i] !== q) {
          if (src[i] === "\\" && i + 1 < src.length) { buf += src[i] + src[i + 1]; adv(2); }
          else { buf += src[i]; adv(); }
        }
        if (i >= src.length) throw new SSError("Незакрытая строка", sl, sc);
        adv();
        push("str", buf, sl, sc);
        continue;
      }

      if (/[A-Za-z_$]/.test(c)) {
        const s = i;
        while (i < src.length && /[A-Za-z0-9_$]/.test(src[i])) adv();
        const w = src.slice(s, i);
        push(KEYWORDS.has(w) ? "kw" : "id", w, sl, sc);
        continue;
      }

      const three = src.slice(i, i + 3);
      if (three === "..." || three === "..<") { push("op", three, sl, sc); adv(3); continue; }

      const two = src.slice(i, i + 2);
      if (["->","==","!=","<=",">=","&&","||","??","?.","+=","-=","*=","/=","%="].includes(two)) {
        push("op", two, sl, sc); adv(2); continue;
      }

      if ("+-*/%<>()[]{},:.=!?@_&|".includes(c)) { push("op", c, sl, sc); adv(); continue; }

      throw new SSError(`Неизвестный символ '${c}'`, sl, sc);
    }

    push("eof", null, line, col);
    return toks;
  }

  window.SwiftScript = window.SwiftScript || {};
  window.SwiftScript.tokenize = tokenize;
  window.SwiftScript.SSError = SSError;
})();