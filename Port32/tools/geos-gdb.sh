#!/bin/sh
# geos-gdb.sh -- gdb on GEOS or a test, with the symbols of the geodes.
#
#   geos-gdb.sh [-vnc] [-view] [gdb options] program [arguments]
#
# Runs gdb with Port32/tools/geos-gdb.py on a screen of its own
# (geos-xserver.sh: Xvfb, or with -vnc a screen to watch, -view opens the
# viewer), never on the desktop.  gdb options (-batch, -ex <command>,
# -x <file>) come before the program; -x files are read after the
# program's symbols.  Examples:
#
#   geos-gdb.sh build/Port32/Tests/test_ui_boot
#   geos-gdb.sh -batch -ex "break OLCtrlSpecBuild" -ex run -ex bt \
#       build/Port32/Tests/test_ui_boot
set -eu
here=$(cd "$(dirname "$0")" && pwd)
xopts=
while [ $# -gt 0 ]; do
    case $1 in
    -vnc|-view) xopts="$xopts $1" ;;
    *) break ;;
    esac
    shift
done
set -- "$@" --end--
# gdb options up to the program; "source" instead of -x (after symbols)
while [ "$1" != --end-- ]; do
    case $1 in
    -ex) set -- "$@" -ex "$2"; shift ;;
    -x) set -- "$@" -ex "source $2"; shift ;;
    -*) set -- "$@" "$1" ;;
    *) break ;;
    esac
    shift
done
[ "$1" != --end-- ] || { echo "usage: geos-gdb.sh [-vnc] [-view] [gdb options] program [arguments]" >&2; exit 2; }
prog=$1; shift
args=
n=0
while [ "$1" != --end-- ]; do n=$((n + 1)); eval "arg$n=\$1"; shift; done
shift
# now "$@" holds the gdb options
i=1
set -- gdb -q -ex "source $here/geos-gdb.py" "$@" --args "$prog"
while [ $i -le $n ]; do eval "set -- \"\$@\" \"\$arg$i\""; i=$((i + 1)); done
# shellcheck disable=SC2086
exec "$here/geos-xserver.sh" $xopts "$@"
