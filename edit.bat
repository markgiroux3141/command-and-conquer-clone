@echo off
rem Launch the mapedit level editor, reading/writing data\custom.
rem
rem   edit                          new 64x64 temperate level (new_level)
rem   edit mymap                    edit data\custom\mymap.ini if it exists,
rem                                 else start a new level saved as mymap
rem   edit mymap --width 96 --height 96 --theater DESERT
rem                                 new level of a given size/theater
rem
rem Any extra mapedit flags after the name pass straight through.
setlocal
cd /d "%~dp0"

set "EXE=build\Release\mapedit.exe"
if not exist "%EXE%" (
    echo %EXE% not found - build first:  cmake --build build --config Release
    exit /b 1
)

set "ROOT=data\assets\tiberian_dawn\gdi"
if not exist "data\custom" mkdir "data\custom"

set "NAME=%~1"
if "%NAME%"=="" set "NAME=new_level"
if not "%~1"=="" shift

set "MAP=data\custom\%NAME%.ini"
if exist "%MAP%" (
    "%EXE%" "%ROOT%" --open "%MAP%" %1 %2 %3 %4 %5 %6 %7 %8 %9
) else (
    "%EXE%" "%ROOT%" --out "%MAP%" %1 %2 %3 %4 %5 %6 %7 %8 %9
)
