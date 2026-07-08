@echo off
rem Launch the RA clone with sensible defaults.
rem
rem   play                      Soviet sandbox (scu04eb, starting MCV, 10k credits)
rem   play scg01ea Greece      pick another mission + house
rem   play scu04eb USSR 5000   ... + starting credits
rem   play scu04eb USSR 10000 --no-shroud   extra game.exe flags pass through
setlocal
cd /d "%~dp0"

set "EXE=build\Release\game.exe"
if not exist "%EXE%" (
    echo %EXE% not found - build first:  cmake --build build --config Release
    exit /b 1
)

set "MAP=%~1"
if "%MAP%"=="" set "MAP=scu04eb"
set "HOUSE=%~2"
if "%HOUSE%"=="" set "HOUSE=USSR"
set "CREDITS=%~3"
if "%CREDITS%"=="" set "CREDITS=10000"

set "MAPFILE=data\assets\red_alert\allied\MAIN\general\%MAP%.ini"
if not exist "%MAPFILE%" (
    echo map not found: %MAPFILE%
    exit /b 1
)

rem Consume the three positional args; anything left passes through.
if not "%~1"=="" shift
if not "%~1"=="" shift
if not "%~1"=="" shift

"%EXE%" "%MAPFILE%" data\assets\red_alert\allied --house %HOUSE% --credits %CREDITS% %1 %2 %3 %4 %5 %6
