#!/usr/bin/env bash
# ridgeracf-3screen.sh
#
# Launch three ridgeracf instances for 3-screen play.
#
# Ring topology:
#
#   Center (master)      Right (forwarder)    Left (slave)
#   localport  15112     localport  15113     localport  15111
#   remoteport 15113 --> remoteport 15111 --> remoteport 15112
#
# Usage:
#   ./ridgeracf-3screen.sh            normal run (all 3 instances)
#   ./ridgeracf-3screen.sh setup      first-run: opens one window at a time so
#                                     you can set the PCB Role in each before
#                                     the next opens
#   ./ridgeracf-3screen.sh verbose    all instances get -verbose (network output in logs)

set -euo pipefail

# Detect binary name (macOS and Linux both produce 'namcos22')
BINARY=./mamenamcos22

GAME=ridgeracf
LOG_DIR=multiplay/logs
MODE="${1:-run}"

# ---- sanity checks --------------------------------------------------------

if [[ ! -x "$BINARY" ]]; then
    echo "error: $BINARY not found." >&2
    echo "       This script must be run from the same directory as the namcos22 binary." >&2
    exit 1
fi

if [[ ! -d roms ]]; then
    echo "error: roms/ directory not found." >&2
    echo "       This script must be run from the same directory as the roms/ folder." >&2
    exit 1
fi

mkdir -p multiplay/center multiplay/right multiplay/left "$LOG_DIR"

# ---- port assignments -----------------------------------------------------

CENTER_LOCAL=15112;  CENTER_REMOTE=15113
RIGHT_LOCAL=15113;   RIGHT_REMOTE=15111
LEFT_LOCAL=15111;    LEFT_REMOTE=15112

# ---- window size ----------------------------------------------------------

WINDOW_OPTS=(-window -resolution 640x480)

# ---- cleanup on exit ------------------------------------------------------

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

# ---- launch helper --------------------------------------------------------

launch() {
    local role="$1"; local lport="$2"; local rport="$3"
    shift 3
    "$BINARY" "$GAME" \
        "${WINDOW_OPTS[@]}" \
        -rompath roms \
        -cfg_directory  "multiplay/${role}" \
        -nvram_directory "multiplay/${role}" \
        -comm_localhost  127.0.0.1 \
        -comm_localport  "${lport}" \
        -comm_remotehost 127.0.0.1 \
        -comm_remoteport "${rport}" \
        "$@" \
        > "$LOG_DIR/${role}.log" 2>&1 &
    local pid=$!
    PIDS+=("$pid")
    echo "  ${role} (pid ${pid})  localport=${lport}  -->  remoteport=${rport}  log: $LOG_DIR/${role}.log"
}

# ==========================================================================
# SETUP MODE
# ==========================================================================

if [[ "$MODE" == "setup" ]]; then
    cat <<'MSG'
FIRST-RUN SETUP
---------------
Each window will open one at a time. In each one:
  1. Press Tab to open the MAME menu
  2. Go to Machine Configuration
  3. Set "PCB Role (3-Screen)" to the value shown below
  4. Close the window to continue to the next

MSG

    echo "--- Opening CENTER window ---"
    echo "    Set PCB Role -> 'Center (master)'"
    launch center "$CENTER_LOCAL" "$CENTER_REMOTE"
    wait "${PIDS[${#PIDS[@]}-1]}"
    echo "Center config saved."
    echo ""

    echo "--- Opening RIGHT SCREEN window ---"
    echo "    Set PCB Role -> 'Right Screen (forwarder -- relays to left)'"
    launch right "$RIGHT_LOCAL" "$RIGHT_REMOTE"
    wait "${PIDS[${#PIDS[@]}-1]}"
    echo "Right screen config saved."
    echo ""

    echo "--- Opening LEFT SCREEN window ---"
    echo "    Set PCB Role -> 'Left Screen (slave -- receive only)'"
    launch left "$LEFT_LOCAL" "$LEFT_REMOTE"
    wait "${PIDS[${#PIDS[@]}-1]}"
    echo "Left screen config saved."
    echo ""
    echo "Setup complete. Run ./ridgeracf-3screen.sh to start all three."
    exit 0
fi

# ==========================================================================
# NORMAL / VERBOSE RUN
# ==========================================================================

EXTRA_ALL=()
if [[ "$MODE" == "verbose" ]]; then
    EXTRA_ALL=(-verbose)
    echo "Verbose mode: network output in $LOG_DIR/*.log"
    echo ""
fi

rm -f "$LOG_DIR"/*.log

cat <<MSG
Starting ridgeracf 3-screen
  Physical layout:  [ LEFT ] [ CENTER ] [ RIGHT ]

MSG

launch center "$CENTER_LOCAL" "$CENTER_REMOTE" ${EXTRA_ALL[@]+"${EXTRA_ALL[@]}"}
sleep 0.5
launch right  "$RIGHT_LOCAL"  "$RIGHT_REMOTE"  ${EXTRA_ALL[@]+"${EXTRA_ALL[@]}"}
sleep 0.5
launch left   "$LEFT_LOCAL"   "$LEFT_REMOTE"   ${EXTRA_ALL[@]+"${EXTRA_ALL[@]}"}

echo ""
echo "All instances running. Ctrl+C to stop all."
echo ""

wait "${PIDS[@]}" 2>/dev/null || true
