#!/usr/bin/env bash
set -euo pipefail

CC=${CC:-gcc}
CFLAGS=${CFLAGS:--std=c11 -O2 -Wall -Wextra -Wno-unused-parameter}

$CC $CFLAGS -o swsc \
    tools/util.c \
    tools/ast.c \
    tools/lexer.c \
    tools/parser.c \
    tools/codegen.c \
    tools/main.c

echo "✔ built ./swsc"