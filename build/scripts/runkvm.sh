#!/bin/sh

set -e

basedir=`realpath ./generated.x86`

qemu-system-x86_64 -cdrom ./generated.x86/LIVE_BOOT/vitruvian-custom.iso -boot menu=on -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm
