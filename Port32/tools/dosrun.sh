#!/bin/sh
# dosrun.sh -- run a DOS program headless and return its exit code.
#
# Used as CMAKE_CROSSCOMPILING_EMULATOR for GEOS_HOST=dos32, so ctest can run
# the DOS test binaries.  The program runs as T.EXE; its standard output is
# printed, its exit code (errorlevel) is returned.
#
#   dosrun.sh PROGRAM.EXE [ARGS...]
#
# Two engines:
#
#   qemu-hd if DOSRUN_BASE_IMG names a hard disk image made by mkdosbase.sh
#           (complete DOS with HIMEM, EMM386, mouse driver ...).  It boots
#           read-only (QEMU snapshot); the program and its results go over a
#           second disk D:, which C:\AUTOEXEC.BAT calls (D:\RUN.BAT).
#   qemu    if DOSRUN_BOOT_IMG names a bootable DOS floppy image (e.g.
#           MS-DOS 7.1 disk 1).  Only IO.SYS, MSDOS.SYS and COMMAND.COM
#           and the boot sector are taken from it; the program runs on real
#           DOS in qemu-system-i386 (KVM if available) with SeaBIOS, a real
#           8253/8259 model and no DOS emulation.  Closest to hardware.
#   dosbox  otherwise: DOSBox-X headless, with its built-in DOS.
#
# DOSRUN_ENGINE=qemu-hd|qemu|dosbox forces one, DOSRUN_QEMU_ACCEL=kvm|tcg the qemu
# accelerator.  DOSRUN_TIMEOUT (seconds, default
# 120).  No DOS files are part of this repository; bring your own image.
set -eu
[ $# -ge 1 ] || { echo "usage: $0 program.exe [args...]" >&2; exit 2; }
prog=$1; shift

engine=${DOSRUN_ENGINE:-}
if [ -z "$engine" ]; then
    if [ -n "${DOSRUN_BASE_IMG:-}" ]; then engine=qemu-hd
    elif [ -n "${DOSRUN_BOOT_IMG:-}" ]; then engine=qemu
    else engine=dosbox; fi
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/dosrun.XXXXXX")
trap 'rm -rf "$work"' EXIT

# RUN.BAT: run the program, map the errorlevel back to a number (DOS has no
# $?), write it to RC.TXT.  $1 = command to run at the end (or empty),
# $2 = drive prefix of the files (e.g. "D:\\", default current dir).
write_batch() {
    p=${2:-}
    {
        printf '@echo off\r\n'
        # DOS/32A: no warning 9004 when no mouse driver is loaded
        printf 'set DOS32A=/NOWARN:9004\r\n'
        printf '%sT.EXE %s > %sOUT.TXT\r\n' "$p" "$ARGS" "$p"
        i=255
        while [ $i -ge 1 ]; do
            printf 'if errorlevel %d goto e%d\r\n' $i $i
            i=$((i - 1))
        done
        printf 'echo 0> %sRC.TXT\r\ngoto end\r\n' "$p"
        i=255
        while [ $i -ge 1 ]; do
            printf ':e%d\r\necho %d> %sRC.TXT\r\ngoto end\r\n' $i $i "$p"
            i=$((i - 1))
        done
        printf ':end\r\n%s\r\n' "$1"
    }
}
ARGS="$*"

case $engine in
dosbox)
    command -v dosbox-x >/dev/null || { echo "dosrun: dosbox-x not found" >&2; exit 2; }
    cp "$prog" "$work/T.EXE"
    write_batch "" > "$work/RUN.BAT"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout "${DOSRUN_TIMEOUT:-120}" dosbox-x -silent -nogui -nomenu \
            -c "mount c \"$work\"" -c "c:" -c "RUN.BAT" -c "exit" \
            > "$work/engine.log" 2>&1 || true
    out="$work/OUT.TXT"; rc="$work/RC.TXT"
    ;;
