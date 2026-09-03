#!/bin/sh
S="$(cd "$(dirname "$0")"&&pwd)";B="$S/../src/vulpin"
[ -x "$B" ]||{ echo "[vulpin] Building...">&2;command -v gcc>/dev/null||{ echo "gcc not found">&2;exit 1;};(cd "$S/../src"&&make 2>/dev/null||gcc -O2 -o vulpin vulpin.c vm.c -lm)||{ echo "Build failed">&2;exit 1;};echo "Done">&2;}
exec "$B" "$@"
