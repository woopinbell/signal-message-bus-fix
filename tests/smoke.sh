#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/tests/.server.out"
EXPECTED="$ROOT/tests/.expected.out"
ERR="$ROOT/tests/.client.err"
SERVER_PID=

cleanup()
{
	if [ -n "$SERVER_PID" ]; then
		kill "$SERVER_PID" 2>/dev/null || true
		wait "$SERVER_PID" 2>/dev/null || true
	fi
	rm -f "$OUT" "$EXPECTED" "$ERR"
}

trap cleanup EXIT HUP INT TERM

rm -f "$OUT" "$EXPECTED" "$ERR"
"$ROOT/server" >"$OUT" 2>"$ERR" &
SERVER_PID=$!

tries=0
while [ "$tries" -lt 50 ]; do
	if [ -s "$OUT" ]; then
		break
	fi
	tries=$((tries + 1))
	sleep 0.1
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
	printf 'server exited before smoke test\n' >&2
	exit 1
fi

"$ROOT/client" "$SERVER_PID" "hello"

{
	printf '%s\n' "$SERVER_PID"
	printf 'hello\n'
} >"$EXPECTED"

diff -u "$EXPECTED" "$OUT"
