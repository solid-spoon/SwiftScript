#!/usr/bin/env bash
# swsc — Rust-strict build driver.
#
# Principles:
#   1. Deny-by-default: warnings are errors (-Werror).
#   2. Explicit modes: release / debug / check / paranoid / test / clean.
#   3. Reproducible: no timestamps, no git hash, no hostname baked in.
#   4. Verified: preflight checks + post-build sanity check.
#   5. Loud: full command with -v; colors disabled via NO_COLOR.

set -euo pipefail
IFS=$'\n\t'

# ---------- constants ----------

readonly SCRIPT="${0##*/}"
readonly ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SRC="$ROOT/tools"
readonly OUT="$ROOT/swsc"

readonly SOURCES=(
    "$SRC/util.c"
    "$SRC/ast.c"
    "$SRC/lexer.c"
    "$SRC/parser.c"
    "$SRC/codegen.c"
    "$SRC/sourcemap.c"
    "$SRC/typecheck.c"
#    "$SRC/codegen_c.c" -- This is a research artifact, not a supported target.
    "$SRC/main.c"
)
readonly HEADERS=(
    "$SRC/util.h"
    "$SRC/ast.h"
    "$SRC/lexer.h"
    "$SRC/parser.h"
    "$SRC/codegen.h"
    "$SRC/runtime.h"
    "$SRC/sourcemap.h"
    "$SRC/typecheck.h"
#    "$SRC/codegen_c.h"  -- This is a research artifact, not a supported target.
)

V="${V:-0}"

# ---------- colors (rustc-style diagnostics) ----------

if [[ -t 2 && -z "${NO_COLOR:-}" ]]; then
    C_RED=$'\033[1;31m'; C_GRN=$'\033[1;32m'; C_YEL=$'\033[1;33m'
    C_BLU=$'\033[1;34m'; C_GRY=$'\033[1;90m'; C_RST=$'\033[0m'
else
    C_RED=; C_GRN=; C_YEL=; C_BLU=; C_GRY=; C_RST=
fi

# ---------- diagnostics ----------

err()  { printf '%serror%s: %s\n'   "$C_RED" "$C_RST" "$*" >&2; }
warn() { printf '%swarning%s: %s\n' "$C_YEL" "$C_RST" "$*" >&2; }
info() { printf '%sinfo%s: %s\n'    "$C_BLU" "$C_RST" "$*" >&2; }
ok()   { printf '%s✔%s %s\n'        "$C_GRN" "$C_RST" "$*" >&2; }

# exit codes:
#   0 = success
#   1 = compile / test failure
#   2 = usage / preflight failure
die()  { err "$*"; exit 2; }

trap 'rc=$?; if (( rc != 0 && rc != 2 )); then err "build failed (exit $rc)"; fi' EXIT

# ---------- platform ----------

platform() {
    case "$(uname -s)" in
        Linux*)               echo linux   ;;
        Darwin*)              echo macos   ;;
        CYGWIN*|MINGW*|MSYS*) echo windows ;;
        *)                    echo unknown ;;
    esac
}
readonly PLAT="$(platform)"

# ---------- compiler detection ----------

detect_cc() {
    if [[ -n "${CC:-}" ]]; then
        command -v "$CC" >/dev/null 2>&1 || die "CC=$CC: not found in PATH"
        echo "$CC"; return
    fi
    local c
    for c in cc gcc clang; do
        if command -v "$c" >/dev/null 2>&1; then echo "$c"; return; fi
    done
    die "no C compiler in PATH; set CC=..."
}

readonly CC_BIN="$(detect_cc)"

# ---------- flag profiles ----------

# Strict flags that must compile this codebase cleanly.
flags_strict() {
    local -a f=(
        -std=c11
        -Wall -Wextra -Wpedantic
        -Werror
        -Wshadow
        -Wundef
        -Wwrite-strings
        -Wcast-qual
        -Wcast-align
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wmissing-declarations
        -Wformat=2
        -Wformat-security
        -Wvla
        -Wstrict-aliasing=2
        -Wpointer-arith
        -Wredundant-decls
        -Wold-style-definition
        -Wnested-externs
        -Wimplicit-fallthrough
        -Wswitch-bool
        -Wno-unused-parameter
    )
    if "$CC_BIN" --version 2>/dev/null | grep -q "clang"; then
        f+=( -Wno-newline-eof )
    fi
    printf '%s\n' "${f[@]}"
}

