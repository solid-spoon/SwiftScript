#!/usr/bin/env bash

# Build the compiler first
if ! ./build.sh > /dev/null; then
    echo "❌ Build failed!"
    exit 1
fi

GOLDEN_DIR="test/golden"
mkdir -p "$GOLDEN_DIR"

FAILED=0

run_test() {
    local src_file="$1"
    local base_name=$(basename "$src_file" .sws)
    local expected_out="$GOLDEN_DIR/${base_name}.out"
    
    echo "▶ Testing $src_file"
    
    # 1. Check syntax/codegen
    if ! ./swsc check "$src_file" > /dev/null 2>&1; then
        echo "  ❌ FAIL: check failed (syntax/codegen error)"
        FAILED=1
        return
    fi

    # 2. Run and capture output
    local actual_out
    actual_out=$(./swsc run "$src_file" 2>&1)
    local exit_code=$?

    if [ $exit_code -ne 0 ]; then
        echo "  ❌ FAIL: runtime error (exit code $exit_code)"
        echo "$actual_out"
        FAILED=1
        return
    fi
    
    # 3. Diff
    if [ ! -f "$expected_out" ]; then
        echo "  ⚠️  Missing golden file. Creating $expected_out"
        echo "$actual_out" > "$expected_out"
    else
        if ! diff -u "$expected_out" <(echo "$actual_out"); then
            echo "  ❌ FAIL: output mismatch"
            FAILED=1
        else
            echo "  ✅ PASS"
        fi
    fi
}

# Test examples
for f in examples/bank/bank.sws examples/todos/todo.sws; do
    [ -f "$f" ] && run_test "$f"
done

# Test example sws
for f in test/swiftscript/*.sws; do
    [ -f "$f" ] && run_test "$f"
done

if [ $FAILED -eq 0 ]; then
    echo -e "\n🎉 All tests passed!"
    exit 0
else
    echo -e "\n💥 Some tests failed."
    exit 1
fi