(function () {
  "use strict";
  const { compile, runInSandbox } = window.SwiftScript;

  const SAMPLE = `// SwiftScript demo — enum, switch, struct, value-семантика

enum Status {
    case ok
    case warn(String)
    case error(String)
}

func describe(_ s: Status) -> String {
    switch s {
    case .ok:
        return "fine"
    case .warn(let msg):
        return "warn: " + msg
    case .error(let msg):
        return "error: " + msg
    }
}

print(describe(.ok))
print(describe(.warn("slow")))
print(describe(.error("boom")))

// ——— switch по числам, диапазоны ———

func classify(_ code: Int) -> String {
    switch code {
    case 200:
        return "OK"
    case 301, 302:
        return "Redirect"
    case 400...499:
        return "Client Error"
    case 500..<600:
        return "Server Error"
    default:
        return "Unknown"
    }
}

print(classify(200))
print(classify(302))
print(classify(404))
print(classify(503))
print(classify(100))

// ——— struct ———

struct Point {
    let x: Int
    let y: Int

    func manhattan() -> Int {
        return abs(self.x) + abs(self.y)
    }

    func scaled(_ k: Int) -> Point {
        return Point(x: self.x * k, y: self.y * k)
    }
}

let p = Point(x: 3, y: 4)
print("manhattan(p) = \\(p.manhattan())")

let q = p.scaled(2)
print("p = (\\(p.x), \\(p.y))")
print("q = (\\(q.x), \\(q.y))")

// ——— базовые вещи ———

let name = "World"
let greeting = "Hello, \\(name)!"
print(greeting)

func fib(_ n: Int) -> Int {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}
print("fib(10) = \\(fib(10))")

let nums = [1, 2, 3, 4, 5]
let doubled = nums.map { $0 * 2 }
print("doubled: \\(doubled)")

let sum = nums.reduce(0) { $0 + $1 }
print("sum: \\(sum)")

var total = 0
for n in nums { total += n }
print("total: \\(total)")

let nothing: Int? = nil
print(nothing ?? 42)

let squares = (0..<5).map { $0 * $0 }
print("squares: \\(squares)")
`;

  const editor = CodeMirror.fromTextArea(document.getElementById("editor"), {
    mode: "swift",
    lineNumbers: true,
    indentUnit: 4,
    tabSize: 4,
  });
  editor.setValue(SAMPLE);

  const jsView = CodeMirror.fromTextArea(document.getElementById("jsout"), {
    mode: "javascript",
    lineNumbers: true,
    readOnly: true,
  });

  const term = document.getElementById("term");
  const btnRun = document.getElementById("btn-run");
  const btnClear = document.getElementById("btn-clear");

  function refresh() {
    const { userJs } = compile(editor.getValue());
    jsView.setValue(userJs || "// (пусто)");
  }

  function doRun() {
    refresh();
    const { js, diagnostics } = compile(editor.getValue());
    if (diagnostics.length) {
      term.innerHTML = "";
      for (const d of diagnostics) {
        const el = document.createElement("div");
        el.className = "err";
        el.textContent = `✖ ${d.line}:${d.col}  ${d.msg}`;
        term.appendChild(el);
      }
      return;
    }
    const out = runInSandbox(js);
    term.innerHTML = "";
    if (!out.length) {
      term.innerHTML = '<div class="dim">(нет вывода)</div>';
      return;
    }
    for (const it of out) {
      const el = document.createElement("div");
      el.className = it.kind;
      el.textContent = it.text;
      term.appendChild(el);
    }
  }

  editor.on("change", () => {
    clearTimeout(window._t);
    window._t = setTimeout(refresh, 200);
  });
  btnRun.addEventListener("click", doRun);
  btnClear.addEventListener("click", () => {
    term.innerHTML = '<div class="dim">(пусто)</div>';
  });

  refresh();
  doRun();
})();
