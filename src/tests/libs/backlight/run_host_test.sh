#!/bin/sh
# Host runner for the libbacklight and card-rank fixtures. Does not need
# the full VOS cmake tree.
set -e
cd "$(dirname "$0")"
cc -Wall -Wextra -O2 -o test_backlight_selection \
	-I../../../libs/backlight \
	-I/usr/include/libdrm \
	test_backlight_selection.c \
	../../../libs/backlight/libbacklight.c \
	-ludev
./test_backlight_selection

c++ -Wall -Wextra -O2 -o test_drm_card_rank \
	-I../../../../headers/private \
	-I/usr/include/libdrm \
	test_drm_card_rank.cpp \
	-lseat
./test_drm_card_rank
