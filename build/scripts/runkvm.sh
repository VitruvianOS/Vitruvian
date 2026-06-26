#!/bin/sh
set -e
basedir=`realpath ./`

log=0; stdout=0
for a in "$@"; do
  case "$a" in
    --enable-console-log) log=1 ;;
    --enable-console-stdout) stdout=1 ;;
  esac
done
logfile="$basedir/vitruvian-console.log"
if [ "$log" -eq 1 ] && [ "$stdout" -eq 1 ]; then
  serial_args="-chardev stdio,id=ch0,mux=on,signal=off,logfile=$logfile -serial chardev:ch0"
elif [ "$stdout" -eq 1 ]; then
  serial_args="-serial mon:stdio"
else
  serial_args="-serial file:$logfile"
fi

qemu-system-x86_64 \
  -cdrom "$basedir/image_tree/vitruvian-custom.iso" -boot menu=on \
  -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
  -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=mynet \
  $serial_args
