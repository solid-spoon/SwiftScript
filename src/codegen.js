(function () {
  "use strict";
  const { SSError } = window.SwiftScript;

  let _swCounter = 0;

  function collectDollar(node, set) {
    if (!node || typeof node !== "object") return;
    if (Array.isArray(node)) {
      node.forEach((n) => collectDollar(n, set));
      return;
    }
    if (node.k === "Id" && /^\$\d+$/.test(node.name))
      set.add(Number(node.name.slice(1)));
    for (const key of Object.keys(node)) {
      if (key === "k") continue;
      collectDollar(node[key], set);
    }
  }

  function collectDecls(p) {
    const structNames = new Set();
    const enumCases = {}; // caseName → enumName
    const enumCaseParamCount = {}; // caseName → params.length
    for (const s of p.stmts) {
      if (s.k === "Struct") structNames.add(s.name);
      if (s.k === "Enum") {
        for (const c of s.cases) {
          enumCases[c.name] = s.name;
          enumCaseParamCount[c.name] = c.params.length;
        }
      }
    }
    return { structNames, enumCases, enumCaseParamCount };
  }

  function genProgram(p) {
    const decls = collectDecls(p);
    const ctx = { ...decls, _swCounter: 0 };
    return p.stmts.map((s) => genStmt(s, ctx)).join("\n");
  }

  function genStmt(n, ctx) {
    ctx = ctx || {};
    switch (n.k) {
      case "Let": {
        const kw = n.kind === "var" ? "let" : "const";
        const v = n.value ? genExpr(n.value, ctx) : "undefined";
        if (ctx.locals) ctx.locals.add(n.name);
        return `${kw} ${n.name} = ${v};`;
      }
      case "Func": {
        const params = n.params.map((p) => p.name).join(", ");
        const inner = {
          ...ctx,
          fields: null,
          inMethod: false,
          locals: new Set(n.params.map((p) => p.name)),
        };
        const body = n.body.map((s) => genStmt(s, inner)).join("\n");
        return `function ${n.name}(${params}) {\n${body}\n}`;
      }
      case "Struct":
        return genStruct(n, ctx);
      case "Enum":
        return genEnum(n, ctx);
      case "Return":
        return n.expr ? `return ${genExpr(n.expr, ctx)};` : "return;";
      case "Expr":
        if (n.expr.k === "If") return genIfStmt(n.expr, ctx);
        if (n.expr.k === "Switch") return genSwitch(n.expr, ctx);
        return `${genExpr(n.expr, ctx)};`;
      case "Block":
        return `{ ${n.stmts.map((s) => genStmt(s, ctx)).join(" ")} }`;
      case "If":
        return genIfStmt(n, ctx);
      case "While": {
        const inner = {
          ...ctx,
          breakStack: [...(ctx.breakStack || []), "loop"],
        };
        return `while (${genExpr(n.cond, ctx)}) ${genStmt(n.body, inner)}`;
      }
      case "For": {
        const inner = {
          ...ctx,
          breakStack: [...(ctx.breakStack || []), "loop"],
          locals: new Set([...(ctx.locals || []), n.name]),
        };
        return `for (const ${n.name} of ${genExpr(n.src, ctx)}) ${genStmt(n.body, inner)}`;
      }
      case "Break": {
        const stack = ctx.breakStack || [];
        const top = stack[stack.length - 1];
        if (top === "switch") return `break ${ctx.switchLabel};`;
        return "break;";
      }
      case "Continue":
        return "continue;";
      default:
        throw new SSError(`Кодоген: неизвестный стейтмент ${n.k}`, 1, 1);
    }
  }

  function genIfStmt(n, ctx) {
    let out = `if (${genExpr(n.cond, ctx)}) ${genStmt(n.then, ctx)}`;
    if (n.else)
      out += ` else ${n.else.k === "If" ? genIfStmt(n.else, ctx) : genStmt(n.else, ctx)}`;
    return out;
  }

  function blockReturning(stmts, ctx) {
    if (!stmts.length) return "return null;";
    return stmts
      .map((s, i) => {
        const isLast = i === stmts.length - 1;
        if (isLast && s.k === "Expr") return `return ${genExpr(s.expr, ctx)};`;
        return genStmt(s, ctx);
      })
      .join(" ");
  }

  function genEnum(n, ctx) {
    const lines = [`const ${n.name} = Object.freeze({`];
    for (const c of n.cases) {
      if (c.params.length === 0) {
        lines.push(
          `  ${c.name}: Object.freeze({ _case: ${JSON.stringify(c.name)} }),`,
        );
      } else {
        const ps = c.params.map((p, i) => `_${i}`).join(", ");
        const fields = c.params.map((p, i) => `_${i}`).join(", ");
        lines.push(
          `  ${c.name}: (${ps}) => Object.freeze({ _case: ${JSON.stringify(c.name)}, ${fields} }),`,
        );
      }
    }
    lines.push(`});`);
    return lines.join("\n");
  }

  function genSwitch(n, ctx) {
    const label = `_sw${++_swCounter}`;
    const subjVar = `_s${_swCounter}`;
    const swCtx = {
      ...ctx,
      breakStack: [...(ctx.breakStack || []), "switch"],
      switchLabel: label,
    };

    const lines = [];
    lines.push(`${label}: {`);
    lines.push(`  const ${subjVar} = ${genExpr(n.subject, ctx)};`);

    for (const c of n.cases) {
      // Собираем все паттерны case — объединяем их условия через ||
      const allConds = [];
      const allBindings = [];
      for (const p of c.patterns) {
        const { cond, bindings } = genPattern(p, subjVar, swCtx);
        allConds.push(cond);
        for (const b of bindings) allBindings.push(b);
      }

      let fullCond =
        allConds.length === 1 ? allConds[0] : `(${allConds.join(" || ")})`;

      if (c.guard) {
        const bindCode = allBindings.join(" ");
        fullCond = `(() => { if (!(${fullCond})) return false; ${bindCode} return (${genExpr(c.guard, swCtx)}); })()`;
      }

      lines.push(`  if (${fullCond}) {`);
      for (const b of allBindings) lines.push(`    ${b}`);
      for (const stmt of c.body) lines.push("    " + genStmt(stmt, swCtx));
      lines.push(`    break ${label};`);
      lines.push(`  }`);
    }

    if (n.defaultBody) {
      lines.push(`  {`);
      for (const stmt of n.defaultBody)
        lines.push("    " + genStmt(stmt, swCtx));
      lines.push(`  }`);
    }

    lines.push(`}`);
    return lines.join("\n");
  }

  function genPattern(p, subjVar, ctx) {
    switch (p.k) {
      case "PatEnum": {
        const cond = `${subjVar} && ${subjVar}._case === ${JSON.stringify(p.name)}`;
        const bindings = [];
        p.bindings.forEach((b, i) => {
          if (b.name) bindings.push(`const ${b.name} = ${subjVar}._${i};`);
        });
        return { cond, bindings };
      }
      case "PatBind":
        return { cond: "true", bindings: [`const ${p.name} = ${subjVar};`] };
      case "PatWild":
        return { cond: "true", bindings: [] };
      case "PatLit": {
        const val = p.v === null ? "null" : JSON.stringify(p.v);
        return { cond: `${subjVar} === ${val}`, bindings: [] };
      }
      case "PatRange": {
        const lo = genExpr(p.lo, ctx);
        const hi = genExpr(p.hi, ctx);
        const hiOp = p.inclusive ? "<=" : "<";
        return {
          cond: `(${subjVar} >= ${lo}) && (${subjVar} ${hiOp} ${hi})`,
          bindings: [],
        };
      }
      case "PatExpr":
        return {
          cond: `${subjVar} === ${genExpr(p.value, ctx)}`,
          bindings: [],
        };
      default:
        throw new SSError(`Кодоген: неизвестный паттерн ${p.k}`, 1, 1);
    }
  }

  function genStruct(n, parentCtx) {
    const fieldNames = n.fields.map((f) => f.name);
    const methodNames = n.methods.map((m) => m.name);
    const members = new Set([...fieldNames, ...methodNames]);

    const lines = [];
    lines.push(`function ${n.name}(_args) {`);
    if (fieldNames.length) {
      lines.push(`  const { ${fieldNames.join(", ")} } = _args;`);
      lines.push(`  const _self = { ${fieldNames.join(", ")} };`);
    } else {
      lines.push(`  const _self = {};`);
    }
    for (const m of n.methods) {
      const mctx = {
        ...parentCtx,
        fields: members,
        inMethod: true,
        locals: new Set(m.params.map((p) => p.name)),
      };
      const paramNames = m.params.map((p) => p.name).join(", ");
      const body = m.body.length
        ? m.body
            .map((s, i) => {
              const isLast = i === m.body.length - 1;
              if (isLast && s.k === "Expr")
                return `return ${genExpr(s.expr, mctx)};`;
              return genStmt(s, mctx);
            })
            .join("\n    ")
        : "return null;";
      lines.push(`  _self.${m.name} = function(${paramNames}) {`);
      lines.push(`    ${body}`);
      lines.push(`  };`);
    }
    const allLet = n.fields.every((f) => f.kind === "let");
    lines.push(allLet ? "  return Object.freeze(_self);" : "  return _self;");
    lines.push("}");
    return lines.join("\n");
  }

  function genExpr(n, ctx) {
    ctx = ctx || {};
    switch (n.k) {
      case "Lit":
        return n.v === null ? "null" : JSON.stringify(n.v);
      case "Id": {
        if (n.name === "self" && ctx.inMethod) return "_self";
        if (
          ctx.fields &&
          ctx.fields.has(n.name) &&
          !(ctx.locals && ctx.locals.has(n.name))
        ) {
          return "_self." + n.name;
        }
        return n.name;
      }
      case "EnumCase": {
        const enumName = ctx.enumCases && ctx.enumCases[n.name];
        if (enumName) return `${enumName}.${n.name}`;
        return n.name;
      }
      case "Template":
        return genTemplate(n, ctx);
      case "Array":
        return `[${n.items.map((x) => genExpr(x, ctx)).join(", ")}]`;
      case "Dict":
        return `({${n.pairs.map((p) => `[${genExpr(p.key, ctx)}]: ${genExpr(p.value, ctx)}`).join(", ")}})`;
      case "Tuple":
        return `[${n.items.map((x) => genExpr(x, ctx)).join(", ")}]`;
      case "Member":
        return `${genExpr(n.obj, ctx)}${n.optional ? "?." : "."}${n.name}`;
      case "Index":
        return `${genExpr(n.obj, ctx)}${n.optional ? "?.[" : "["}${genExpr(n.idx, ctx)}]`;
      case "Call":
        return genCall(n, ctx);
      case "Force":
        return `_force(${genExpr(n.arg, ctx)})`;
      case "Unary":
        return `(${n.op}${genExpr(n.arg, ctx)})`;
      case "Bin":
        if (n.op === "..<")
          return `_range(${genExpr(n.l, ctx)}, ${genExpr(n.r, ctx)})`;
        if (n.op === "...")
          return `_rangeIn(${genExpr(n.l, ctx)}, ${genExpr(n.r, ctx)})`;
        if (n.op === "??")
          return `(${genExpr(n.l, ctx)} ?? ${genExpr(n.r, ctx)})`;
        return `(${genExpr(n.l, ctx)} ${n.op} ${genExpr(n.r, ctx)})`;
      case "Ternary":
        return `(${genExpr(n.cond, ctx)} ? ${genExpr(n.then, ctx)} : ${genExpr(n.else, ctx)})`;
      case "Assign":
        return `(${genExpr(n.target, ctx)} ${n.op} ${genExpr(n.value, ctx)})`;
      case "If":
        return genIfExpr(n, ctx);
      case "Switch":
        // switch as expression: only inside expression context — not supported, but wrap in IIFE
        return `(() => { ${genSwitch(n, ctx)} })()`;
      case "Closure":
        return genClosure(n, ctx);
      case "Try":
        return genExpr(n.arg, ctx);
      case "Await":
        return `(await ${genExpr(n.arg, ctx)})`;
      default:
        throw new SSError(`Кодоген: неизвестное выражение ${n.k}`, 1, 1);
    }
  }

  function genCall(n, ctx) {
    // Struct instantiation → {named fields}
    if (
      n.callee.k === "Id" &&
      ctx.structNames &&
      ctx.structNames.has(n.callee.name)
    ) {
      const fields = n.args.map((a) => {
        if (!a.name)
          throw new SSError(
            `Структура ${n.callee.name}: аргументы должны быть именованными`,
            1,
            1,
          );
        return `${a.name}: ${genExpr(a.value, ctx)}`;
      });
      return `${n.callee.name}({${fields.join(", ")}})`;
    }

    const callee = n.callee;
    let args = n.args.map((a) => a.value);

    // xs.reduce(0) { $0 + $1 } → xs.reduce(closure, 0)
    if (
      callee.k === "Member" &&
      callee.name === "reduce" &&
      args.length === 2 &&
      args[1].k === "Closure"
    ) {
      args = [args[1], args[0]];
    }

    return `${genExpr(callee, ctx)}${n.optional ? "?." : ""}(${args.map((a) => genExpr(a, ctx)).join(", ")})`;
  }

  function genIfExpr(n, ctx) {
    const thenCode = `if (${genExpr(n.cond, ctx)}) { ${blockReturning(n.then.stmts, ctx)} }`;
    let elseCode;
    if (!n.else) elseCode = "return null;";
    else if (n.else.k === "If") elseCode = `return ${genIfExpr(n.else, ctx)};`;
    else elseCode = `{ ${blockReturning(n.else.stmts, ctx)} }`;
    return `(() => { ${thenCode} ${elseCode} })()`;
  }

  function genClosure(n, ctx) {
    let params;
    if (n.params && n.params.length) {
      params = n.params.map((p) => p.name).join(", ");
    } else {
      const refs = new Set();
      collectDollar(n.body, refs);
      if (refs.size === 0) params = "";
      else {
        const max = Math.max(...refs);
        params = Array.from({ length: max + 1 }, (_, i) => `$${i}`).join(", ");
      }
    }
    const innerLocals = new Set(ctx.locals || []);
    if (n.params) for (const p of n.params) innerLocals.add(p.name);
    const innerCtx = { ...ctx, locals: innerLocals };
    return `((${params}) => { ${blockReturning(n.body, innerCtx)} })`;
  }

  function genTemplate(n, ctx) {
    let out = "`";
    for (const p of n.parts) {
      if (p.type === "text") {
        out += p.value
          .replace(/\\/g, "\\\\")
          .replace(/`/g, "\\`")
          .replace(/\$/g, "\\$");
      } else {
        out += "${String(" + genExpr(p.value, ctx) + ")}";
      }
    }
    return out + "`";
  }

  window.SwiftScript.genProgram = genProgram;
})();
