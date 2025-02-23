#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/signal-message-bus-response.XXXXXX")
OUT="$TEST_TMP/server.out"
ERR="$TEST_TMP/server.err"
CLIENT_ERR="$TEST_TMP/client.err"
SERVER_PID=

cleanup()
{
	if [ -n "$SERVER_PID" ]; then
		kill "$SERVER_PID" 2>/dev/null || true
		wait "$SERVER_PID" 2>/dev/null || true
	fi
	rm -rf "$TEST_TMP"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

"$ROOT/tests/response_server" >"$OUT" 2>"$ERR" &
SERVER_PID=$!
tries=0
while [ "$tries" -lt 50 ] && ! grep -qx "$SERVER_PID" "$OUT" 2>/dev/null; do
	if ! kill -0 "$SERVER_PID" 2>/dev/null; then
		printf 'response server exited before becoming ready\n' >&2
		exit 1
	fi
	tries=$((tries + 1))
	sleep 0.1
done
if ! grep -qx "$SERVER_PID" "$OUT"; then
	printf 'response server did not become ready\n' >&2
	exit 1
fi

"$ROOT/client" "$SERVER_PID" probe 2>"$CLIENT_ERR"
wait "$SERVER_PID"
SERVER_PID=

if [ -s "$CLIENT_ERR" ] || [ -s "$ERR" ]; then
	cat "$CLIENT_ERR" "$ERR" >&2
	exit 1
fi
{
	printf '%s\n' "$(sed -n '1p' "$OUT")"
	printf 'probe\n'
} >"$TEST_TMP/expected"
diff -u "$TEST_TMP/expected" "$OUT"
