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

FIRST-RUN SETUP (required once per screen)
-------------------------------------------
Each MAME instance needs to know which screen it represents.
Run the setup script to configure them one at a time:

  Linux / macOS:
    chmod +x ridgeracf-3screen.sh     (first time only)
    ./ridgeracf-3screen.sh setup

  Windows:
    ridgeracf-3screen.bat setup

A window will open for each screen in turn. In each window:
  1. Press Tab to open the MAME menu
  2. Go to DIP Switches
  3. Set "PCB Role (3-Screen)" to the value shown in the terminal:
       - First window:   Center (master)
       - Second window:  Right Screen (forwarder -- relays to left)
       - Third window:   Left Screen (slave -- receive only)
  4. Close the window — the next one will open automatically

Setup only needs to be done once. Settings are saved in multiplay/.


RUNNING
-------
  Linux / macOS:
    ./ridgeracf-3screen.sh

  Windows:
    ridgeracf-3screen.bat

Three windows will open in order: center, right, left.
Physical screen layout:  [ LEFT ] [ CENTER ] [ RIGHT ]

Press Ctrl+C (Linux/macOS) or close all windows (Windows) to stop.


NETWORK TOPOLOGY
----------------
The three instances communicate over TCP on localhost:

  Center (master)       Right (forwarder)     Left (slave)
  port 15112            port 15113            port 15111
       |                     |                     |
       +-------------------->+-------------------->+
                                                   |
       <-------------------------------------------+

Ports 15111-15113 must be free on localhost.


===========================================================================
RIDGE RACER THREE MONITOR VERSION (ridgerac3m)
===========================================================================

FIRST-RUN SETUP (required once per screen)
-------------------------------------------
ridgerac3m uses physical DIP switches (SW2:1 and SW2:2) to identify each
PCB, rather than a Machine Configuration setting.

Run the setup script to configure them one at a time:

  Linux / macOS:
    chmod +x ridgerac3m-3screen.sh    (first time only)
    ./ridgerac3m-3screen.sh setup

A window will open for each screen in turn. In each window:
  1. Press Tab to open the MAME menu
  2. Go to DIP Switches
  3. Set SW2:1 and SW2:2 to the values shown in the terminal:
       - First window  (center): SW2:1=OFF, SW2:2=ON
       - Second window (right):  SW2:1=OFF, SW2:2=OFF
       - Third window  (left):   SW2:1=ON,  SW2:2=OFF
  4. Close the window — the next one will open automatically

DIP switch to role mapping (active-low: bit=1 means switch OFF):

  SW2:1  SW2:2  Role
  -----  -----  ----
  OFF    ON     Center/Main (PCB 2)  [default]
  ON     OFF    Left        (PCB 1)
  OFF    OFF    Right       (PCB 3)

Setup only needs to be done once. Settings are saved in multiplay/.


RUNNING
-------
  Linux / macOS:
    ./ridgerac3m-3screen.sh

Three windows will open in order: center, right, left.
Physical screen layout:  [ LEFT ] [ CENTER ] [ RIGHT ]

Press Ctrl+C to stop all instances.


NETWORK TOPOLOGY
----------------
  Center (master)       Right (forwarder)     Left (slave)
  port 15112            port 15113            port 15111
       |                     |                     |
       +-------------------->+-------------------->+
                                                   |
       <-------------------------------------------+

Ports 15111-15113 must be free on localhost.


===========================================================================
TROUBLESHOOTING (both games)
===========================================================================

  "Unknown system" error
    Rebuild the binary: make SUBTARGET=namcos22

  "ROM not found"
    Ensure the ROM files are in the correct roms/ subdirectory.

  Windows: black screen or immediate exit (ridgeracf)
    Run setup mode first to configure PCB Role for each screen.

  ridgerac3m side screens show black
    C139 linking must be active. Verify DIP switch settings and that
    all three instances are running and connected on ports 15121-15123.

  Screens do not sync / game freezes on attract
    Check PCB role / DIP switch settings — each screen must be unique.
    Re-run setup mode if needed.

  macOS: "mamenamcos22 cannot be opened because it is from an unidentified developer"
    Right-click mamenamcos22 -> Open -> Open (first time only).
