#!/bin/sh
# Host runner for the fbdev pixel-format, mode-probe and stride tests.
set -e
cd "$(dirname "$0")"
trap 'rm -f .test_*.bin' EXIT

run_one() {
	name=$1
	shift
	out="./.test_${name}.bin"
	c++ -Wall -Wextra -O2 -o "$out" "$@"
	"$out"
	rm -f "$out"
}

run_one format -Ihoststub \
	-I../../../../servers/app/drawing/interface/linux/fbdev \
	test_fbdev_format.cpp

run_one mode -Ihoststub \
	-I../../../../../headers/os/add-ons/graphics \
	-I../../../../servers/app/drawing/interface/linux/fbdev \
	test_fbdev_mode.cpp

run_one stride -Ihoststub \
	-I../../../../servers/app/drawing/interface/linux/fbdev \
	test_fbdev_buffer_stride.cpp \
	../../../../servers/app/drawing/interface/linux/fbdev/FBDevBuffer.cpp
