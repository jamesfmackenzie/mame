#!/usr/bin/env bash
# ridgerac3m-3screen.sh
#
# Launch three ridgerac3m instances for 3-screen play.
#
# Ridge Racer (Three Monitor Version, RRC) — requires C139 link-up across 3 PCBs.
# PCB role is selected via DIP switches SW2:1 and SW2:2 (not CONF settings).
#
# Ring topology:
#
#   Center (master)      Right (forwarder)    Left (slave)
#   localport  15122     localport  15123     localport  15121
#   remoteport 15123 --> remoteport 15121 --> remoteport 15122
#
# DIP switch settings (SW2:1, SW2:2):
#   Center/Main (PCB 2):  SW2:1=OFF, SW2:2=OFF
#   Left        (PCB 1):  SW2:1=OFF, SW2:2=ON
#   Right       (PCB 3):  SW2:1=ON,  SW2:2=ON
#
# Usage:
#   ./ridgerac3m-3screen.sh            normal run (all 3 instances)
#   ./ridgerac3m-3screen.sh setup      first-run: opens one window at a time so
#                                      you can set the DIP switches in each before
#                                      the next opens
#   ./ridgerac3m-3screen.sh verbose    all instances get -verbose (network output in logs)

set -euo pipefail

# Detect binary name (macOS and Linux both produce 'namcos22')
BINARY=./mamenamcos22

GAME=ridgerac3m
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

CENTER_LOCAL=15122;  CENTER_REMOTE=15123
RIGHT_LOCAL=15123;   RIGHT_REMOTE=15121
LEFT_LOCAL=15121;    LEFT_REMOTE=15122

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
  2. Go to DIP Switches
  3. Set SW2:1 and SW2:2 to the values shown below
  4. Close the window to continue to the next

MSG

    echo "--- Opening CENTER window ---"
    echo "    Set SW2:1=OFF, SW2:2=OFF  ->  Center/Main (PCB 2)"
    launch center "$CENTER_LOCAL" "$CENTER_REMOTE"
    wait "${PIDS[${#PIDS[@]}-1]}"
    echo "Center config saved."
    echo ""

    echo "--- Opening RIGHT SCREEN window ---"
    echo "    Set SW2:1=ON,  SW2:2=ON   ->  Right (PCB 3)"
    launch right "$RIGHT_LOCAL" "$RIGHT_REMOTE"
    wait "${PIDS[${#PIDS[@]}-1]}"
    echo "Right screen config saved."
    echo ""

    echo "--- Opening LEFT SCREEN window ---"
    echo "    Set SW2:1=OFF, SW2:2=ON   ->  Left (PCB 1)"
    launch left "$LEFT_LOCAL" "$LEFT_REMOTE"
    wait "${PIDS[${#PIDS[@]}-1]}"
    echo "Left screen config saved."
    echo ""
    echo "Setup complete. Run ./ridgerac3m-3screen.sh to start all three."
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
Starting ridgerac3m 3-screen (Ridge Racer Three Monitor Version)
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
