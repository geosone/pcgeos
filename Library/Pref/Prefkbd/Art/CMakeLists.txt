# The pictures of Library/Pref/Prefkbd (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): cvtpcx calls found for those no convert
# script here makes (it gives the same files).  The .ui files checked in
# are the 16-bit build's; the ctest library.art.pref_prefkbd checks that
# both are the same.
#
# Not made here (the CMake build uses the files checked in):
#   mkrPrefKbd.ui: written by hand
include(GeosCvtpcx)

# not in a convert script: found
geos_cvtpcx(mkrPrefGPC.ui PR003-8.pcx -2 -e -nPrefGPC -m1 -x0 -y0 -w64 -h40)

geos_art(pref_prefkbd EXCEPT mkrPrefKbd.ui)
