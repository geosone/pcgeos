#!/bin/sh
# uiref-cmp.sh -- the same UI objects in the original 16-bit GEOS and in
# the C port, side by side.
#
#   uiref-cmp.sh [variant ...]        (default: 1 2 3 4)
#
# For each variant (see uiref-gen.py) the reference application is
# generated and built with the original goc and pmake in
# Local/geosref/uiref, run in the original GEOS on DOS in QEMU
# (geosrun.sh, target Local/gbuild/localpc/ensemble, 640x480) and its
# screen dumped; the port draws the same variant (uiboot-shot.sh -ref).
# Result: Local/shots/ref<v>.png (original), port<v>.png and cmp<v>.png
# (original left, port right).
#
# Needs the DOS base image (DOSRUN_BASE_IMG, default Local/dosbase.img),
# Open Watcom (WATCOM, default /opt/watcom), ImageMagick.  Environment:
# UIREF_TARGET (GEOS target), UIREF_BUILD (the port's build directory,
# default build), UIREF_WAIT (seconds GEOS gets to come up, default 40).
set -eu
top=$(cd "$(dirname "$0")/../.." && pwd)
: "${WATCOM:=/opt/watcom}"
ROOT_DIR=$top
LOCAL_ROOT=$top/Local
PATH=$WATCOM/binl64:$WATCOM/binl:$top/bin:$PATH
INCLUDE=$WATCOM/lh
export WATCOM ROOT_DIR LOCAL_ROOT PATH INCLUDE
: "${DOSRUN_BASE_IMG:=$top/Local/dosbase.img}"
export DOSRUN_BASE_IMG
target=${UIREF_TARGET:-$top/Local/gbuild/localpc/ensemble}
build=${UIREF_BUILD:-$top/build}
app=$top/Local/geosref/uiref
shots=$top/Local/shots
mkdir -p "$shots"
[ $# -gt 0 ] || set -- 1 2 3 4
for v in "$@"; do
    python3 "$top/Port32/tools/uiref-gen.py" "$app" "$v"
    (cd "$app" && { [ -f Makefile ] || mkmf >/dev/null 2>&1; } &&
     pmake full >"$app/build.log" 2>&1) ||
	{ echo "uiref-cmp: the reference application did not build ($app/build.log)" >&2; exit 1; }
    keep=$top/Local/geosref/run-uiref$v
    GEOSRUN_VIDEO="VESA Compatible SuperVGA: 640x480 64K-color" \
    GEOSRUN_TIMEOUT=${UIREF_WAIT:-40} GEOSRUN_KEEP=$keep \
	"$top/Port32/tools/geosrun.sh" "$target" NONE.TXT "$app/uiref.geo" \
	>/dev/null 2>&1 || true
    cp "$keep/screen.png" "$shots/ref$v.png"
    "$top/Port32/tools/uiboot-shot.sh" -ref "$v" "$build" "$shots/port$v.png" >/dev/null
    magick "$shots/ref$v.png" "$shots/port$v.png" +append "$shots/cmp$v.png"
    echo "$shots/cmp$v.png"
done
