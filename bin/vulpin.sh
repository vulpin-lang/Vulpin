#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"
BINARY="$SRC_DIR/vulpin"

if [[ ! -x "$BINARY" ]]; then
    echo "[vulpin] Building from source..." >&2
    if ! command -v gcc &>/dev/null; then
        echo "[vulpin] ERROR: gcc not found." >&2
        exit 1
    fi
    (cd "$SRC_DIR" && make 2>/dev/null || gcc -O2 -o vulpin main.c lexer.c parser.c vm.c vulpin.c -lm) || {
        echo "[vulpin] ERROR: Build failed." >&2
        exit 1
    }
    echo "[vulpin] Build complete." >&2
fi

exec "$BINARY" "$@"
