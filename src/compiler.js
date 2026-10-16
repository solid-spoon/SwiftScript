(function () {
  "use strict";
  const { tokenize, parse, genProgram, SSError, RUNTIME } = window.SwiftScript;

  function compile(src) {
    const diagnostics = [];
    let userJs = "";
    try {
      const ast = parse(tokenize(src));
      userJs = genProgram(ast);
    } catch (e) {
      if (e instanceof SSError) diagnostics.push({ line: e.line, col: e.col, msg: e.message, severity: "error" });
      else diagnostics.push({ line: 1, col: 1, msg: e.message, severity: "error" });
    }
    return { js: RUNTIME + "\n" + userJs, userJs, diagnostics };
  }

  window.SwiftScript.compile = compile;
})();