#!/usr/bin/env bash
# Golden test runner for swsc.
#
# Usage:
#   ./test/run.sh              run tests, compare with golden files
#   ./test/run.sh --update     regenerate golden files from current output
#   ./test/run.sh -u           same as --update
#
# Layout:
#   test/swiftscript/*.sws         positive tests, expected stdout in
#                                  test/golden/<name>.out
#   test/swiftscript/neg/*.sws     negative tests, must fail check;
#                                  expected substring of stderr in
#                                  "// EXPECT_ERROR: ..." header comment

set -euo pipefail

if ! ./build.sh > /dev/null; then
    echo "❌ Build failed!"
    exit 1
fi

GOLDEN_DIR="test/golden"
mkdir -p "$GOLDEN_DIR"

UPDATE=0
case "${1:-}" in
    -u|--update) UPDATE=1 ;;
esac

FAILED=0
PASSED=0

run_positive() {
    local src="$1"
    local name
    name=$(basename "$src" .sws)
    local golden="$GOLDEN_DIR/${name}.out"

    echo "▶ $src"

    if ! ./swsc check "$src" > /dev/null 2>&1; then
        echo "  ❌ FAIL: check"
        ./swsc check "$src" 2>&1 | sed 's/^/       /'
        FAILED=$((FAILED + 1))
        return
    fi

    local out
    local code=0
    out=$(./swsc run "$src" 2>&1) || code=$?

    if [ $code -ne 0 ]; then
        echo "  ❌ FAIL: runtime (exit $code)"
        echo "$out" | sed 's/^/       /'
        FAILED=$((FAILED + 1))
        return
    fi

    if [ $UPDATE -eq 1 ] || [ ! -f "$golden" ]; then
        echo "$out" > "$golden"
        if [ $UPDATE -eq 1 ]; then
            echo "  📝 UPDATED"
        else
            echo "  ⚠️  CREATED $golden"
        fi
        PASSED=$((PASSED + 1))
        return
    fi

    local diff_file
    diff_file=$(mktemp)
    if diff -u "$golden" <(printf '%s\n' "$out") > "$diff_file"; then
        echo "  ✅ PASS"
        PASSED=$((PASSED + 1))
    else
        echo "  ❌ FAIL: output mismatch"
        sed 's/^/       /' "$diff_file" | head -40
        FAILED=$((FAILED + 1))
    fi
    rm -f "$diff_file"
}

run_negative() {
    local src="$1"
    echo "▶ $src (negative)"

    local expected
    expected=$(grep -o 'EXPECT_ERROR:.*' "$src" 2>/dev/null | head -1 | sed 's/EXPECT_ERROR: *//' || true)

    local code=0
    local errout
    errout=$(./swsc check "$src" 2>&1) || code=$?

    if [ $code -eq 0 ]; then
        echo "  ❌ FAIL: expected error, got success"
        FAILED=$((FAILED + 1))
        return
    fi

    if [ -n "$expected" ] && ! echo "$errout" | grep -qF -- "$expected"; then
        echo "  ❌ FAIL: error message mismatch"
        echo "  Expected stderr to contain: $expected"
        echo "  Actual:"
        echo "$errout" | sed 's/^/       /'
        FAILED=$((FAILED + 1))
        return
    fi

    echo "  ✅ PASS"
    PASSED=$((PASSED + 1))
}

# ── examples (pre-existing) ───────────────────────────────────────────
for f in examples/bank/bank.sws examples/todos/todo.sws; do
    [ -f "$f" ] && run_positive "$f"
done

# ── positive ──────────────────────────────────────────────────────────
for f in test/swiftscript/*.sws; do
    [ -f "$f" ] && run_positive "$f"
done

# ── negative ──────────────────────────────────────────────────────────
if [ -d test/swiftscript/neg ]; then
    for f in test/swiftscript/neg/*.sws; do
        [ -f "$f" ] && run_negative "$f"
    done
fi

echo ""
if [ $FAILED -eq 0 ]; then
    echo "🎉 All tests passed! ($PASSED total)"
    exit 0
else
    echo "💥 $FAILED failed, $PASSED passed"
    exit 1
fi