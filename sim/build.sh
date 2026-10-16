#!/bin/bash
# 小维宿主模拟器构建与运行(支持 Linux/macOS g++ 与 Windows MSVC)
# 用法: bash sim/build.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p sim/out sim/build sim/generated

if command -v cmd.exe >/dev/null 2>&1 && command -v cygpath >/dev/null 2>&1; then
  MSYS2_ARG_CONV_EXCL="*" cmd.exe /C "$(cygpath -w sim/build.bat)"
else
  if [ ! -f sim/generated/Font_GLCD.h ]; then
    echo "#define SIM_NO_GLCD_FONT 1" > sim/generated/Font_GLCD.h
  fi
  CXX="${CXX:-g++}"
  CXXFLAGS="-std=c++17 -O2 -Isim -Ilibraries/WioKit/src -Ipet"
  "$CXX" $CXXFLAGS sim/main_selftest.cpp sim/sim_backend.cpp -o sim/build/selftest
  "$CXX" $CXXFLAGS sim/main_sim.cpp sim/sim_backend.cpp sim/mocks.cpp libraries/WioKit/src/WioKitCjk.cpp pet/pet_skin_manager.cpp -o sim/build/pet_sim
  echo "=== L0 selftest on host ==="
  ./sim/build/selftest
  echo "=== XiaoWei scenario ==="
  ./sim/build/pet_sim
  python3 sim/ppm2png.py
fi
