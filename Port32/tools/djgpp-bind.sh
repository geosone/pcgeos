#!/bin/sh
# djgpp-bind.sh -- bind CWSDPMI into a DJGPP executable.
#
#   djgpp-bind.sh EXE2COFF CWSDSTUB.EXE PROGRAM.EXE
#
# A DJGPP program is a COFF image behind a small real-mode stub that loads
# a DPMI host from disk.  CWSDSTUB.EXE is a stub with CWSDPMI built in:
# exe2coff strips the original stub, the COFF image goes behind CWSDSTUB
# (see cwsdpmi.doc), so the program runs on plain DOS without other files.
set -eu
[ $# -eq 3 ] || { echo "usage: $0 exe2coff cwsdstub.exe program.exe" >&2; exit 2; }
exe2coff=$1 stub=$2 prog=$3
case $prog in
*.exe|*.EXE) coff=${prog%.???} ;;
*) echo "djgpp-bind: $prog: not an .exe" >&2; exit 2 ;;
esac
"$exe2coff" "$prog"
cat "$stub" "$coff" > "$prog.tmp"
rm -f "$coff"
mv "$prog.tmp" "$prog"
