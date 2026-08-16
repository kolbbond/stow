#!/bin/sh
# The public API must not leak X11. A consumer compiles against include/ alone,
# with no X11 include path - if an X11 header appears here, that breaks, and so
# does the whole point of the library boundary.
set -eu

root="${1:?usage: $0 <source-root>}"

# win32*.hpp are unbuilt legacy files kept for a future Win32 backend; they are
# not part of the public API surface and are excluded deliberately.
hits=$(grep -rn '#[[:space:]]*include[[:space:]]*[<"]X11/' "$root/include" \
       --exclude='win32*.hpp' || true)

if [ -n "$hits" ]; then
	echo "FAIL: X11 includes found under include/:" >&2
	echo "$hits" >&2
	exit 1
fi

echo "OK: no X11 includes under include/"
