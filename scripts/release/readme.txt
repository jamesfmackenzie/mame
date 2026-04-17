Ridge Racer Full Scale - 3-Screen Setup
========================================

CONTENTS
--------
  namcos22 / namcos22.exe   MAME emulator (namcos22 subtarget)
  ridgeracf-3screen.sh      Triple-screen launcher (Linux / macOS)
  ridgeracf-3screen.bat     Triple-screen launcher (Windows)
  roms/                     Place your ROM files here
  readme.txt                This file


REQUIREMENTS
------------
  - ROM set for Ridge Racer Full Scale (ridgeracf)
    Place the ridgeracf zip file inside the roms/ folder.

  - Linux only: SDL2 runtime libraries
    Ubuntu/Debian:  sudo apt-get install libsdl2-2.0-0 libsdl2-ttf-2.0-0
    Fedora/RHEL:    sudo dnf install SDL2 SDL2_ttf


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
  2. Go to Machine Configuration
  3. Set "PCB Role (3-Screen)" to the value shown in the terminal:
       - First window:   Center (master)
       - Second window:  Right Screen (forwarder)
       - Third window:   Left Screen (slave)
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


TROUBLESHOOTING
---------------
  "ROM not found"
    Ensure the ridgeracf ROM zip is in the roms/ folder.

  Windows: black screen or immediate exit
    Run setup mode first to configure PCB Role for each screen.

  Screens do not sync / game freezes on attract
    Check PCB Role settings — each screen must have a unique role.
    Re-run setup mode if needed.

  macOS: "namcos22 cannot be opened because it is from an unidentified developer"
    Right-click namcos22 -> Open -> Open (first time only).
