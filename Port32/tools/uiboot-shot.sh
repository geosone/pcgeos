#!/bin/sh
# uiboot-shot.sh -- a screenshot of the UI as library.ui.boot draws it
# (Motif, uiboot's primary and document windows), read back from the
# display after a step of the test, on an X server of its own
# (geos-xserver.sh, Xvfb).
#
#   uiboot-shot.sh [-at step] [build dir] [out.png]
#
# Default: after the last step, build, Local/shots/uiboot.png.  Needs
# ImageMagick (magick) for the PNG.
set -eu

top=$(cd "$(dirname "$0")/../.." && pwd)
at=
if [ "${1:-}" = -at ]; then at=$2; shift 2; fi
build=${1:-$top/build}
out=${2:-$top/Local/shots/uiboot.png}
mkdir -p "$(dirname "$out")"
ppm=${out%.png}.ppm
UIBOOT_SHOT=$ppm UIBOOT_SHOT_AT=${at:-50} \
    "$top/Port32/tools/geos-xserver.sh" -geometry 640x480 \
    "$build/Port32/Tests/test_ui_boot" >/dev/null
magick "$ppm" "$out"
rm -f "$ppm"
echo "$out"
