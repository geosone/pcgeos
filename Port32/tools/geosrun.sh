#!/bin/sh
# geosrun.sh -- run a GEOS target tree headless on real DOS in QEMU and
# fetch a result file it writes.
#
#   geosrun.sh TARGET_DIR RESULT [APP.GEO ...]
#
# TARGET_DIR is a target made by buildbbx.pl (the directory with loader.exe
# and geos.ini, e.g. Local/gbuild/localpc/ensemble).  It is copied to a
# second disk as D:\GEOS together with the given applications (into
# D:\GEOS\WORLD).  geos.ini is adapted for a plain PC: no host file system
# (DOSBox only), the generic mouse driver (DOS MOUSE.EXE of the base
# image), and execOnStartup lists only the given applications, by their
# long names.  The applications must shut GEOS down when done
# (SysShutdown); then the batch file ends QEMU.
#
# RESULT is a file name relative to D:\GEOS (the GEOS top directory);
# it is printed to stdout.  The DOS base image comes from DOSRUN_BASE_IMG
# (see mkdosbase.sh); it is booted read-only.
#
# Engines: qemu (real MS-DOS from DOSRUN_BASE_IMG, with HDPMI16 from the
# target; the default when DOSRUN_BASE_IMG is set) or dosbox (DOSBox-X with
# its built-in DOS); GEOSRUN_ENGINE=qemu|dosbox forces one.
#
# Environment: GEOSRUN_VIDEO (device of [screen 0], e.g. "VESA Compatible
# SuperVGA: 640x480 64K-color"), GEOSRUN_TIMEOUT (s, default 180),
# GEOSRUN_MEM (MB, qemu, default 64), DOSRUN_QEMU_ACCEL=kvm|tcg.
# GEOSRUN_KEEP=DIR keeps the GEOS tree after the run and, with qemu, a
# screenshot (screen.png) and the exchange disk; GEOSRUN_SHOT=1 stays at
# the DOS prompt after GEOS ends, so the screenshot shows its messages.
set -eu
[ $# -ge 2 ] || { echo "usage: $0 TARGET_DIR RESULT [APP.GEO ...]" >&2; exit 2; }
target=$1; result=$2; shift 2
# NC target: loader.exe + geos.ini; EC target: loaderec.exe + geosec.ini
if [ -f "$target/loader.exe" ] && [ -f "$target/geos.ini" ]; then
    loader=loader; ini=geos.ini
elif [ -f "$target/loaderec.exe" ] && [ -f "$target/geosec.ini" ]; then
    loader=loaderec; ini=geosec.ini
else
    echo "geosrun: $target is no GEOS target (loader[ec].exe, geos[ec].ini)" >&2; exit 2
fi
for t in python3 mcopy sfdisk mkfs.fat; do
    command -v $t >/dev/null || { echo "geosrun: $t not found" >&2; exit 2; }
done
work=$(mktemp -d "${TMPDIR:-/tmp}/geosrun.XXXXXX")
trap '[ -n "${GEOSRUN_KEEP:-}" ] && mkdir -p "$GEOSRUN_KEEP" && rm -rf "$GEOSRUN_KEEP/GEOS" && cp -r "$work/GEOS" "$GEOSRUN_KEEP/" 2>/dev/null; rm -rf "$work"' EXIT
export MTOOLS_SKIP_CHECK=1

# the GEOS tree with the applications
mkdir -p "$work/GEOS"
cp -r "$target"/. "$work/GEOS/"
apps=
for a in "$@"; do
    cp "$a" "$work/GEOS/world/"
    # long name: GEOS file header, 32 bytes at offset 4 (GFH_longName)
    ln=$(python3 -c 'import sys; d=open(sys.argv[1],"rb").read(); print(d[4:36].split(b"\0")[0].decode("latin-1"))' "$a")
    apps="$apps$ln
"
done
python3 - "$work/GEOS/$ini" "$apps" "${GEOSRUN_VIDEO:-}" <<'EOF'
import re, sys
path, apps, video = sys.argv[1], [a for a in sys.argv[2].split("\n") if a], sys.argv[3]
text = open(path, encoding="latin-1").read().replace("\r\n", "\n")
def setblock(text, key, lines):
    """replace 'key = { ... }' with the given lines (or drop it)."""
    pat = re.compile(r"^%s\s*=\s*\{.*?^\}\s*$\n?" % re.escape(key), re.M | re.S)
    new = ("%s = {\n%s\n}\n" % (key, "\n".join(lines))) if lines is not None else ""
    return pat.sub(new, text, count=1)
text = setblock(text, "fs", None)                    # no hostfs on real DOS
text = setblock(text, "execOnStartup", apps)
text = re.sub(r"^(\[mouse\]\n)(?:(?!\[).*\n)*",
              "\\1device = MOUSE.COM or MOUSE.SYS\ndriver = genmouse.geo\ninfo = 0\n\n",
              text, count=1, flags=re.M)
if video:                                            # [screen 0] device
    text = re.sub(r"^(\[screen 0\]\n(?:(?!\[).*\n)*?)device\s*=.*$",
                  lambda m: m.group(1) + "device = " + video, text, count=1, flags=re.M)
open(path, "w", encoding="latin-1").write(text.replace("\n", "\r\n"))
EOF

engine=${GEOSRUN_ENGINE:-}
if [ -z "$engine" ]; then
    if [ -n "${DOSRUN_BASE_IMG:-}" ]; then engine=qemu; else engine=dosbox; fi
fi
rc=0
case $engine in
dosbox)
    command -v dosbox-x >/dev/null || { echo "geosrun: dosbox-x not found" >&2; exit 2; }
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout "${GEOSRUN_TIMEOUT:-180}" dosbox-x -nogui -nomenu \
            -set "dosbox memsize=64" -set "cpu cputype=pentium" \
            -c "mount c \"$work/GEOS\"" -c "c:" -c "hdpmi16" -c "$loader > LOADER.LOG" -c "exit" \
            > "$work/engine.log" 2>&1 || rc=$?
    [ $rc -eq 124 ] && echo "geosrun: GEOS did not end within ${GEOSRUN_TIMEOUT:-180}s" >&2
    resultfile="$work/GEOS/$result"
    ;;
