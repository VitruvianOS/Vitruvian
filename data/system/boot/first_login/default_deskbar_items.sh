#!/bin/sh

install_item() {
	if ! "$@"; then
		echo "default_deskbar_items: ${1} failed" >&2
	fi
}

install_item /system/apps/ProcessController -deskbar
install_item /system/apps/NetworkStatus --deskbar
install_item /system/apps/AudioMixer --deskbar

exit 0
