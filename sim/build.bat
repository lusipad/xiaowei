@echo off
rem 小维宿主模拟器:MSVC 编译 + L0 自检 + 场景跑帧(由 sim/build.sh 调起)
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
set SEEED_GFX2=C:\Users\lus\Documents\Arduino\libraries\Seeed_GFX2
set INC=/I sim /I libraries\WioKit\src /I pet
set CFLAGS=/nologo /utf-8 /EHsc /std:c++17 /W3 /D_CRT_SECURE_NO_WARNINGS %INC%

if not exist sim\generated mkdir sim\generated
if exist "%SEEED_GFX2%\src\font\Font_GLCD.h" (
  copy /y "%SEEED_GFX2%\src\font\Font_GLCD.h" sim\generated\Font_GLCD.h >nul
) else (
  echo #define SIM_NO_GLCD_FONT 1> sim\generated\Font_GLCD.h
)
if not exist sim\build mkdir sim\build
if not exist sim\out mkdir sim\out

call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo ERR: vcvars64 failed & exit /b 1 )

cl %CFLAGS% sim\main_sim.cpp sim\sim_backend.cpp sim\mocks.cpp libraries\WioKit\src\WioKitCjk.cpp pet\pet_skin_manager.cpp /Fe:sim\build\pet_sim.exe /Fo:sim\build\
if errorlevel 1 exit /b 1

cl %CFLAGS% sim\main_selftest.cpp sim\sim_backend.cpp /Fe:sim\build\selftest.exe /Fo:sim\build\
if errorlevel 1 exit /b 1

echo === L0 selftest on host ===
sim\build\selftest.exe
if errorlevel 1 exit /b 1

echo === XiaoWei scenario ===
sim\build\pet_sim.exe
if errorlevel 1 exit /b 1

python sim\ppm2png.py
