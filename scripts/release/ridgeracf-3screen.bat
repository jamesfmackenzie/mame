@echo off
REM ridgeracf-3screen.bat
REM
REM Launch three ridgeracf instances for 3-screen play.
REM
REM Ring topology:
REM
REM   Center (master)      Right (forwarder)    Left (slave)
REM   localport  15112     localport  15113     localport  15111
REM   remoteport 15113 --> remoteport 15111 --> remoteport 15112
REM
REM Usage:
REM   ridgeracf-3screen.bat          normal run (all 3 instances)
REM   ridgeracf-3screen.bat setup    first-run: opens one window at a time so
REM                                  you can set the PCB Role in each before
REM                                  the next opens

setlocal

set BINARY=mamenamcos22.exe
set GAME=ridgeracf
set MODE=%1

if not exist %BINARY% (
    echo Error: %BINARY% not found.
    echo This script must be run from the same directory as mamenamcos22.exe
    pause
    exit /b 1
)

if not exist roms\ (
    echo Error: roms\ directory not found.
    echo This script must be run from the same directory as the roms\ folder.
    pause
    exit /b 1
)

mkdir multiplay\center 2>nul
mkdir multiplay\right 2>nul
mkdir multiplay\left 2>nul
mkdir multiplay\logs 2>nul

REM ==========================================================================
REM SETUP MODE
REM ==========================================================================

if /i "%MODE%"=="setup" (
    echo FIRST-RUN SETUP
    echo ---------------
    echo Each window will open one at a time. In each one:
    echo   1. Press Tab to open the MAME menu
    echo   2. Go to Machine Configuration
    echo   3. Set "PCB Role ^(3-Screen^)" to the value shown below
    echo   4. Close the window to continue to the next
    echo.

    echo --- Opening CENTER window ---
    echo     Set PCB Role -^> 'Center ^(master^)'
    echo.
    %BINARY% %GAME% -window -resolution 640x480 -rompath roms -cfg_directory multiplay\center -nvram_directory multiplay\center -comm_localhost 127.0.0.1 -comm_localport 15112 -comm_remotehost 127.0.0.1 -comm_remoteport 15113
    echo Center config saved.
    echo.

    echo --- Opening RIGHT SCREEN window ---
    echo     Set PCB Role -^> 'Right Screen ^(forwarder^)'
    echo.
    %BINARY% %GAME% -window -resolution 640x480 -rompath roms -cfg_directory multiplay\right -nvram_directory multiplay\right -comm_localhost 127.0.0.1 -comm_localport 15113 -comm_remotehost 127.0.0.1 -comm_remoteport 15111
    echo Right screen config saved.
    echo.

    echo --- Opening LEFT SCREEN window ---
    echo     Set PCB Role -^> 'Left Screen ^(slave^)'
    echo.
    %BINARY% %GAME% -window -resolution 640x480 -rompath roms -cfg_directory multiplay\left -nvram_directory multiplay\left -comm_localhost 127.0.0.1 -comm_localport 15111 -comm_remotehost 127.0.0.1 -comm_remoteport 15112
    echo Left screen config saved.
    echo.
    echo Setup complete. Run ridgeracf-3screen.bat to start all three.
    pause
    exit /b 0
)

REM ==========================================================================
REM NORMAL RUN
REM ==========================================================================

echo Starting ridgeracf 3-screen
echo   Physical layout:  [ LEFT ] [ CENTER ] [ RIGHT ]
echo.

start "ridgeracf-center" %BINARY% %GAME% -window -resolution 640x480 -rompath roms -cfg_directory multiplay\center -nvram_directory multiplay\center -comm_localhost 127.0.0.1 -comm_localport 15112 -comm_remotehost 127.0.0.1 -comm_remoteport 15113
timeout /t 1 /nobreak >nul

start "ridgeracf-right"  %BINARY% %GAME% -window -resolution 640x480 -rompath roms -cfg_directory multiplay\right  -nvram_directory multiplay\right  -comm_localhost 127.0.0.1 -comm_localport 15113 -comm_remotehost 127.0.0.1 -comm_remoteport 15111
timeout /t 1 /nobreak >nul

start "ridgeracf-left"   %BINARY% %GAME% -window -resolution 640x480 -rompath roms -cfg_directory multiplay\left   -nvram_directory multiplay\left   -comm_localhost 127.0.0.1 -comm_localport 15111 -comm_remotehost 127.0.0.1 -comm_remoteport 15112

echo All instances launched. Close the windows to stop.
