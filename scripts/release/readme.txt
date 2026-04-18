Ridge Racer 3-Screen Setup
==========================
Covers: Ridge Racer Full Scale (ridgeracf) and
        Ridge Racer Three Monitor Version (ridgerac3m)

CONTENTS
--------
  mamenamcos22 / mamenamcos22.exe   MAME emulator (namcos22 subtarget)
  ridgeracf-3screen.sh      ridgeracf triple-screen launcher (Linux / macOS)
  ridgeracf-3screen.bat     ridgeracf triple-screen launcher (Windows)
  ridgerac3m-3screen.sh     ridgerac3m triple-screen launcher (Linux / macOS)
  roms/                     Place your ROM files here
  readme.txt                This file


REQUIREMENTS
------------
  - ROM set for the game you want to run:
      ridgeracf:  roms/ridgeracf/   (Ridge Racer Full Scale, RRF2)
      ridgerac3m: roms/ridgerac3m/  (Ridge Racer Three Monitor Version, RRC)

  - Linux only: SDL2 runtime libraries
    Ubuntu/Debian:  sudo apt-get install libsdl2-2.0-0 libsdl2-ttf-2.0-0
    Fedora/RHEL:    sudo dnf install SDL2 SDL2_ttf


===========================================================================
RIDGE RACER FULL SCALE (ridgeracf)
===========================================================================

FIRST-RUN DIP SWITCH SETUP (required once per screen)
------------------------------------------------------
Each MAME instance needs to know which screen it represents via its
"PCB Role (3-Screen)" DIP switch setting.  The launcher opens windows
left→center→right and prints a reminder, but you must set the DIP once:

  In each window, in the order they open:
    1. Press Tab to open the MAME menu
    2. Go to DIP Switches
    3. Set "PCB Role (3-Screen)":
         left window   → Left Screen (slave — receive only)
         center window → Center (master — generates and TX scene data)
         right window  → Right Screen (forwarder — relays to left)

Settings are saved in multiplay/ and persist across runs.


RUNNING
-------
  Linux / macOS:
    chmod +x ridgeracf-3screen.sh     (first time only)
    ./ridgeracf-3screen.sh

  Windows:
    ridgeracf-3screen.bat

Three windows open in order: left, center, right.
Each waits for the previous to be ready before starting.
Physical screen layout:  [ LEFT ] [ CENTER ] [ RIGHT ]

Press Ctrl+C (Linux/macOS) or close all windows (Windows) to stop.


NETWORK TOPOLOGY
----------------
The three instances communicate over TCP on localhost:

  Left (slave)         Center (master)      Right (forwarder)
  port 15111           port 15112           port 15113
       |                    |                    |
       <────────────────────+────────────────────+


===========================================================================
RIDGE RACER THREE MONITOR VERSION (ridgerac3m)
===========================================================================

FIRST-RUN DIP SWITCH SETUP (required once per screen)
------------------------------------------------------
ridgerac3m uses physical DIP switches (SW2:1 and SW2:2) to identify each
PCB.  The launcher opens windows left→center→right and prints a reminder:

  In each window, in the order they open:
    1. Press Tab to open the MAME menu
    2. Go to DIP Switches
    3. Set SW2:1 and SW2:2:
         left window   → SW2:1=ON,  SW2:2=OFF  (Left, PCB 1)
         center window → SW2:1=OFF, SW2:2=ON   (Center/Main, PCB 2)
         right window  → SW2:1=OFF, SW2:2=OFF  (Right, PCB 3)

DIP switch to role mapping (active-low: bit=1 means switch OFF):

  SW2:1  SW2:2  Role
  -----  -----  ----
  OFF    ON     Center/Main (PCB 2)  [default]
  ON     OFF    Left        (PCB 1)
  OFF    OFF    Right       (PCB 3)

Settings are saved in multiplay/ and persist across runs.


RUNNING
-------
  Linux / macOS:
    chmod +x ridgerac3m-3screen.sh    (first time only)
    ./ridgerac3m-3screen.sh

Three windows open in order: left, center, right.
Each waits for the previous to be ready before starting.
Physical screen layout:  [ LEFT ] [ CENTER ] [ RIGHT ]

Press Ctrl+C to stop all instances.


NETWORK TOPOLOGY
----------------
  Left (slave)         Center (master)      Right (forwarder)
  port 15111           port 15112           port 15113
       |                    |                    |
       <────────────────────+────────────────────+

Ports 15111-15113 must be free on localhost.


===========================================================================
TROUBLESHOOTING (both games)
===========================================================================

  "Unknown system" error
    Rebuild the binary: make SUBTARGET=namcos22

  "ROM not found"
    Ensure the ROM files are in the correct roms/ subdirectory.

  Windows: black screen or immediate exit (ridgeracf)
    Set the PCB Role DIP switch for each screen (see First-Run Setup above).

  ridgerac3m side screens show black
    C139 linking must be active. Verify DIP switch settings and that
    all three instances are running and connected on ports 15111-15113.

  Screens do not sync / game freezes on attract
    Check PCB role / DIP switch settings — each screen must be unique.

  Launcher times out waiting for a screen to be ready
    Check multiplay/logs/<role>.log for errors.
    Ensure ports 15111-15113 are not in use by another process.

  macOS: "mamenamcos22 cannot be opened because it is from an unidentified developer"
    Right-click mamenamcos22 -> Open -> Open (first time only).