# Additional flags that usually require source fixes.
# Only enabled in `paranoid` mode.
flags_paranoid() {
    local -a f=(
        -Wconversion
        -Wsign-conversion
        -Wswitch-enum
        -Wswitch-default
        -Wdouble-promotion
        -Wfloat-equal
        -Wbad-function-cast
        -Wformat-truncation
        -Wframe-larger-than=65536
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
    )
    printf '%s\n' "${f[@]}"
}

# Sanitizers (debug).
flags_sanitize() {
    local -a f=(
        -O1
        -g3
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fsanitize-address-use-after-scope
    )
    printf '%s\n' "${f[@]}"
}

# Hardening (release).
flags_harden() {
    case "$PLAT" in
        linux)
            local -a f=(
                -O2 -pipe -DNDEBUG
                -D_FORTIFY_SOURCE=2
                -fstack-protector-strong
                -fPIE -pie
                -fno-plt
                -Wl,-z,relro,-z,now
                -Wl,-z,noexecstack
            )
            printf '%s\n' "${f[@]}"
            ;;
        macos)
            local -a f=(
                -O2 -pipe -DNDEBUG
                -D_FORTIFY_SOURCE=2
                -fstack-protector-strong
                -fPIE -pie
                -Qunused-arguments
            )
            printf '%s\n' "${f[@]}"
            ;;
        *)
            printf '%s\n' -O2 -DNDEBUG
            ;;
    esac
}

# ---------- preflight ----------

preflight() {
    local missing=0 f
    for f in "${SOURCES[@]}" "${HEADERS[@]}"; do
        if [[ ! -f "$f" ]]; then
            err "missing file: ${f#"$ROOT"/}"
            missing=1
        fi
    done
    (( missing == 0 )) || exit 2

    # Smoke-test the compiler. Catches CC=clang-99-style typos.
    # Portable: uses a temp file instead of /dev/null so it works on MSYS2.
    local smoke_bin="$ROOT/.swsc-smoke-test"
    if ! printf 'int main(void){return 0;}\n' \
            | "$CC_BIN" -x c - -o "$smoke_bin" 2>/dev/null; then
        rm -f "$smoke_bin"
        die "$CC_BIN does not produce working binaries"
    fi
    rm -f "$smoke_bin"
}

# ---------- execution ----------

run_cc() {
    if (( V )); then
        printf '%s+ %s' "$C_GRY" "$C_RST" >&2
        printf '%q ' "$CC_BIN" "$@" >&2
        printf '\n' >&2
    fi
    "$CC_BIN" "$@"
}

# Apply WERROR=0 escape hatch (local iteration only).
demote_werror() {
    local -a in=( "$@" )
    local -a out=() f
    for f in "${in[@]}"; do
        if [[ "$f" == "-Werror" && "${WERROR:-1}" == "0" ]]; then
            out+=("-Wno-error")
        else
            out+=("$f")
        fi
    done
    printf '%s\n' "${out[@]}"
}

# ---------- commands ----------

