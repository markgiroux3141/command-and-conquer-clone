@echo off
rem Launch a custom level authored by the mapedit tool.
rem
rem   play-custom                       menu of every level in data\custom
rem   play-custom skirmish_a            open that level (GoodGuy, 5000 credits)
rem   play-custom skirmish_a BadGuy     ... pick a house (GoodGuy|BadGuy)
rem   play-custom skirmish_a GoodGuy 8000   ... + starting credits
rem
rem Custom levels are TD-flavored, so they use a Tiberian Dawn asset root for
rem art/rules. Launching any one lists all data\custom levels in the menu.
setlocal
cd /d "%~dp0"

set "EXE=build\Release\game.exe"
if not exist "%EXE%" (
    echo %EXE% not found - build first:  cmake --build build --config Release
    exit /b 1
)

set "ROOT=data\assets\tiberian_dawn\gdi"

rem No level named: pick the first one in data\custom so the menu can list all.
set "LEVEL=%~1"
if "%LEVEL%"=="" (
    for %%F in ("data\custom\*.ini") do (
        set "LEVEL=%%~nF"
        goto :gotlevel
    )
    echo no levels found in data\custom - author one with mapedit first.
    exit /b 1
)
:gotlevel

set "MAP=data\custom\%LEVEL%.ini"
if not exist "%MAP%" (
    echo not found: %MAP%
    exit /b 1
)

set "HOUSE=%~2"
if "%HOUSE%"=="" set "HOUSE=GoodGuy"
set "CREDITS=%~3"
if "%CREDITS%"=="" set "CREDITS=5000"

"%EXE%" "%MAP%" "%ROOT%" --house %HOUSE% --credits %CREDITS%
