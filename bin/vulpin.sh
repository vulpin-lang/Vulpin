#!/bin/bash

SOURCE="${BASH_SOURCE[0]}"
while [ -L "$SOURCE" ]; do
  DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
ORIG_DIR="$(pwd)"
cd "$SCRIPT_DIR/.." || exit 1
PROJECT_ROOT="$(pwd)"
SRC_FILE="$PROJECT_ROOT/src/vulpin.rs"
EXE_FILE="$PROJECT_ROOT/vulpin_bin"
if [ ! -f "$SRC_FILE" ]; then
    echo "Error: Source file not found at $SRC_FILE"
    exit 1
fi
if [ $# -eq 0 ]; then
    echo "Usage: vulpin [command] [args...]"
    echo "Example: vulpin app.vul"
    exit 0
fi
ARGS=("$@")
if [ -f "$ORIG_DIR/${ARGS[0]}" ]; then
    ARGS[0]="$ORIG_DIR/${ARGS[0]}"
fi
rustc -C opt-level=3 "$SRC_FILE" -o "$EXE_FILE"

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi
"$EXE_FILE" "${ARGS[@]}"
