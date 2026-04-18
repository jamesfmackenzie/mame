#!/usr/bin/env bash
# ridgeracf-3screen.sh
#
# Launch three ridgeracf instances for 3-screen play.
# Instances start left→center→right; each waits for its TCP listener
# to be ready before the next launches, ensuring reliable connection.
#
# Ring topology:
#   Left (slave)         Center (master)      Right (forwarder)
#   localport  15111     localport  15112     localport  15113
#   remoteport 15112 <── remoteport 15113 ──► remoteport 15111
#
# DIP switch setup (Tab → DIP Switches → "PCB Role (3-Screen)"):
#   left window   → Left Screen (slave — receive only)
#   center window → Center (master — generates and TX scene data)
#   right window  → Right Screen (forwarder — relays to left)
#   Settings persist in multiplay/ after first run.
#
# Usage:
#   ./ridgeracf-3screen.sh          normal run
#   ./ridgeracf-3screen.sh debug    right screen gets -debug

set -euo pipefail

BINARY=./mamenamcos22
GAME=ridgeracf
LOG_DIR=multiplay/logs
WAIT_TIMEOUT=30

MODE="${1:-run}"

if [[ ! -x "$BINARY" ]]; then
    echo "error: $BINARY not found." >&2
    echo "       This script must be run from the same directory as mamenamcos22." >&2
    exit 1
fi

if [[ ! -d roms ]]; then
    echo "error: roms/ directory not found." >&2
    echo "       This script must be run from the same directory as the roms/ folder." >&2
    exit 1
fi

mkdir -p multiplay/center multiplay/right multiplay/left "$LOG_DIR"

CENTER_LOCAL=15112;  CENTER_REMOTE=15113
RIGHT_LOCAL=15113;   RIGHT_REMOTE=15111
LEFT_LOCAL=15111;    LEFT_REMOTE=15112

WINDOW_OPTS=(-window -resolution 640x480)
PIDS=()

cleanup() {
    echo ""
    echo "Stopping all instances..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    sleep 0.5
    for pid in "${PIDS[@]}"; do
        kill -KILL "$pid" 2>/dev/null || true
    done
    wait "${PIDS[@]}" 2>/dev/null || true
    echo "Logs saved to $LOG_DIR/"
}

trap cleanup INT TERM EXIT

wait_for_listener() {
    local logfile="$1" label="$2" waited=0
    while (( waited < WAIT_TIMEOUT )); do
        grep -Fq "C139: RX listening on" "$logfile" 2>/dev/null && { echo "  $label ready"; return 0; }
        sleep 1
        (( waited++ ))
    done
    echo "  timed out waiting for $label" >&2
    return 1
}

launch() {
    local role="$1" lport="$2" rport="$3"
    shift 3
    "$BINARY" "$GAME" \
        "${WINDOW_OPTS[@]}" \
        -verbose \
        -rompath         roms \
        -cfg_directory   "multiplay/${role}" \
        -nvram_directory "multiplay/${role}" \
        -comm_localhost  127.0.0.1 \
        -comm_localport  "${lport}" \
        -comm_remotehost 127.0.0.1 \
        -comm_remoteport "${rport}" \
        "$@" \
        > "$LOG_DIR/${role}.log" 2>&1 &
    local pid=$!
    PIDS+=("$pid")
    echo "  ${role} (pid ${pid})  localport=${lport} → remoteport=${rport}  log: $LOG_DIR/${role}.log"
}

EXTRA_RIGHT=()
[[ "$MODE" == "debug" ]] && EXTRA_RIGHT=(-debug)

rm -f "$LOG_DIR"/*.log

cat <<MSG
Starting ridgeracf 3-screen
  Physical layout:  [ LEFT ] [ CENTER ] [ RIGHT ]

DIP switch reminder (Tab → DIP Switches → "PCB Role (3-Screen)"):
  left window   → Left Screen (slave — receive only)
  center window → Center (master — generates and TX scene data)
  right window  → Right Screen (forwarder — relays to left)
  (Settings persist in multiplay/ — only needed on first run.)

MSG

echo "Launching LEFT (slave, port ${LEFT_LOCAL})..."
launch left "$LEFT_LOCAL" "$LEFT_REMOTE"
wait_for_listener "$LOG_DIR/left.log" "left"

echo "Launching CENTER (master, port ${CENTER_LOCAL})..."
launch center "$CENTER_LOCAL" "$CENTER_REMOTE"
wait_for_listener "$LOG_DIR/center.log" "center"

echo "Launching RIGHT (forwarder, port ${RIGHT_LOCAL})..."
launch right "$RIGHT_LOCAL" "$RIGHT_REMOTE" ${EXTRA_RIGHT[@]+"${EXTRA_RIGHT[@]}"}
wait_for_listener "$LOG_DIR/right.log" "right"

echo ""
echo "All instances running. Ctrl+C to stop all."
echo "Logs: tail -f $LOG_DIR/left.log  (or center.log / right.log)"
echo ""

wait "${PIDS[@]}" 2>/dev/null || true
