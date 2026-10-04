#!/bin/sh
# Host runner for the fbdev pixel-format and stride fixtures.
set -e
cd "$(dirname "$0")"
out=$(mktemp)
trap 'rm -f "$out"' EXIT
c++ -Wall -Wextra -O2 -o "$out" \
	-Ihoststub \
	-I../../../../servers/app/drawing/interface/linux/fbdev \
	test_fbdev_format.cpp
"$out"
