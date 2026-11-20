#!/bin/bash
# 5.02: boots the Ubuntu 26.04.1 live server ISO in QEMU (install once into disk.qcow2, then it boots
# from the disk). Ports 1922/udp (MP) and 7777/udp (SA-MP / open.mp crossplay) reach the VM, 2222 = SSH.
# Inside the VM:  git clone https://github.com/JebetrexxMC/wagu && cd wagu/server && make
#                 ./mp_server 1922 "MP LAN Server" 100 Asia --persistent --samp-port 7777
#   RAM=4096 CPUS=4 DISK=40G ./run_qemu.sh      NOGUI=1 for a serial console
cd "$(dirname "$0")" || exit 1
NAME=ubuntu-26.04.1-live-server-amd64.iso
[ -f "$NAME" ] || ./get_iso.sh || exit 1
command -v qemu-system-x86_64 >/dev/null || { echo "Install QEMU: sudo apt install qemu-system-x86 qemu-utils"; exit 1; }
[ -f disk.qcow2 ] || qemu-img create -f qcow2 disk.qcow2 "${DISK:-30G}" || exit 1
accel=(); [ -w /dev/kvm ] && accel=(-enable-kvm -cpu host)
gui=(); [ "$NOGUI" = 1 ] && gui=(-nographic)
exec qemu-system-x86_64 "${accel[@]}" -m "${RAM:-4096}" -smp "${CPUS:-2}" \
    -drive file=disk.qcow2,if=virtio -cdrom "$NAME" -boot order=dc \
    -nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22,hostfwd=udp::1922-:1922,hostfwd=udp::7777-:7777 \
    "${gui[@]}"
