@echo off
rem Launch the Tiberian Dawn clone with sensible defaults.
rem
rem   play                          GDI mission 1 (scg01ea), GoodGuy, 5000 credits
rem   play scb03ea BadGuy           pick another mission + house (GoodGuy|BadGuy)
rem   play scg08ea GoodGuy 8000     ... + starting credits
rem   play scg01ea GoodGuy 5000 --no-shroud   extra game.exe flags pass through
rem
rem Map naming: scg* = GDI missions, scb* = Nod missions, scm* = multiplayer.
setlocal
cd /d "%~dp0"

set "EXE=build\Release\game.exe"
if not exist "%EXE%" (
    echo %EXE% not found - build first:  cmake --build build --config Release
    exit /b 1
)

set "MAP=%~1"
if "%MAP%"=="" set "MAP=scg01ea"
set "HOUSE=%~2"
if "%HOUSE%"=="" set "HOUSE=GoodGuy"
set "CREDITS=%~3"
if "%CREDITS%"=="" set "CREDITS=5000"

rem Find which disc holds this map (GDI missions live on the gdi disc, Nod on
rem nod, expansion maps on covert_ops).
set "ROOT="
if exist "data\assets\tiberian_dawn\gdi\GENERAL\%MAP%.ini" set "ROOT=data\assets\tiberian_dawn\gdi"
if not defined ROOT if exist "data\assets\tiberian_dawn\nod\GENERAL\%MAP%.ini" set "ROOT=data\assets\tiberian_dawn\nod"
if not defined ROOT if exist "data\assets\tiberian_dawn\covert_ops\GENERAL\%MAP%.ini" set "ROOT=data\assets\tiberian_dawn\covert_ops"
if not defined ROOT (
    echo map not found on any disc: %MAP%.ini
    exit /b 1
)

rem Consume the three positional args; anything left passes through.
if not "%~1"=="" shift
if not "%~1"=="" shift
if not "%~1"=="" shift

"%EXE%" "%ROOT%\GENERAL\%MAP%.ini" "%ROOT%" --house %HOUSE% --credits %CREDITS% %1 %2 %3 %4 %5 %6
