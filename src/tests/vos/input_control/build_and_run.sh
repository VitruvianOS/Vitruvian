#!/bin/sh
# Build and run the pointer-control host checks with a plain C++
# compiler; pure logic, nothing needs /dev/nexus or a V\OS runtime.
set -e

cd "$(dirname "$0")/../../../.."

CXX="${CXX:-c++}"
CXXFLAGS="-std=gnu++17 -O1 -g -Wno-multichar \
	-include headers/build/LinuxBuildCompatibility.h \
	-I headers -I headers/os -I headers/os/support -I headers/private/input"

OUT="${OUT:-build/hosttests/input_control}"
mkdir -p "$OUT"

echo "== building input_control_host_test =="
$CXX $CXXFLAGS \
	src/kits/shared/PointerControlCore.cpp \
	src/tests/vos/input_control/input_control_host_test.cpp \
	-o "$OUT/input_control_host_test"

echo "== running input_control_host_test =="
"$OUT/input_control_host_test"
