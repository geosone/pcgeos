#!/bin/sh
# mkdosbase.sh -- build a hard disk image with a complete DOS installation,
# unattended, as the base for running tests (and later GEOS) in QEMU.
#
#   mkdosbase.sh OUT.IMG DISK1.IMG [DISK2.IMG ...]
#
# The disks are an installation set made by msdosinst (github.com/geosone/
# msdosinst): disk 1 boots and has INSTALL.EXE (GeosOne Dual Installer),
# the others hold the archive parts.  No DOS files are part of this
# repository; bring your own set.
#
# How: the target disk gets an empty primary partition (C:).  A second
# disk (D:) receives the files of all installation disks, so the installer
# never asks for a disk change.  QEMU boots a copy of disk 1 whose
# AUTOEXEC.BAT runs FDISK /MBR and INSTALL /S (all defaults: format C:,
# SYS, files, CONFIG.SYS/AUTOEXEC.BAT with HIMEM, EMM386, mouse driver ...)
# and then ends QEMU through the isa-debug-exit device.
#
# At the end C:\AUTOEXEC.BAT gets one line: IF EXIST D:\RUN.BAT CALL ...,
# the hook dosrun.sh uses (it passes the program on a second disk D: and
# starts this image read-only, as a QEMU snapshot).
#
# Environment: DOSBASE_SIZE_MB (default 256), DOSBASE_TIMEOUT (s, 600).
# If QEMU does not end by itself, a screenshot OUT.IMG.png shows why.
set -eu
[ $# -ge 2 ] || { echo "usage: $0 OUT.IMG DISK1.IMG [DISK2.IMG ...]" >&2; exit 2; }
out=$1; shift
disk1=$1
for t in qemu-system-i386 sfdisk mkfs.fat mcopy mdir socat; do
    command -v $t >/dev/null || { echo "mkdosbase: $t not found" >&2; exit 2; }
done
size=${DOSBASE_SIZE_MB:-256}
work=$(mktemp -d "${TMPDIR:-/tmp}/mkdosbase.XXXXXX")
trap 'rm -rf "$work"' EXIT
export MTOOLS_SKIP_CHECK=1

# target disk: one active primary FAT16 partition from sector 63 (DOS CHS
# alignment); formatted by the installer
truncate -s "${size}M" "$work/c.img"
printf 'label: dos\nstart=63, type=6, bootable\n' | sfdisk -q "$work/c.img"

# source disk D: all files of all installation disks in its root
truncate -s 32M "$work/d.img"
printf 'label: dos\nstart=63, type=6\n' | sfdisk -q "$work/d.img"
mkfs.fat -F 16 -n DOSSETUP --offset 63 -h 63 "$work/d.img" >/dev/null
for d in "$@"; do
    mkdir -p "$work/files"
    mcopy -i "$d" -n -s '::*' "$work/files/" 2>/dev/null
done
mcopy -i "$work/d.img@@32256" -s "$work/files/"* ::

# boot floppy: disk 1 with our AUTOEXEC.BAT
cp "$disk1" "$work/a.img"
chmod u+w "$work/a.img"
printf '\260\000\346\364\353\376' > "$work/QEXIT.COM"   # out 0f4h -> qemu ends
{
    printf '@echo off\r\n'
    printf 'FDISK /MBR\r\n'
    printf 'D:\r\n'
    printf 'D:\\INSTALL.EXE /S\r\n'
    printf 'A:\\QEXIT.COM\r\n'
} > "$work/AUTOEXEC.BAT"
mcopy -i "$work/a.img" -o "$work/AUTOEXEC.BAT" "$work/QEXIT.COM" ::

accel=tcg
[ -w /dev/kvm ] && accel=kvm
echo "mkdosbase: installing (qemu, $accel) ..."
qemu-system-i386 -accel "$accel" -m 32 \
    -display none -serial none -parallel none \
    -monitor "unix:$work/mon.sock,server,nowait" \
    -drive "file=$work/a.img,if=floppy,format=raw" \
    -drive "file=$work/c.img,if=ide,index=0,format=raw" \
    -drive "file=$work/d.img,if=ide,index=1,format=raw" \
    -boot a -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x01 \
    > "$work/qemu.log" 2>&1 &
qpid=$!

# INSTALL /S of GeosOne Dual Installer 1.1 does not return after a complete
# installation (empty Setup screen, 100% CPU; seen 2026-11-10).  So: done
# when C:\AUTOEXEC.BAT exists and the screen has not changed for 10 s.
mon() { echo "$1" | socat - "UNIX-CONNECT:$work/mon.sock" >/dev/null 2>&1; }
shot() { rm -f "$work/screen.ppm"; mon "screendump $work/screen.ppm"; sleep 0.5
         md5sum "$work/screen.ppm" 2>/dev/null | cut -d' ' -f1; }
t=0; same=0; last=
while kill -0 $qpid 2>/dev/null && [ $t -lt "${DOSBASE_TIMEOUT:-600}" ]; do
    sleep 2; t=$((t + 2))
    command -v socat >/dev/null || continue
    h=$(shot)
    if [ -n "$h" ] && [ "$h" = "$last" ]; then same=$((same + 2)); else same=0; fi
    last=$h
    if [ $same -ge 10 ] && mdir -i "$work/c.img@@32256" ::AUTOEXEC.BAT >/dev/null 2>&1; then
        echo "mkdosbase: installation finished after ${t}s (installer idle; stopping QEMU)"
        break
    fi
done
if kill -0 $qpid 2>/dev/null; then
    if [ $t -ge "${DOSBASE_TIMEOUT:-600}" ]; then
        echo "mkdosbase: timeout after ${t}s; screenshot $out.png" >&2
        python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
            "$work/screen.ppm" "$out.png" 2>/dev/null || true
    fi
    kill $qpid
fi
wait $qpid 2>/dev/null || true

# check the result
c="$work/c.img@@32256"
if ! mdir -i "$c" -a ::IO.SYS >/dev/null 2>&1 || ! mdir -i "$c" ::DOS >/dev/null 2>&1; then
    echo "mkdosbase: installation failed (no IO.SYS or C:\\DOS); qemu log:" >&2
    tail -20 "$work/qemu.log" >&2
    exit 1
fi
# file system check of C: (the installer was stopped, not ended)
if command -v fsck.fat >/dev/null; then
    dd if="$work/c.img" of="$work/part.img" bs=512 skip=63 status=none
    if ! fsck.fat -n "$work/part.img" > "$work/fsck.log" 2>&1; then
        echo "mkdosbase: fsck.fat reports problems on C:" >&2
        cat "$work/fsck.log" >&2
        exit 1
    fi
    rm -f "$work/part.img"
fi
# hook for dosrun.sh
mtype -i "$c" ::AUTOEXEC.BAT > "$work/AE.BAT"
printf 'IF EXIST D:\\RUN.BAT CALL D:\\RUN.BAT\r\n' >> "$work/AE.BAT"
mcopy -o -i "$c" "$work/AE.BAT" ::AUTOEXEC.BAT
mv "$work/c.img" "$out"
echo "mkdosbase: $out ($size MB); C:\\CONFIG.SYS:"
mtype -i "$out@@32256" ::CONFIG.SYS | tr -d '\r'
echo "C:\\AUTOEXEC.BAT:"
mtype -i "$out@@32256" ::AUTOEXEC.BAT | tr -d '\r'