qemu-hd)
    base=${DOSRUN_BASE_IMG:?set DOSRUN_BASE_IMG to an image made by mkdosbase.sh}
    for t in qemu-system-i386 mcopy sfdisk mkfs.fat; do
        command -v $t >/dev/null || { echo "dosrun: $t not found" >&2; exit 2; }
    done
    export MTOOLS_SKIP_CHECK=1
    x="$work/x.img"
    truncate -s 32M "$x"
    printf 'label: dos\nstart=63, type=6\n' | sfdisk -q "$x"
    mkfs.fat -F 16 -n DOSRUN --offset 63 -h 63 "$x" >/dev/null
    printf '\260\000\346\364\353\376' > "$work/QEXIT.COM"
    write_batch 'D:\QEXIT.COM' 'D:\' > "$work/RUN.BAT"
    cp "$prog" "$work/T.EXE"
    mcopy -i "$x@@32256" "$work/T.EXE" "$work/QEXIT.COM" "$work/RUN.BAT" ::
    accel=${DOSRUN_QEMU_ACCEL:-}
    if [ -z "$accel" ]; then
        accel=tcg
        [ -w /dev/kvm ] && accel=kvm
    fi
    timeout "${DOSRUN_TIMEOUT:-120}" qemu-system-i386 -accel "$accel" \
        -m 32 -display none -monitor none -serial none -parallel none \
        -drive "file=$base,if=ide,index=0,format=raw,snapshot=on" \
        -drive "file=$x,if=ide,index=1,format=raw" -boot c -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x01 \
        > "$work/engine.log" 2>&1 || true
    mcopy -i "$x@@32256" ::OUT.TXT "$work/OUT.TXT" 2>/dev/null || true
    mcopy -i "$x@@32256" ::RC.TXT "$work/RC.TXT" 2>/dev/null || true
    out="$work/OUT.TXT"; rc="$work/RC.TXT"
    ;;
qemu)
    boot=${DOSRUN_BOOT_IMG:?set DOSRUN_BOOT_IMG to a bootable DOS floppy image}
    for t in qemu-system-i386 mcopy mdel mattrib; do
        command -v $t >/dev/null || { echo "dosrun: $t not found" >&2; exit 2; }
    done
    img="$work/disk.img"
    cp "$boot" "$img"
    chmod u+w "$img"
    export MTOOLS_SKIP_CHECK=1
    # keep only the boot sector and the system files
    mattrib -i "$img" -r -s -h '::*.*' >/dev/null 2>&1 || true
    for f in $(mdir -i "$img" -a -b :: 2>/dev/null | sed 's|^::/||'); do
        case $(echo "$f" | tr a-z A-Z) in
            IO.SYS|MSDOS.SYS|COMMAND.COM) ;;
            *) mdeltree -i "$img" "::$f" >/dev/null 2>&1 || mdel -i "$img" "::$f" >/dev/null 2>&1 || true ;;
        esac
    done
    mattrib -i "$img" +r +s +h ::IO.SYS ::MSDOS.SYS >/dev/null 2>&1 || true
    # QEXIT.COM: mov al,0 / out 0f4h,al / jmp $ -> isa-debug-exit ends qemu
    printf '\260\000\346\364\353\376' > "$work/QEXIT.COM"
    printf '\r\n' > "$work/CONFIG.SYS"
    write_batch "QEXIT.COM" > "$work/AUTOEXEC.BAT"
    cp "$prog" "$work/T.EXE"
    mcopy -i "$img" -o "$work/T.EXE" "$work/QEXIT.COM" "$work/CONFIG.SYS" \
        "$work/AUTOEXEC.BAT" :: || { echo "dosrun: image too small for $prog" >&2; exit 2; }
    accel=${DOSRUN_QEMU_ACCEL:-}
    if [ -z "$accel" ]; then
        accel=tcg
        [ -w /dev/kvm ] && accel=kvm
    fi
    timeout "${DOSRUN_TIMEOUT:-120}" qemu-system-i386 -accel "$accel" \
        -m 32 -display none -monitor none -serial none -parallel none \
        -drive "file=$img,if=floppy,format=raw" -boot a -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x01 \
        > "$work/engine.log" 2>&1 || true
    mcopy -i "$img" ::OUT.TXT "$work/OUT.TXT" 2>/dev/null || true
    mcopy -i "$img" ::RC.TXT "$work/RC.TXT" 2>/dev/null || true
    out="$work/OUT.TXT"; rc="$work/RC.TXT"
    ;;
*)
    echo "dosrun: unknown DOSRUN_ENGINE '$engine'" >&2; exit 2 ;;
esac

[ -f "$out" ] && tr -d '\r' < "$out"
if [ ! -f "$rc" ]; then
    echo "dosrun: program did not finish under $engine (timeout or crash); log:" >&2
    tail -20 "$work/engine.log" >&2
    exit 125
fi
exit "$(tr -dc '0-9' < "$rc")"
