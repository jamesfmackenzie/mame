
# **MAME** #

[![Join the chat at https://gitter.im/mamedev/mame](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/mamedev/mame?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Build status:

| OS/Compiler   | Status        | 
| ------------- |:-------------:| 
| Linux/GCC and clang | ![CI (Linux)](https://github.com/mamedev/mame/workflows/CI%20(Linux)/badge.svg) |
| Windows/MinGW GCC | ![CI (Windows)](https://github.com/mamedev/mame/workflows/CI%20(Windows)/badge.svg) |
| macOS/clang | ![CI (macOS)](https://github.com/mamedev/mame/workflows/CI%20(macOS)/badge.svg) |
| UI Translations | ![Compile UI translations](https://github.com/mamedev/mame/workflows/Compile%20UI%20translations/badge.svg) |
| Documentation | ![Build documentation](https://github.com/mamedev/mame/workflows/Build%20documentation/badge.svg) |

Static analysis status for entire build (except for third-party parts of project):

[![Coverity Scan Status](https://scan.coverity.com/projects/5727/badge.svg?flat=1)](https://scan.coverity.com/projects/mame-emulator)

Ridge Racer Full Scale 3-Screen Branch
======================================

This branch is a custom build of MAME for running **Ridge Racer Full Scale** across three linked screens on a single machine.

**Visible credit:** the Ridge Racer Full Scale changes in this branch were authored by **John Bennett**. This repo packages those changes together with the launcher and operating notes needed to bring up a 3-screen linked setup reliably.

What Is Ridge Racer Full Scale?
===============================

Ridge Racer Full Scale is the three-screen deluxe version of Ridge Racer. Instead of a single cabinet view, it uses linked left, center, and right displays to create a much wider field of view and a more cabinet-accurate presentation.

How This Custom Build Helps
===========================

This branch includes the extra pieces needed to launch three custom MAME instances and link them together for an ultra-wide Ridge Racer Full Scale experience:

* support for the required Ridge Racer Full Scale link behavior
* a dedicated 3-instance launcher script
* per-screen config, NVRAM, state, and diff directories
* window title tagging so each instance is clearly identified as `LEFT`, `CENTER`, or `RIGHT`
* logging to help confirm the link ring comes up correctly

How To Launch
=============

From the project root, run:

```sh
./scripts/run/ridgeracf-link.sh
```

The first time you launch it, configure the in-game DIP switches so each instance matches its assigned cabinet role. After that, the saved configuration will be reused.

The launcher starts three instances in this repo's actual bring-up order:

1. `LEFT`
2. `CENTER`
3. `RIGHT`

The logical communication ring is still:

`CENTER -> RIGHT -> LEFT -> CENTER`

Cabinet Role Mapping And DIP Switches
=====================================

The window title tells you which role each running instance expects. Match the cabinet-role DIP setting to the same role:

| Window tag | Cabinet role | PCB select | Local port | Remote port |
| --- | --- | --- | --- | --- |
| `LEFT` | Left | PCB 1 | `15111` | `15112` |
| `CENTER` | Center / Main | PCB 2 | `15112` | `15113` |
| `RIGHT` | Right | PCB 3 | `15113` | `15111` |

In this branch, `ridgeracf` exposes the role directly as a tri-state DIP selector named `PCB (screen) Select` in the `DSW` input port. The available values in the driver are:

| PCB (screen) Select | Use for |
| --- | --- |
| `Centre/Main (PCB 2)` | `CENTER` |
| `Left (PCB 1)` | `LEFT` |
| `Right (PCB 3)` | `RIGHT` |

Set each running instance to the role shown in its window title. Once this has been configured once, the per-instance saved configuration is reused on later launches.

If the selected PCB role does not match the window tag and port mapping, the link ring will be mis-assigned even if the network ports are correct.

How To Build
============

Build this branch the same way you would build any other version of MAME. The standard MAME build instructions below still apply.

Technical Details
=================

Code changes in this branch
===========================

At a high level, this work takes the Ridge Racer Full Scale support from the MAME 0.251-era codebase and adds the pieces needed to make a practical 3-screen linked setup usable:

* Namco `C139` link communication support used by Ridge Racer Full Scale
* Ridge Racer Full Scale driver support in `namcos22`
* window-title tagging via `MAME_WINDOW_TAG` so each instance is easy to identify during setup
* a dedicated launcher script at `scripts/run/ridgeracf-link.sh`

Ring topology
=============

The configured ring is:

`CENTER -> RIGHT -> LEFT -> CENTER`

Each instance listens on a local TCP port and forwards to the next role in the ring:

* `LEFT`: `15111 -> 15112`
* `CENTER`: `15112 -> 15113`
* `RIGHT`: `15113 -> 15111`

Parameters used by the launcher script
======================================

The launcher defaults are:

```sh
GAME=ridgeracf
WINDOW_GEOMETRY=640x480
VIDEO=soft
WAIT_TIMEOUT=30
ROM_PATH=./roms
```

Per instance, the script sets:

* `-window -skip_gameinfo -verbose`
* `-video "$VIDEO"`
* `-resolution "$WINDOW_GEOMETRY"`
* `-rompath "$ROM_PATH"`
* dedicated `cfg`, `nvram`, `sta`, and `diff` directories for each role
* `-comm_localhost 0.0.0.0`
* role-specific `-comm_localport`, `-comm_remotehost`, and `-comm_remoteport`

Extra command-line arguments passed to `scripts/run/ridgeracf-link.sh` are forwarded to all three launched MAME instances.

Startup order
=============

The script launches and waits for each listener in this order:

1. `LEFT`
2. `CENTER`
3. `RIGHT`

This is the startup order implemented by this branch. The cabinet role mapping and the communication ring shown above remain the important reference points during setup.

Diagnostic logs
===============

The launcher creates one log per role in the project root:

* `rrf_left.log`
* `rrf_center.log`
* `rrf_right.log`

These logs are used to confirm that each instance is listening and that the `C139COMM` link setup is coming up as expected. To watch them live:

```sh
tail -f rrf_left.log
tail -f rrf_center.log
tail -f rrf_right.log
```

What is MAME?
=============

MAME is a multi-purpose emulation framework.

MAME's purpose is to preserve decades of software history. As electronic technology continues to rush forward, MAME prevents this important "vintage" software from being lost and forgotten. This is achieved by documenting the hardware and how it functions. The source code to MAME serves as this documentation. The fact that the software is usable serves primarily to validate the accuracy of the documentation (how else can you prove that you have recreated the hardware faithfully?). Over time, MAME (originally stood for Multiple Arcade Machine Emulator) absorbed the sister-project MESS (Multi Emulator Super System), so MAME now documents a wide variety of (mostly vintage) computers, video game consoles and calculators, in addition to the arcade video games that were its initial focus.

How to compile?
===============

If you're on a UNIX-like system (including Linux and macOS), it could be as easy as typing

```
make
```

for a MAME build,

```
make SUBTARGET=arcade
```

for an arcade-only build, or

```
make SUBTARGET=mess
```

for a MESS build.

See the [Compiling MAME](http://docs.mamedev.org/initialsetup/compilingmame.html) page on our documentation site for more information, including prerequisites for macOS and popular Linux distributions.

For recent versions of macOS you need to install [Xcode](https://developer.apple.com/xcode/) including command-line tools and [SDL 2.0](https://www.libsdl.org/download-2.0.php).

For Windows users, we provide a ready-made [build environment](http://mamedev.org/tools/) based on MinGW-w64.

Visual Studio builds are also possible, but you still need [build environment](http://mamedev.org/tools/) based on MinGW-w64.
In order to generate solution and project files just run:

```
make vs2019
```
or use this command to build it directly using msbuild

```
make vs2019 MSBUILD=1
```


Where can I find out more?
=============

* [Official MAME Development Team Site](https://mamedev.org/) (includes binary downloads, wiki, forums, and more)
* [Official MESS Wiki](http://mess.redump.net/)
* [MAME Testers](https://mametesters.org/) (official bug tracker for MAME and MESS)


Contributing
=============

## Coding standard

MAME source code should be viewed and edited with your editor set to use four spaces per tab. Tabs are used for initial indentation of lines, with one tab used per indentation level. Spaces are used for other alignment within a line.

Some parts of the code follow [Allman style](https://en.wikipedia.org/wiki/Indent_style#Allman_style); some parts of the code follow [K&R style](https://en.wikipedia.org/wiki/Indent_style#K.26R_style) -- mostly depending on who wrote the original version. **Above all else, be consistent with what you modify, and keep whitespace changes to a minimum when modifying existing source.** For new code, the majority tends to prefer Allman style, so if you don't care much, use that.

All contributors need to either add a standard header for license info (on new files) or inform us of their wishes regarding which of the following licenses they would like their code to be made available under: the [BSD-3-Clause](http://opensource.org/licenses/BSD-3-Clause) license, the [LGPL-2.1](http://opensource.org/licenses/LGPL-2.1), or the [GPL-2.0](http://opensource.org/licenses/GPL-2.0).

See more specific [C++ Coding Guidelines](https://docs.mamedev.org/contributing/cxx.html) on our documentation web site.


License
=======

The MAME project as a whole is made available under the terms of the
[GNU General Public License, version 2](http://opensource.org/licenses/GPL-2.0)
or later (GPL-2.0+), since it contains code made available under multiple
GPL-compatible licenses.  A great majority of the source files (over 90%
including core files) are made available under the terms of the
[3-clause BSD License](http://opensource.org/licenses/BSD-3-Clause), and we
would encourage new contributors to make their contributions available under the
terms of this license.

Please note that MAME is a registered trademark of Gregory Ember, and permission
is required to use the "MAME" name, logo, or wordmark.

<a href="http://opensource.org/licenses/GPL-2.0" target="_blank">
<img align="right" src="http://opensource.org/trademarks/opensource/OSI-Approved-License-100x137.png">
</a>

    Copyright (C) 1997-2021  MAMEDev and contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2, as provided in
    docs/legal/GPL-2.0.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
    more details.

Please see COPYING for more details.
