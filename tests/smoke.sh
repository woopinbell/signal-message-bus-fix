#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/signal-message-bus-smoke.XXXXXX")
OUT="$TEST_TMP/server.out"
EXPECTED="$TEST_TMP/expected.out"
SERVER_ERR="$TEST_TMP/server.err"
CLIENT_ERR="$TEST_TMP/client.err"
INVALID_ERR="$TEST_TMP/invalid.err"
TIMEOUT_ERR="$TEST_TMP/timeout.err"
EXPECTED_INVALID_ERR="$TEST_TMP/expected-invalid.err"
EXPECTED_TIMEOUT_ERR="$TEST_TMP/expected-timeout.err"
DUMMY_READY="$TEST_TMP/dummy.ready"
SERVER_PID=
DUMMY_PID=

send_checked()
{
	label=$1
	message=$2
	if ! "$ROOT/client" "$SERVER_PID" "$message" 2>"$CLIENT_ERR"; then
		printf 'normal client failed during %s\n' "$label" >&2
		cat "$CLIENT_ERR" >&2
		exit 1
	fi
	if [ -s "$CLIENT_ERR" ]; then
		printf 'normal client wrote to stderr during %s\n' "$label" >&2
		cat "$CLIENT_ERR" >&2
		exit 1
	fi
}

server_ready()
{
	[ -f "$OUT" ] || return 1
	[ "$(wc -l <"$OUT")" -ge 1 ] &&
	[ "$(sed -n '1p' "$OUT")" = "$SERVER_PID" ]
}

cleanup()
{
	if [ -n "$DUMMY_PID" ]; then
		kill "$DUMMY_PID" 2>/dev/null || true
		wait "$DUMMY_PID" 2>/dev/null || true
	fi
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

"$ROOT/server" >"$OUT" 2>"$SERVER_ERR" &
SERVER_PID=$!

tries=0
while [ "$tries" -lt 50 ]; do
	if server_ready; then
		break
	fi
	tries=$((tries + 1))
	sleep 0.1
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
	printf 'server exited before smoke test\n' >&2
	exit 1
fi
if ! server_ready; then
	printf 'server did not publish its complete pid line\n' >&2
	exit 1
fi

send_checked hello "hello"
send_checked empty ""
send_checked utf8 "안녕하세요"
LONG_MESSAGE=$(awk 'BEGIN { for (i = 0; i < 1024; i++) printf "x" }')
send_checked long "$LONG_MESSAGE"
send_checked final "last message"

if "$ROOT/client" 1 "bad pid" 2>"$INVALID_ERR"; then
	printf 'client accepted an invalid pid\n' >&2
	exit 1
fi
printf 'client: invalid server pid\n' >"$EXPECTED_INVALID_ERR"
diff -u "$EXPECTED_INVALID_ERR" "$INVALID_ERR"

: >"$DUMMY_READY"
(
	trap - EXIT HUP INT TERM
	trap '' USR1 USR2
	printf 'ready\n' >"$DUMMY_READY"
	exec sleep 5
) &
DUMMY_PID=$!
tries=0
while [ "$tries" -lt 50 ] && ! grep -qx 'ready' "$DUMMY_READY"; do
	if ! kill -0 "$DUMMY_PID" 2>/dev/null; then
		printf 'non-acking process exited before becoming ready\n' >&2
		exit 1
	fi
	tries=$((tries + 1))
	sleep 0.1
done
if ! grep -qx 'ready' "$DUMMY_READY"; then
	printf 'non-acking process did not become ready\n' >&2
	exit 1
fi
if "$ROOT/client" "$DUMMY_PID" "timeout" 2>"$TIMEOUT_ERR"; then
	printf 'client did not fail when acknowledgement was missing\n' >&2
	exit 1
fi
printf 'client: timed out waiting for acknowledgement\n' \
	>"$EXPECTED_TIMEOUT_ERR"
diff -u "$EXPECTED_TIMEOUT_ERR" "$TIMEOUT_ERR"
if ! kill -0 "$DUMMY_PID" 2>/dev/null; then
	printf 'non-acking process died during timeout verification\n' >&2
	exit 1
fi
kill "$DUMMY_PID" 2>/dev/null || true
wait "$DUMMY_PID" 2>/dev/null || true
DUMMY_PID=

if [ -s "$SERVER_ERR" ]; then
	printf 'server wrote to stderr\n' >&2
	cat "$SERVER_ERR" >&2
	exit 1
fi

{
	printf '%s\n' "$SERVER_PID"
	printf 'hello\n'
	printf '\n'
	printf '안녕하세요\n'
	printf '%s\n' "$LONG_MESSAGE"
	printf 'last message\n'
} >"$EXPECTED"

diff -u "$EXPECTED" "$OUT"
