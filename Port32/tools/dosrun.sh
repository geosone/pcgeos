#!/bin/sh
# dosrun.sh -- run a DOS program headless in DOSBox-X and return its exit code.
#
# Used as CMAKE_CROSSCOMPILING_EMULATOR for GEOS_HOST=dos32, so ctest can run
# the DOS test binaries.  The program is copied under an 8.3 name into a
# fresh directory that becomes drive C:; its standard output is printed, its
# exit code (errorlevel) is returned.
#
#   dosrun.sh PROGRAM.EXE [ARGS...]
#
# Environment: DOSRUN_TIMEOUT (seconds, default 120).
set -eu
[ $# -ge 1 ] || { echo "usage: $0 program.exe [args...]" >&2; exit 2; }
prog=$1; shift
command -v dosbox-x >/dev/null || { echo "dosrun: dosbox-x not found" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/dosrun.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp "$prog" "$work/T.EXE"

# The batch file maps the errorlevel back to a number (DOS has no $?).
{
    printf '@echo off\r\n'
    printf 'T.EXE %s > OUT.TXT\r\n' "$*"
    i=255
    while [ $i -ge 1 ]; do
        printf 'if errorlevel %d goto e%d\r\n' $i $i
        i=$((i - 1))
    done
    printf 'echo 0> RC.TXT\r\ngoto end\r\n'
    i=255
    while [ $i -ge 1 ]; do
        printf ':e%d\r\necho %d> RC.TXT\r\ngoto end\r\n' $i $i
        i=$((i - 1))
    done
    printf ':end\r\n'
} > "$work/RUN.BAT"

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout "${DOSRUN_TIMEOUT:-120}" dosbox-x -silent -nogui -nomenu \
        -c "mount c \"$work\"" -c "c:" -c "RUN.BAT" -c "exit" \
        > "$work/dosbox.log" 2>&1 || true

[ -f "$work/OUT.TXT" ] && tr -d '\r' < "$work/OUT.TXT"
if [ ! -f "$work/RC.TXT" ]; then
    echo "dosrun: program did not finish (timeout or crash); dosbox log:" >&2
    tail -20 "$work/dosbox.log" >&2
    exit 125
fi
exit "$(tr -dc '0-9' < "$work/RC.TXT")"
