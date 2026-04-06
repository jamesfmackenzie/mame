#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
if [[ -n "${MAME_BIN:-}" ]]; then
	MAME_BIN="$MAME_BIN"
else
	for candidate in \
		"$ROOT_DIR/mame" \
		"$ROOT_DIR/arcade" \
		"$ROOT_DIR/mess" \
		"$ROOT_DIR/build/mame" \
		"$ROOT_DIR/build/arcade" \
		"$ROOT_DIR/build/mess"
	do
		if [[ -x "$candidate" ]]; then
			MAME_BIN="$candidate"
			break
		fi
	done
fi
GAME="${GAME:-ridgeracf}"
WINDOW_GEOMETRY="${WINDOW_GEOMETRY:-640x480}"
# Video backend: "soft" or "accel" avoids the macOS/Apple OpenGL compositor
# freeze observed with "opengl".  Set VIDEO=opengl to revert to the old path.
VIDEO="${VIDEO:-soft}"
WAIT_TIMEOUT="${WAIT_TIMEOUT:-30}"
ROM_PATH="${ROM_PATH:-$ROOT_DIR/roms}"

if [[ ! -x "$MAME_BIN" ]]; then
	echo "MAME binary not found or not executable." >&2
	echo "Set MAME_BIN to your built executable if it is not in a standard location." >&2
	exit 1
fi

mkdir -p \
	"$ROOT_DIR/cfg_rrf_l" \
	"$ROOT_DIR/nvram_rrf_l" \
	"$ROOT_DIR/sta_rrf_l" \
	"$ROOT_DIR/diff_rrf_l" \
	"$ROOT_DIR/cfg_rrf_c" \
	"$ROOT_DIR/nvram_rrf_c" \
	"$ROOT_DIR/sta_rrf_c" \
	"$ROOT_DIR/diff_rrf_c" \
	"$ROOT_DIR/cfg_rrf_r" \
	"$ROOT_DIR/nvram_rrf_r" \
	"$ROOT_DIR/sta_rrf_r" \
	"$ROOT_DIR/diff_rrf_r"

: >"$ROOT_DIR/rrf_left.log"
: >"$ROOT_DIR/rrf_center.log"
: >"$ROOT_DIR/rrf_right.log"

wait_for_log() {
	local logfile="$1"
	local pattern="$2"
	local label="$3"
	local timeout="${4:-$WAIT_TIMEOUT}"
	local waited=0

	while (( waited < timeout )); do
		if grep -Fq "$pattern" "$logfile" 2>/dev/null; then
			echo "  $label ready"
			return 0
		fi
		sleep 1
		((waited += 1))
	done

	echo "  timed out waiting for $label ($pattern in ${logfile:t})" >&2
	return 1
}

echo "Ridge Racer Full Scale link launcher"
echo "Role mapping:"
echo "  LEFT   = PCB 1 = local 15111 -> remote 15112"
echo "  CENTER = PCB 2 = local 15112 -> remote 15113"
echo "  RIGHT  = PCB 3 = local 15113 -> remote 15111"
echo
echo "Set PCB (screen) Select in each instance to match:"
echo "  first window  -> Left (PCB 1)"
echo "  second window -> Centre/Main (PCB 2)"
echo "  third window  -> Right (PCB 3)"
echo
echo "Logs:"
echo "  $ROOT_DIR/rrf_left.log"
echo "  $ROOT_DIR/rrf_center.log"
echo "  $ROOT_DIR/rrf_right.log"
echo "MAME binary:"
echo "  $MAME_BIN"
echo "Window size:"
echo "  $WINDOW_GEOMETRY"
echo "Video backend:"
echo "  $VIDEO  (override with VIDEO=opengl to test OpenGL path)"
echo "Startup wait:"
echo "  waiting for current C139COMM log messages from this tree"
echo

echo "Launching LEFT / PCB 1..."
MAME_WINDOW_TAG=LEFT "$MAME_BIN" "$GAME" -window -skip_gameinfo -verbose \
	-video "$VIDEO" \
	-resolution "$WINDOW_GEOMETRY" \
	-rompath "$ROM_PATH" \
	-cfg_directory "$ROOT_DIR/cfg_rrf_l" \
	-nvram_directory "$ROOT_DIR/nvram_rrf_l" \
	-state_directory "$ROOT_DIR/sta_rrf_l" \
	-diff_directory "$ROOT_DIR/diff_rrf_l" \
	-comm_localhost 0.0.0.0 -comm_localport 15111 \
	-comm_remotehost 127.0.0.1 -comm_remoteport 15112 \
	"$@" >"$ROOT_DIR/rrf_left.log" 2>&1 &

PID1=$!
echo "  PID $PID1"
wait_for_log "$ROOT_DIR/rrf_left.log" "C139COMM: listen on" "LEFT listener"

echo "Launching CENTER / PCB 2..."
MAME_WINDOW_TAG=CENTER "$MAME_BIN" "$GAME" -window -skip_gameinfo -verbose \
	-video "$VIDEO" \
	-resolution "$WINDOW_GEOMETRY" \
	-rompath "$ROM_PATH" \
	-cfg_directory "$ROOT_DIR/cfg_rrf_c" \
	-nvram_directory "$ROOT_DIR/nvram_rrf_c" \
	-state_directory "$ROOT_DIR/sta_rrf_c" \
	-diff_directory "$ROOT_DIR/diff_rrf_c" \
	-comm_localhost 0.0.0.0 -comm_localport 15112 \
	-comm_remotehost 127.0.0.1 -comm_remoteport 15113 \
	"$@" >"$ROOT_DIR/rrf_center.log" 2>&1 &

PID2=$!
echo "  PID $PID2"
wait_for_log "$ROOT_DIR/rrf_center.log" "C139COMM: listen on" "CENTER listener"

echo "Launching RIGHT / PCB 3..."
MAME_WINDOW_TAG=RIGHT "$MAME_BIN" "$GAME" -window -skip_gameinfo -verbose \
	-video "$VIDEO" \
	-resolution "$WINDOW_GEOMETRY" \
	-rompath "$ROM_PATH" \
	-cfg_directory "$ROOT_DIR/cfg_rrf_r" \
	-nvram_directory "$ROOT_DIR/nvram_rrf_r" \
	-state_directory "$ROOT_DIR/sta_rrf_r" \
	-diff_directory "$ROOT_DIR/diff_rrf_r" \
	-comm_localhost 0.0.0.0 -comm_localport 15113 \
	-comm_remotehost 127.0.0.1 -comm_remoteport 15111 \
	"$@" >"$ROOT_DIR/rrf_right.log" 2>&1 &

PID3=$!
echo "  PID $PID3"
wait_for_log "$ROOT_DIR/rrf_right.log" "C139COMM: listen on" "RIGHT listener"
echo
echo "Tail logs with:"
echo "  tail -f $ROOT_DIR/rrf_left.log"
echo "  tail -f $ROOT_DIR/rrf_center.log"
echo "  tail -f $ROOT_DIR/rrf_right.log"

cleanup() {
	kill "$PID1" "$PID2" "$PID3" 2>/dev/null || true
}

trap cleanup INT TERM
wait "$PID1" "$PID2" "$PID3"
