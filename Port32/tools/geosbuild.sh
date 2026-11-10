#!/bin/sh
# geosbuild.sh -- run the GEOS (pmake) build reproducibly on Linux.
#
# Usage:  Port32/tools/geosbuild.sh [DIR] [pmake args...]
#         DIR defaults to $ROOT_DIR/Installed.
#
# Sets WATCOM / ROOT_DIR / LOCAL_ROOT / PATH like the README describes and
# runs pmake with address space randomization disabled (setarch -R).
# Open Watcom's wcc (and esp's .obj symbol order) leak heap addresses into
# object files; with ASLR every build differs, without it two clean builds
# are byte-identical apart from timestamps (see Port32/tools/geocmp.py).

set -e

: "${WATCOM:=/opt/watcom}"
ROOT_DIR=${ROOT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}
: "${LOCAL_ROOT:=$ROOT_DIR/Local}"
PATH=$WATCOM/binl64:$WATCOM/binl:$ROOT_DIR/bin:$PATH
INCLUDE=$WATCOM/lh
export WATCOM ROOT_DIR LOCAL_ROOT PATH INCLUDE

dir=${1:-$ROOT_DIR/Installed}
[ $# -gt 0 ] && shift
mkdir -p "$LOCAL_ROOT"
cd "$dir"
exec setarch "$(uname -m)" -R pmake "$@"
