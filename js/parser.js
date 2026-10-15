(function () {
  "use strict";
  const { SSError } = window.SwiftScript;

  const PREC = {
    "??": 1,
    "||": 2,
    "&&": 3,
    "==": 4,
    "!=": 4,
    "<": 5,
    "<=": 5,
    ">": 5,
    ">=": 5,
    "..<": 5.5,
    "...": 5.5,
    "+": 6,
    "-": 6,
    "*": 7,
    "/": 7,
    "%": 7,
  };

  function parse(toks) {
    let pos = 0;
    const peek = (n = 0) => toks[pos + n];
    const next = () => toks[pos++];
    const check = (t, v) =>
      peek().t === t && (v === undefined || peek().v === v);
    const match = (t, v) => (check(t, v) ? (pos++, true) : false);
    const expect = (t, v) => {
      if (!check(t, v)) {
        const g = peek();
        throw new SSError(
          `Ожидалось ${t}${v ? " '" + v + "'" : ""}, получено '${g.v}'`,
          g.line,
          g.col,
        );
      }
      return next();
    };

    function parseProgram() {
      const stmts = [];
      while (!check("eof")) stmts.push(parseStmt());
      return { k: "Program", stmts };
    }

    function parseStmt() {
      if (check("kw", "let") || check("kw", "var")) return parseLet();
      if (check("kw", "func")) return parseFunc();
      if (check("kw", "struct")) return parseStruct();
      if (check("kw", "enum")) return parseEnum();
      if (check("kw", "switch")) return { k: "Expr", expr: parseSwitch() };
      if (check("kw", "return")) {
        next();
        if (check("op", "}") || check("eof") || check("op", ";")) {
          match("op", ";");
          return { k: "Return", expr: null };
        }
        const e = parseExpr(0);
        match("op", ";");
        return { k: "Return", expr: e };
      }
      if (check("kw", "if")) return { k: "Expr", expr: parseIf() };
      if (check("kw", "while")) return parseWhile();
      if (check("kw", "for")) return parseFor();
      if (check("kw", "break")) {
        next();
        match("op", ";");
        return { k: "Break" };
      }
      if (check("kw", "continue")) {
        next();
        match("op", ";");
        return { k: "Continue" };
      }
      if (check("op", "{")) return parseBlock();
      const e = parseExpr(0);
      match("op", ";");
      return { k: "Expr", expr: e };
    }

    function parseLet() {
      const kind = next().v;
      const name = expect("id").v;
      let type = null;
      if (match("op", ":")) type = parseType();
      let value = null;
      if (match("op", "=")) value = parseExpr(0);
      match("op", ";");
      return { k: "Let", kind, name, type, value };
    }

    function parseType() {
      if (match("op", "[")) {
        const inner = parseType();
        expect("op", "]");
        let t = { k: "Array", inner };
        if (match("op", "?")) t = { k: "Opt", inner: t };
        return t;
      }
      if (check("op", "(")) {
        next();
        const items = [];
        while (!check("op", ")")) {
          items.push(parseType());
          if (!match("op", ",")) break;
        }
        expect("op", ")");
        let t = { k: "Tuple", items };
        if (match("op", "?")) t = { k: "Opt", inner: t };
        return t;
      }
      const name = expect("id").v;
      let t = { k: "Named", name };
      if (match("op", "<")) {
        let depth = 1;
        while (depth > 0 && !check("eof")) {
          if (check("op", "<")) depth++;
          else if (check("op", ">")) depth--;
          if (depth > 0) next();
        }
        expect("op", ">");
      }
      if (match("op", "?")) t = { k: "Opt", inner: t };
      return t;
    }

    function parseFunc() {
      expect("kw", "func");
      const name = expect("id").v;
      expect("op", "(");
      const params = [];
      if (!check("op", ")")) {
        while (true) {
          let label = null,
            pname;
          if (check("op", "_")) {
            next();
            pname = expect("id").v;
          } else if (check("id") && peek(1).t === "id") {
            label = next().v;
            pname = expect("id").v;
          } else {
            pname = expect("id").v;
            label = pname;
          }
          let ptype = null;
          if (match("op", ":")) ptype = parseType();
          params.push({ label, name: pname, type: ptype });
          if (!match("op", ",")) break;
          if (check("op", ")")) break;
        }
      }
      expect("op", ")");
      let retType = null;
      if (match("op", "->")) retType = parseType();
      expect("op", "{");
      const body = [];
      while (!check("op", "}")) {
        if (check("eof"))
          throw new SSError("Незакрытая функция", peek().line, peek().col);
        body.push(parseStmt());
      }
      expect("op", "}");
      return { k: "Func", name, params, retType, body };
    }

    function parseStruct() {
      expect("kw", "struct");
      const name = expect("id").v;
      expect("op", "{");
      const fields = [];
      const methods = [];
      while (!check("op", "}")) {
        if (check("eof"))
          throw new SSError("Незакрытая структура", peek().line, peek().col);
        if (check("kw", "let") || check("kw", "var")) {
          const kind = next().v;
          const fname = expect("id").v;
          expect("op", ":");
          const ftype = parseType();
          fields.push({ kind, name: fname, type: ftype });
          match("op", ";");
        } else if (check("kw", "func")) {
          methods.push(parseFunc());
        } else {
          const g = peek();
          throw new SSError(
            `В структуре ожидалось поле или метод, получено '${g.v}'`,
            g.line,
            g.col,
          );
        }
      }
      expect("op", "}");
      return { k: "Struct", name, fields, methods };
    }

    function parseEnum() {
      expect("kw", "enum");
      const name = expect("id").v;
      expect("op", "{");
      const cases = [];
      while (!check("op", "}")) {
        if (check("eof"))
          throw new SSError("Незакрытый enum", peek().line, peek().col);
        if (!match("kw", "case")) {
          const g = peek();
          throw new SSError(
            `В enum ожидался case, получено '${g.v}'`,
            g.line,
            g.col,
          );
        }
        const cname = expect("id").v;
        const params = [];
        if (match("op", "(")) {
          if (!check("op", ")")) {
            while (true) {
              let plabel = null,
                pname;
              if (check("op", "_")) {
                next();
                pname = expect("id").v;
              } else if (check("id") && peek(1).t === "id") {
                plabel = next().v;
                pname = expect("id").v;
              } else {
                pname = expect("id").v;
                plabel = pname;
              }
              let ptype = null;
              if (match("op", ":")) ptype = parseType();
              params.push({ label: plabel, name: pname, type: ptype });
              if (!match("op", ",")) break;
            }
          }
          expect("op", ")");
        }
        cases.push({ name: cname, params });
        match("op", ";");
      }
      expect("op", "}");
      return { k: "Enum", name, cases };
    }

    function parseSwitch() {
      expect("kw", "switch");
      const subject = parseExpr(0, { noTrailing: true });
      expect("op", "{");
      const cases = [];
      let defaultBody = null;
      while (!check("op", "}")) {
        if (check("eof"))
          throw new SSError("Незакрытый switch", peek().line, peek().col);
        if (match("kw", "default")) {
          expect("op", ":");
          const body = [];
          while (
            !check("kw", "case") &&
            !check("kw", "default") &&
            !check("op", "}")
          ) {
            if (check("eof"))
              throw new SSError("Незакрытый switch", peek().line, peek().col);
            body.push(parseStmt());
          }
          defaultBody = body;
        } else if (match("kw", "case")) {
          const patterns = [parsePattern()];
          while (match("op", ",")) patterns.push(parsePattern());
          let guard = null;
          if (match("kw", "where")) guard = parseExpr(0);
          expect("op", ":");
          const body = [];
          while (
            !check("kw", "case") &&
            !check("kw", "default") &&
            !check("op", "}")
          ) {
            if (check("eof"))
              throw new SSError("Незакрытый switch", peek().line, peek().col);
            body.push(parseStmt());
          }
          cases.push({ patterns, guard, body });
        } else {
          const g = peek();
          throw new SSError(
            `В switch ожидался case или default, получено '${g.v}'`,
            g.line,
            g.col,
          );
        }
      }
      expect("op", "}");
      return { k: "Switch", subject, cases, defaultBody };
    }

    function parsePattern() {
      if (check("op", ".")) {
        next();
        const name = expect("id").v;
        const bindings = [];
        if (match("op", "(")) {
          if (!check("op", ")")) {
            while (true) {
              if (match("kw", "let")) {
                const bname = expect("id").v;
                bindings.push({ name: bname });
              } else if (check("op", "_")) {
                next();
                bindings.push({ name: null });
              } else {
                const bname = expect("id").v;
                bindings.push({ name: bname });
              }
              if (!match("op", ",")) break;
            }
          }
          expect("op", ")");
        }
        return { k: "PatEnum", name, bindings };
      }
      if (match("kw", "let")) {
        const name = expect("id").v;
        return { k: "PatBind", name };
      }
      if (check("op", "_")) {
        next();
        return { k: "PatWild" };
      }

      const t = peek();
      if (match("kw", "true")) return { k: "PatLit", v: true };
      if (match("kw", "false")) return { k: "PatLit", v: false };
      if (match("kw", "nil")) return { k: "PatLit", v: null };

      // Парсим как выражение — это покроет литералы, диапазоны, отрицательные числа
      const e = parseExpr(0);

      if (e.k === "Lit") return { k: "PatLit", v: e.v };
      if (e.k === "Bin" && (e.op === "..." || e.op === "..<")) {
        return { k: "PatRange", lo: e.l, hi: e.r, inclusive: e.op === "..." };
      }
      return { k: "PatExpr", value: e };
    }

    function parseBlock() {
      expect("op", "{");
      const stmts = [];
      while (!check("op", "}")) {
        if (check("eof"))
          throw new SSError("Незакрытый блок", peek().line, peek().col);
        stmts.push(parseStmt());
      }
      expect("op", "}");
      return { k: "Block", stmts };
    }

    function parseWhile() {
      expect("kw", "while");
      const cond = parseExpr(0, { noTrailing: true });
      const body = parseBlock();
      return { k: "While", cond, body };
    }

    function parseFor() {
      expect("kw", "for");
      const name = expect("id").v;
      expect("kw", "in");
      const src = parseExpr(0, { noTrailing: true });
      const body = parseBlock();
      return { k: "For", name, src, body };
    }

    function parseIf() {
      expect("kw", "if");
      const cond = parseExpr(0, { noTrailing: true });
      const then = parseBlock();
      let elseB = null;
      if (match("kw", "else"))
        elseB = check("kw", "if") ? parseIf() : parseBlock();
      return { k: "If", cond, then, else: elseB };
    }

    function parseExpr(minPrec, flags) {
      flags = flags || {};
      let left = parseUnary(flags);
      while (true) {
        const t = peek();
        if (t.t !== "op") break;
        const prec = PREC[t.v];
        if (prec == null || prec < minPrec) break;
        next();
        const right = parseExpr(prec + 1, flags);
        left = { k: "Bin", op: t.v, l: left, r: right };
      }
      if (minPrec === 0 && check("op", "?")) {
        next();
        const thenE = parseExpr(0);
        expect("op", ":");
        const elseE = parseExpr(0);
        left = { k: "Ternary", cond: left, then: thenE, else: elseE };
      }
      if (minPrec === 0 && check("op", "=")) {
        next();
        const value = parseExpr(0);
        left = { k: "Assign", target: left, op: "=", value };
      } else if (
        minPrec === 0 &&
        ["+=", "-=", "*=", "/=", "%="].some((o) => check("op", o))
      ) {
        const op = next().v;
        const value = parseExpr(0);
        left = { k: "Assign", target: left, op, value };
      }
      return left;
    }

    function parseUnary(flags) {
      const t = peek();
      if (t.t === "op" && (t.v === "-" || t.v === "!" || t.v === "+")) {
        next();
        return { k: "Unary", op: t.v, arg: parseUnary(flags) };
      }
      if (check("kw", "try")) {
        next();
        return { k: "Try", arg: parseUnary(flags) };
      }
      if (check("kw", "await")) {
        next();
        return { k: "Await", arg: parseUnary(flags) };
      }
      return parsePostfix(flags);
    }

    function parsePostfix(flags) {
      let node = parsePrimary(flags);
      while (true) {
        if (check("op", ".")) {
          next();
          const name = expect("id").v;
          node = { k: "Member", obj: node, name, optional: false };
        } else if (check("op", "?.")) {
          next();
          const name = expect("id").v;
          node = { k: "Member", obj: node, name, optional: true };
        } else if (check("op", "(")) {
          next();
          const args = parseArgs();
          expect("op", ")");
          node = { k: "Call", callee: node, args, optional: false };
        } else if (check("op", "[")) {
          next();
          const idx = parseExpr(0);
          expect("op", "]");
          node = { k: "Index", obj: node, idx };
        } else if (check("op", "!")) {
          next();
          node = { k: "Force", arg: node };
        } else if (check("op", "{") && !flags.noTrailing) {
          const closure = parseClosure();
          const arg = { name: null, value: closure };
          if (node.k === "Call") {
            node.args.push(arg);
          } else {
            node = { k: "Call", callee: node, args: [arg] };
          }
        } else {
          break;
        }
      }
      return node;
    }

    function parseArgs() {
      const args = [];
      if (!check("op", ")")) {
        while (true) {
          let name = null;
          if (check("id") && peek(1).t === "op" && peek(1).v === ":") {
            name = next().v;
            next();
          }
          args.push({ name, value: parseExpr(0) });
          if (!match("op", ",")) break;
          if (check("op", ")")) break;
        }
      }
      return args;
    }

    function parsePrimary(flags) {
      const t = peek();
      if (t.t === "num") {
        next();
        return { k: "Lit", v: t.v };
      }
      if (t.t === "str") {
        next();
        return parseString(t.v);
      }
      if (match("kw", "true")) return { k: "Lit", v: true };
      if (match("kw", "false")) return { k: "Lit", v: false };
      if (match("kw", "nil")) return { k: "Lit", v: null };
      if (check("kw", "if")) return parseIf();
      if (check("kw", "switch")) return parseSwitch();

      // .caseName — enum shorthand
      if (check("op", ".")) {
        next();
        const name = expect("id").v;
        return { k: "EnumCase", name };
      }

      if (check("op", "[")) {
        next();
        const items = [],
          pairs = [];
        let isDict = false;
        if (!check("op", "]")) {
          while (true) {
            const first = parseExpr(0);
            if (check("op", ":")) {
              next();
              pairs.push({ key: first, value: parseExpr(0) });
              isDict = true;
            } else items.push(first);
            if (!match("op", ",")) break;
            if (check("op", "]")) break;
          }
        }
        expect("op", "]");
        return isDict ? { k: "Dict", pairs } : { k: "Array", items };
      }

      if (check("op", "(")) {
        next();
        const items = [];
        if (!check("op", ")")) {
          while (true) {
            if (check("id") && peek(1).t === "op" && peek(1).v === ":") {
              next();
              next();
            }
            items.push(parseExpr(0));
            if (!match("op", ",")) break;
          }
        }
        expect("op", ")");
        return items.length === 1 ? items[0] : { k: "Tuple", items };
      }

      if (check("op", "{")) return parseClosure();

      if (check("kw", "func")) {
        next();
        expect("op", "(");
        const params = [];
        if (!check("op", ")")) {
          while (true) {
            if (check("op", "_")) next();
            const pname = expect("id").v;
            let ptype = null;
            if (match("op", ":")) ptype = parseType();
            params.push({ name: pname, type: ptype });
            if (!match("op", ",")) break;
          }
        }
        expect("op", ")");
        if (match("op", "->")) parseType();
        expect("op", "{");
        const body = [];
        while (!check("op", "}")) body.push(parseStmt());
        expect("op", "}");
        return { k: "Closure", params, body };
      }

      if (t.t === "id") {
        next();
        return { k: "Id", name: t.v };
      }
      if (t.t === "kw" && t.v === "self") {
        next();
        return { k: "Id", name: "self" };
      }

      throw new SSError(`Неожиданный токен '${t.v}'`, t.line, t.col);
    }

    function parseClosure() {
      expect("op", "{");
      const save = pos;
      let params = null;

      if (check("op", "(")) {
        try {
          next();
          const ps = [];
          if (!check("op", ")")) {
            while (true) {
              if (check("op", "_")) next();
              const pname = expect("id").v;
              let ptype = null;
              if (match("op", ":")) ptype = parseType();
              ps.push({ name: pname, type: ptype });
              if (!match("op", ",")) break;
            }
          }
          expect("op", ")");
          if (match("op", "->")) parseType();
          if (match("kw", "in")) params = ps;
          else pos = save;
        } catch {
          pos = save;
        }
      }

      if (
        !params &&
        check("id") &&
        ((peek(1).t === "kw" && peek(1).v === "in") ||
          (peek(1).t === "op" && peek(1).v === ","))
      ) {
        const trySave = pos;
        try {
          const ps = [{ name: expect("id").v, type: null }];
          while (match("op", ","))
            ps.push({ name: expect("id").v, type: null });
          if (match("kw", "in")) params = ps;
          else pos = trySave;
        } catch {
          pos = trySave;
        }
      }

      const body = [];
      while (!check("op", "}")) {
        if (check("eof"))
          throw new SSError("Незакрытое замыкание", peek().line, peek().col);
        body.push(parseStmt());
      }
      expect("op", "}");
      return { k: "Closure", params, body };
    }

    function parseString(raw) {
      if (!raw.includes("\\(")) return { k: "Lit", v: unescape(raw) };
      const parts = [];
      let i = 0,
        text = "";
      while (i < raw.length) {
        if (raw[i] === "\\" && raw[i + 1] === "(") {
          if (text) {
            parts.push({ type: "text", value: text });
            text = "";
          }
          i += 2;
          let depth = 1,
            j = i;
          while (j < raw.length && depth > 0) {
            if (raw[j] === "(") depth++;
            else if (raw[j] === ")") depth--;
            if (depth === 0) break;
            j++;
          }
          const sub = raw.slice(i, j);
          const subAst = parse(window.SwiftScript.tokenize(sub));
          if (subAst.stmts.length !== 1 || subAst.stmts[0].k !== "Expr")
            throw new SSError("Интерполяция должна быть выражением", 1, 1);
          parts.push({ type: "expr", value: subAst.stmts[0].expr });
          i = j + 1;
        } else if (raw[i] === "\\" && i + 1 < raw.length) {
          const n = raw[i + 1];
          text +=
            n === "n"
              ? "\n"
              : n === "t"
                ? "\t"
                : n === "r"
                  ? "\r"
                  : n === "\\"
                    ? "\\"
                    : n;
          i += 2;
        } else text += raw[i++];
      }
      if (text) parts.push({ type: "text", value: text });
      return { k: "Template", parts };
    }

    function unescape(s) {
      let out = "",
        i = 0;
      while (i < s.length) {
        if (s[i] === "\\" && i + 1 < s.length) {
          const n = s[i + 1];
          out +=
            n === "n"
              ? "\n"
              : n === "t"
                ? "\t"
                : n === "r"
                  ? "\r"
                  : n === "\\"
                    ? "\\"
                    : n === '"'
                      ? '"'
                      : n === "'"
                        ? "'"
                        : n;
          i += 2;
        } else out += s[i++];
      }
      return out;
    }

    return parseProgram();
  }

  window.SwiftScript.parse = parse;
})();
