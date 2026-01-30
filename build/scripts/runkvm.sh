#!/bin/sh
set -e
basedir=`realpath ./`

qemu-system-x86_64 \
  -cdrom "$basedir/LIVE_BOOT/vitruvian-custom.iso" -boot menu=on \
  -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
  -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=mynet
