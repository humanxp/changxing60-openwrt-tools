#!/bin/sh
# Assemble the portrait UI (base screen + overlay) and cross-compile it for the 7552N panel.
# Requires python3 and zig (set ZIG=/path/to/zig if it is not on PATH).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}

python3 "$here/patch.py"
cd "$here/build"
"$ZIG" cc -target aarch64-linux-musl -O2 -static -s -I. \
	-o screen changxing60-screen.c qr.c -lm
echo "built: $here/build/screen"
