#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/signal-message-bus-output.XXXXXX")
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

wait_ready()
{
	out=$1
	tries=0
	while [ "$tries" -lt 50 ] && ! grep -qx "$SERVER_PID" "$out" 2>/dev/null; do
		if ! kill -0 "$SERVER_PID" 2>/dev/null; then
			printf 'fault server exited before publishing its pid\n' >&2
			exit 1
		fi
		tries=$((tries + 1))
		sleep 0.1
	done
	grep -qx "$SERVER_PID" "$out"
}

STARTUP_OUT="$TEST_TMP/startup.out"
STARTUP_ERR="$TEST_TMP/startup.err"
MT_TEST_ZERO_ONCE=1 "$ROOT/tests/fault_server" >"$STARTUP_OUT" 2>"$STARTUP_ERR" &
STARTUP_PID=$!
startup_status=0
wait "$STARTUP_PID" || startup_status=$?
if [ "$startup_status" -eq 0 ] || [ -s "$STARTUP_OUT" ]; then
	printf 'zero-length startup write did not fail\n' >&2
	exit 1
fi
grep -qx 'server: failed to publish pid' "$STARTUP_ERR"
if [ -e "/tmp/signal-message-bus-$(id -u)/server-$STARTUP_PID.sock" ]; then
	printf 'failed startup left a response socket\n' >&2
	exit 1
fi

PARTIAL_OUT="$TEST_TMP/partial.out"
PARTIAL_ERR="$TEST_TMP/partial.err"
CLIENT_ERR="$TEST_TMP/client.err"
MT_TEST_MAX_WRITE=1 MT_TEST_EINTR_ONCE=1 \
	"$ROOT/tests/fault_server" >"$PARTIAL_OUT" 2>"$PARTIAL_ERR" &
SERVER_PID=$!
wait_ready "$PARTIAL_OUT"
"$ROOT/client" "$SERVER_PID" partial 2>"$CLIENT_ERR"
kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=
if [ -s "$PARTIAL_ERR" ] || [ -s "$CLIENT_ERR" ]; then
	cat "$PARTIAL_ERR" "$CLIENT_ERR" >&2
	exit 1
fi
{
	sed -n '1p' "$PARTIAL_OUT"
	printf 'partial\n'
} >"$TEST_TMP/partial.expected"
diff -u "$TEST_TMP/partial.expected" "$PARTIAL_OUT"

BYTE_OUT="$TEST_TMP/byte.out"
BYTE_ERR="$TEST_TMP/byte.err"
BYTE_CLIENT_ERR="$TEST_TMP/byte-client.err"
MT_TEST_FAIL_BYTE=X MT_TEST_FAIL_EPIPE=1 \
	"$ROOT/tests/fault_server" >"$BYTE_OUT" 2>"$BYTE_ERR" &
SERVER_PID=$!
wait_ready "$BYTE_OUT"
client_status=0
"$ROOT/client" "$SERVER_PID" X 2>"$BYTE_CLIENT_ERR" || client_status=$?
server_status=0
wait "$SERVER_PID" || server_status=$?
SERVER_PID=
if [ "$client_status" -eq 0 ] || [ "$server_status" -eq 0 ]; then
	printf 'output failure was acknowledged as success\n' >&2
	exit 1
fi
[ "$(wc -l <"$BYTE_OUT")" -eq 1 ]
grep -qx 'server: signal event channel failed' "$BYTE_ERR"
grep -qx 'client: timed out waiting for acknowledgement' "$BYTE_CLIENT_ERR"

NEWLINE_OUT="$TEST_TMP/newline.out"
NEWLINE_ERR="$TEST_TMP/newline.err"
NEWLINE_CLIENT_ERR="$TEST_TMP/newline-client.err"
MT_TEST_FAIL_NEWLINE_NUMBER=2 MT_TEST_FAIL_EPIPE=1 \
	"$ROOT/tests/fault_server" >"$NEWLINE_OUT" 2>"$NEWLINE_ERR" &
SERVER_PID=$!
wait_ready "$NEWLINE_OUT"
client_status=0
"$ROOT/client" "$SERVER_PID" "" 2>"$NEWLINE_CLIENT_ERR" || client_status=$?
server_status=0
wait "$SERVER_PID" || server_status=$?
SERVER_PID=
if [ "$client_status" -eq 0 ] || [ "$server_status" -eq 0 ]; then
	printf 'terminating newline failure was acknowledged as success\n' >&2
	exit 1
fi
[ "$(wc -l <"$NEWLINE_OUT")" -eq 1 ]
grep -qx 'server: signal event channel failed' "$NEWLINE_ERR"
grep -qx 'client: timed out waiting for acknowledgement' "$NEWLINE_CLIENT_ERR"
