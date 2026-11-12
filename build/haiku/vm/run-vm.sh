#!/usr/bin/env bash
# Starts the wxHaiku test VM (air/OS x86_64; VNC 127.0.0.1:5915, SSH 2246).
# Disk: WX_VM_DIR/disk.img from make-disk.sh.
set -euo pipefail
DIR=${WX_VM_DIR:-/data1/scratch/bambustudio/vm-wx}
if [[ -f $DIR/qemu.pid ]] && kill -0 "$(cat "$DIR/qemu.pid")" 2>/dev/null; then
	echo 'The wx test VM is already running.'; exit 0
fi
rm -f "$DIR/qmp.sock"
exec qemu-system-x86_64 -enable-kvm -cpu host -m "${WX_VM_MEMORY:-6144}" \
	-smp "${WX_VM_CPUS:-6}" \
	-drive file="$DIR/disk.img",format=raw,if=ide,index=0 \
	-nic user,model=e1000,hostfwd=tcp:127.0.0.1:2246-:22 \
	-device qemu-xhci -device usb-tablet -vga std -display none \
	-vnc 127.0.0.1:15 -qmp unix:"$DIR/qmp.sock",server=on,wait=off \
	-pidfile "$DIR/qemu.pid" -serial file:"$DIR/serial.log" -daemonize
