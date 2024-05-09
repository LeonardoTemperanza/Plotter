
REM Arguments:
REM - run, runs the program
REM - debug, debug build
REM - release, includes O2 optimization
REM - profile, does profiling
REM etc.

@echo off
setlocal enabledelayedexpansion

if not exist ..\Build mkdir ..\Build 
if not exist ..\Build\Win64 mkdir ..\Build\Win64

pushd ..\Build\Win64

set include_dirs=/I..\..\Source
set lib_dirs=/LIBPATH:..\..\Libs\Win64

set source_files=..\..\Source\unity_build.cpp
set lib_files=user32.lib gdi32.lib shell32.lib glfw3-4\glfw3_mt.lib dawn2024-05-05\webgpu.lib
set output_name=plotter.exe

set common=/nologo /std:c++20 /FC /MT %include_dirs% %source_files% /link %lib_dirs% %lib_files% /out:%output_name% /subsystem:WINDOWS /entry:mainCRTStartup

REM Development build, debug is enabled, profiling and optimization disabled
cl /Zi /Od %common%
set build_ret=%errorlevel%

echo Done.

popd