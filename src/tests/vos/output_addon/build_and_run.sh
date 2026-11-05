#!/bin/sh
# Build and run the output add-on transport host checks with a plain
# C++ compiler. Everything here is pure protocol code; nothing needs
# /dev/nexus or a V\OS runtime, so it runs in the development container.
set -e

cd "$(dirname "$0")/../../../.."

CXX="${CXX:-c++}"
CXXFLAGS="-std=gnu++17 -O1 -g -Wno-multichar \
	-include headers/build/LinuxBuildCompatibility.h \
	-I headers -I headers/os -I headers/os/support -I headers/private/interface"

OUT="${OUT:-build/hosttests/output_addon}"
mkdir -p "$OUT"

echo "== building output_addon_host_test =="
$CXX $CXXFLAGS \
	src/kits/interface/OutputAddonCore.cpp \
	src/tests/vos/output_addon/output_addon_host_test.cpp \
	-o "$OUT/output_addon_host_test"

echo "== running output_addon_host_test =="
"$OUT/output_addon_host_test"