cmd_check() {
    preflight
    info "compiler: $CC_BIN ($("$CC_BIN" --version 2>/dev/null | head -1))"
    info "mode: check (-fsyntax-only)"
    local -a fl=( $(demote_werror $(flags_strict)) )
    local t0=$SECONDS i
    for i in "${!SOURCES[@]}"; do
        printf '%s[%d/%d]%s checking %s\n' \
            "$C_GRY" $((i+1)) ${#SOURCES[@]} "$C_RST" \
            "${SOURCES[i]#"$ROOT"/}" >&2
        run_cc "${fl[@]}" -fsyntax-only "${SOURCES[i]}"
    done
    ok "checked in $((SECONDS - t0))s"
}

cmd_build() {
    local mode="${1:-release}"
    preflight
    info "compiler: $CC_BIN ($("$CC_BIN" --version 2>/dev/null | head -1))"
    info "mode: $mode"

    local -a fl=( $(flags_strict) )

    case "$mode" in
        release)
            fl+=( $(flags_harden) )
            ;;
        debug)
            fl+=( $(flags_sanitize) )
            ;;
        paranoid)
            fl+=( $(flags_harden) )
            fl+=( $(flags_paranoid) )
            warn "paranoid mode: additional warnings enabled; expect failures"
            ;;
        *)
            die "internal: unknown mode $mode"
            ;;
    esac

    # User-supplied overrides (word-split on whitespace for convenience).
    local -a user_cflags=() user_ldflags=()
    [[ -n "${CFLAGS:-}"  ]] && read -ra user_cflags  <<< "$CFLAGS"
    [[ -n "${LDFLAGS:-}" ]] && read -ra user_ldflags <<< "$LDFLAGS"
    if [[ ${#user_cflags[@]} -gt 0 ]]; then fl+=( "${user_cflags[@]}" ); fi
    if [[ ${#user_ldflags[@]} -gt 0 ]]; then fl+=( "${user_ldflags[@]}" ); fi

    # Apply WERROR=0 escape hatch last so it wins over -Werror.
    fl=( $(demote_werror "${fl[@]}") )

    local t0=$SECONDS
    run_cc "${fl[@]}" -o "$OUT" "${SOURCES[@]}"

    [[ -x "$OUT" ]] || die "build succeeded but $OUT is not executable"
    local sz
    sz=$(du -h "$OUT" | cut -f1)
    ok "built $OUT (${sz}, $((SECONDS - t0))s)"
}

cmd_test() {
    preflight

    # Build debug first so test binaries inherit ASAN + UBSAN.
    cmd_build debug

    local rc=0 found=0

    if [[ -d "$ROOT/tests" ]]; then
        # C unit tests
        local -a tc=( "$ROOT"/tests/*_test.c )
        if [[ -e "${tc[0]}" ]]; then
            found=1
            local -a tfl=( $(flags_strict) $(flags_sanitize) -I"$SRC" )
            tfl=( $(demote_werror "${tfl[@]}") )
            local t bin
            for t in "${tc[@]}"; do
                bin=$(mktemp /tmp/swsc-test-XXXXXX)
                info "test: ${t#"$ROOT"/}"
                if ! run_cc "${tfl[@]}" -o "$bin" "$t"; then
                    err "test build failed: $t"; rc=1
                elif ! "$bin"; then
                    err "test failed: $t"; rc=1
                fi
                rm -f "$bin"
            done
        fi

        # Shell tests
        local -a ts=( "$ROOT"/tests/*.sh )
        if [[ -e "${ts[0]}" ]]; then
            found=1
            local s
            for s in "${ts[@]}"; do
                info "test: ${s#"$ROOT"/}"
                if ! bash "$s"; then
                    err "test failed: $s"; rc=1
                fi
            done
        fi
    fi

    (( found )) || warn "no tests found in $ROOT/tests"
    if (( rc == 0 )); then ok "all tests passed"; else exit 1; fi
}

cmd_clean() {
    rm -f  "$OUT"
    rm -rf "$ROOT/build"
    ok "clean"
}

cmd_help() {
    cat <<'EOF'
swsc — Rust-strict build driver.

Usage:
  ./build.sh [command] [-v]

Commands:
  build, release   (default) optimized + hardened build → ./swsc
  debug            -O1 -g3 + ASAN + UBSAN → ./swsc
  check            syntax-only, no binary produced
  paranoid         release + every warning; may fail on this codebase
  test             build debug + run tests/*_test.c and tests/*.sh
  clean            remove build artifacts
  help             this message

Environment:
  CC               C compiler (default: cc → gcc → clang)
  CFLAGS           extra CFLAGS, appended last
  LDFLAGS          extra LDFLAGS, appended last
  WERROR=0         demote -Werror to warnings (local iteration only)
  NO_COLOR         disable ANSI colors (any non-empty value)
  V=1              print full compile command

Exit codes:
  0  success
  1  compile / test failure
  2  usage / preflight failure
EOF
}

# ---------- dispatch ----------

main() {
    local cmd=""
    local arg

    for arg in "$@"; do
        case "$arg" in
            -v|--verbose) V=1 ;;
            -h|--help)    cmd=help ;;
            build|release|debug|paranoid|check|test|clean|help)
                [[ -n "$cmd" ]] && die "duplicate command: $arg (already had $cmd)"
                cmd="$arg"
                ;;
            *)
                die "unknown argument: $arg (try: $SCRIPT help)"
                ;;
        esac
    done

    [[ -z "$cmd" ]] && cmd=build

    case "$cmd" in
        build|release)  cmd_build release  ;;
        debug)          cmd_build debug    ;;
        paranoid)       cmd_build paranoid ;;
        check)          cmd_check          ;;
        test)           cmd_test           ;;
        clean)          cmd_clean          ;;
        help)           cmd_help           ;;
        *)              die "unknown command: $cmd" ;;
    esac
}

main "$@"