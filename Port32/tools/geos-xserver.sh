#!/bin/sh
# geos-xserver.sh -- run a command on an X server of its own, so GEOS
# (or a test) can take a whole screen without touching the desktop.
#
#   geos-xserver.sh [-vnc] [-view] [-geometry WxH] [-port N] command [args]
#
#   (default)  Xvfb: a screen in memory, nobody sees it (ctest).
#   -vnc       Xvnc: the screen can be watched and used with a VNC viewer
#              at localhost:PORT (default 5900 + display); no password,
#              only from this machine.
#   -view      with -vnc: also start vncviewer on it.
#   -geometry  the screen's size (default 1024x768).
#
# The command runs with DISPLAY set to that server and
# GEOS_DISPLAY=fullscreen, so ghal_display covers the screen.  The server
# ends with the command; the script exits with the command's status.
set -eu

server=Xvfb
view=0
geometry=1024x768
port=
while [ $# -gt 0 ]; do
    case $1 in
    -vnc) server=Xvnc ;;
    -view) view=1 ;;
    -geometry) geometry=$2; shift ;;
    -port) port=$2; shift ;;
    --) shift; break ;;
    -*) echo "geos-xserver.sh: unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
    shift
done
[ $# -gt 0 ] || { echo "usage: geos-xserver.sh [-vnc] [-view] [-geometry WxH] [-port N] command [args]" >&2; exit 2; }
command -v $server >/dev/null || { echo "geos-xserver.sh: $server not found" >&2; exit 2; }

fifo=$(mktemp -u "${XDG_RUNTIME_DIR:-/tmp}/geos-xserver.XXXXXX")
mkfifo "$fifo"
if [ $server = Xvnc ]; then
    portarg=
    [ -n "$port" ] && portarg="-rfbport $port"
    # shellcheck disable=SC2086
    Xvnc -displayfd 3 -geometry "$geometry" -depth 24 -SecurityTypes None -noreset \
         -localhost -AlwaysShared $portarg 3>"$fifo" >/dev/null 2>&1 &
else
    Xvfb -displayfd 3 -screen 0 "${geometry}x24" -nolisten tcp -noreset \
         3>"$fifo" >/dev/null 2>&1 &
fi
pid=$!
read -r disp <"$fifo"
rm -f "$fifo"
viewer=
cleanup() {
    [ -n "$viewer" ] && kill "$viewer" 2>/dev/null || true
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [ $server = Xvnc ]; then
    [ -n "$port" ] || port=$((5900 + disp))
    echo "geos-xserver.sh: display :$disp, VNC at localhost:$port" >&2
    if [ $view = 1 ] && command -v vncviewer >/dev/null; then
        vncviewer "localhost::$port" >/dev/null 2>&1 &
        viewer=$!
    fi
fi

status=0
DISPLAY=:$disp GEOS_DISPLAY=fullscreen "$@" || status=$?
exit $status
