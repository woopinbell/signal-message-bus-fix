#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/signal-message-bus-protocol.XXXXXX")
OUT="$TEST_TMP/server.out"
ERR="$TEST_TMP/server.err"
CLIENT_ERR="$TEST_TMP/client.err"
SERVER_PID=
CLIENT_PID=
UNRELATED_PID=

cleanup()
{
	if [ -n "$UNRELATED_PID" ]; then
		kill "$UNRELATED_PID" 2>/dev/null || true
		wait "$UNRELATED_PID" 2>/dev/null || true
	fi
	if [ -n "$CLIENT_PID" ]; then
		kill -CONT "$CLIENT_PID" 2>/dev/null || true
		kill "$CLIENT_PID" 2>/dev/null || true
		wait "$CLIENT_PID" 2>/dev/null || true
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

wait_ready()
{
	file=$1
	tries=0
	while [ "$tries" -lt 50 ] && ! grep -qx "$SERVER_PID" "$file" 2>/dev/null; do
		if ! kill -0 "$SERVER_PID" 2>/dev/null; then
			printf 'server exited before becoming ready\n' >&2
			exit 1
		fi
		tries=$((tries + 1))
		sleep 0.1
	done
	grep -qx "$SERVER_PID" "$file"
}

UNRELATED_ERR="$TEST_TMP/unrelated.err"
sleep 30 &
UNRELATED_PID=$!
if "$ROOT/client" "$UNRELATED_PID" unrelated 2>"$UNRELATED_ERR"; then
	printf 'client accepted a process without an active server\n' >&2
	exit 1
fi
grep -qx 'client: invalid server pid' "$UNRELATED_ERR"
kill -0 "$UNRELATED_PID"
kill "$UNRELATED_PID"
wait "$UNRELATED_PID" 2>/dev/null || true
UNRELATED_PID=

STALE_UNRELATED_ERR="$TEST_TMP/stale-unrelated.err"
"$ROOT/tests/stale_server_exec" "$ROOT/client" unrelated \
	2>"$STALE_UNRELATED_ERR"
grep -qx 'client: failed to send signal' "$STALE_UNRELATED_ERR"

FLOOD_OUT="$TEST_TMP/flood.out"
FLOOD_ERR="$TEST_TMP/flood.err"
FLOOD_CLIENT_ERR="$TEST_TMP/flood-client.err"
MT_TEST_INVALID_FLOOD=1 "$ROOT/tests/response_server" \
	>"$FLOOD_OUT" 2>"$FLOOD_ERR" &
SERVER_PID=$!
wait_ready "$FLOOD_OUT"
flood_started=$(date +%s)
flood_status=0
"$ROOT/client" "$SERVER_PID" flood 2>"$FLOOD_CLIENT_ERR" || flood_status=$?
flood_elapsed=$(( $(date +%s) - flood_started ))
[ "$flood_status" -ne 0 ]
grep -qx 'client: timed out waiting for acknowledgement' "$FLOOD_CLIENT_ERR"
[ "$flood_elapsed" -ge 2 ]
[ "$flood_elapsed" -le 6 ]
wait "$SERVER_PID"
SERVER_PID=
[ ! -s "$FLOOD_ERR" ]

STALE_SERVER_OUT="$TEST_TMP/stale-server.out"
STALE_SERVER_ERR="$TEST_TMP/stale-server.err"
"$ROOT/tests/stale_server_exec" "$ROOT/server" >"$STALE_SERVER_OUT" 2>"$STALE_SERVER_ERR"
[ ! -s "$STALE_SERVER_OUT" ]
grep -qx 'server: failed to create response channel' "$STALE_SERVER_ERR"

RUNTIME_DIR="/tmp/signal-message-bus-$(id -u)"
"$ROOT/server" >"$OUT" 2>"$ERR" &
SERVER_PID=$!
wait_ready "$OUT"
if [ "$(uname -s)" = Darwin ]; then
	runtime_mode=$(stat -f '%Lp' "$RUNTIME_DIR")
else
	runtime_mode=$(stat -c '%a' "$RUNTIME_DIR")
fi
[ "$runtime_mode" = 700 ]

"$ROOT/tests/stale_exec" "$ROOT/client" "$SERVER_PID" stale socket \
	2>"$TEST_TMP/stale.err"
[ ! -s "$TEST_TMP/stale.err" ]
"$ROOT/tests/stale_exec" "$ROOT/client" "$SERVER_PID" blocked file \
	2>"$TEST_TMP/file.err"
grep -qx 'client: failed to create response channel' "$TEST_TMP/file.err"

LONG_MESSAGE=$(awk 'BEGIN { for (i = 0; i < 16384; i++) printf "z" }')
"$ROOT/client" "$SERVER_PID" "$LONG_MESSAGE" 2>"$CLIENT_ERR" &
CLIENT_PID=$!
sleep 0.05
kill -STOP "$CLIENT_PID"
sleep 0.1
kill -CONT "$CLIENT_PID"
wait "$CLIENT_PID"
CLIENT_PATH="$RUNTIME_DIR/client-$CLIENT_PID.sock"
CLIENT_PID=
[ ! -e "$CLIENT_PATH" ]
[ ! -s "$CLIENT_ERR" ]

SERVER_PATH="$RUNTIME_DIR/server-$SERVER_PID.sock"
kill -TERM "$SERVER_PID"
server_status=0
wait "$SERVER_PID" || server_status=$?
SERVER_PID=
[ "$server_status" -eq 143 ]
[ ! -e "$SERVER_PATH" ]
[ ! -s "$ERR" ]
{
	sed -n '1p' "$OUT"
	printf 'stale\n'
	printf '%s\n' "$LONG_MESSAGE"
} >"$TEST_TMP/expected"
diff -u "$TEST_TMP/expected" "$OUT"

OVERFLOW_OUT="$TEST_TMP/overflow.out"
OVERFLOW_ERR="$TEST_TMP/overflow.err"
OVERFLOW_CLIENT_ERR="$TEST_TMP/overflow-client.err"
MT_TEST_EVENT_EAGAIN=1 "$ROOT/tests/fault_server" \
	>"$OVERFLOW_OUT" 2>"$OVERFLOW_ERR" &
SERVER_PID=$!
wait_ready "$OVERFLOW_OUT"
overflow_client_status=0
"$ROOT/client" "$SERVER_PID" overflow 2>"$OVERFLOW_CLIENT_ERR" \
	|| overflow_client_status=$?
overflow_server_status=0
wait "$SERVER_PID" || overflow_server_status=$?
OVERFLOW_PATH="$RUNTIME_DIR/server-$SERVER_PID.sock"
SERVER_PID=
[ "$overflow_client_status" -ne 0 ]
[ "$overflow_server_status" -ne 0 ]
grep -qx 'server: signal event channel failed' "$OVERFLOW_ERR"
grep -qx 'client: timed out waiting for acknowledgement' "$OVERFLOW_CLIENT_ERR"
[ ! -e "$OVERFLOW_PATH" ]