qemu)
    # exchange disk D:
    x="$work/x.img"
    truncate -s 64M "$x"
    printf 'label: dos\nstart=63, type=6\n' | sfdisk -q "$x"
    mkfs.fat -F 16 -n GEOSRUN --offset 63 -h 63 "$x" >/dev/null
    printf '\260\000\346\364\353\376' > "$work/QEXIT.COM"
    # GEOS32 (Loader32) needs a DPMI host: HDPMI16 from the target, started
    # like bin/basebox.conf does
    # GEOSRUN_SHOT=1: stay at the DOS prompt, screenshot at the timeout
    qexit='D:\QEXIT.COM'
    [ -n "${GEOSRUN_SHOT:-}" ] && qexit='rem'
    printf '@echo off\r\nD:\r\ncd \\GEOS\r\nif exist hdpmi16.exe hdpmi16\r\n%s\r\n%s\r\n' "$loader" "$qexit" > "$work/RUN.BAT"
    mcopy -i "$x@@32256" -s "$work/GEOS" "$work/RUN.BAT" "$work/QEXIT.COM" ::
    accel=${DOSRUN_QEMU_ACCEL:-}
    if [ -z "$accel" ]; then
        accel=tcg
        [ -w /dev/kvm ] && accel=kvm
    fi
    base=${DOSRUN_BASE_IMG:?set DOSRUN_BASE_IMG to an image made by mkdosbase.sh}
    qemu-system-i386 -accel "$accel" -m "${GEOSRUN_MEM:-64}" \
        -display none -serial none -parallel none \
        -monitor "unix:$work/mon.sock,server,nowait" \
        -drive "file=$base,if=ide,index=0,format=raw,snapshot=on" \
        -drive "file=$x,if=ide,index=1,format=raw" -boot c -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x01 \
        > "$work/engine.log" 2>&1 &
    qpid=$!
    t=0
    while kill -0 $qpid 2>/dev/null && [ $t -lt "${GEOSRUN_TIMEOUT:-180}" ]; do
        sleep 1; t=$((t + 1))
    done
    if kill -0 $qpid 2>/dev/null; then
        echo "geosrun: GEOS did not end within ${t}s" >&2
        rc=124
    fi
    if [ -n "${GEOSRUN_KEEP:-}" ]; then
        mkdir -p "$GEOSRUN_KEEP"
        echo "screendump $work/screen.ppm" | socat - "UNIX-CONNECT:$work/mon.sock" >/dev/null 2>&1 || true
        sleep 1
        python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
            "$work/screen.ppm" "$GEOSRUN_KEEP/screen.png" 2>/dev/null || true
        cp "$x" "$GEOSRUN_KEEP/geosrun-d.img"
    fi
    kill $qpid 2>/dev/null || true
    wait $qpid 2>/dev/null || true
    mcopy -i "$x@@32256" "::GEOS/$result" "$work/result" 2>/dev/null || true
    resultfile="$work/result"
    ;;
*)
    echo "geosrun: unknown GEOSRUN_ENGINE '$engine'" >&2; exit 2 ;;
esac

if [ -f "$resultfile" ]; then
    tr -d '\r' < "$resultfile"
else
    echo "geosrun: no $result written" >&2
    [ -n "${GEOSRUN_KEEP:-}" ] && [ -f "$work/engine.log" ] && mkdir -p "$GEOSRUN_KEEP" && cp "$work/engine.log" "$GEOSRUN_KEEP/"
    [ $rc -ne 0 ] || rc=1
fi
exit $rc
