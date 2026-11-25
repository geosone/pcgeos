# The pictures of Library/Pref/Prefint (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_prefint checks that both
# are the same.
#
# Not made here (the CMake build uses the files checked in):
#   mkrPrefUIC.ui: its PCX file (uicolor.pcx) is not here
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefInt.ui prefint.pcx -dSM -dSC -dSCGA -f -m13 -t -l -2 -w64
            -y0 -h40 -nPrefInt)

geos_art(pref_prefint EXCEPT mkrPrefUIC.ui)
