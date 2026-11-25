# The pictures of Library/Pref/Prefts (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_prefts checks that both are
# the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefTS.ui prefts.pcx -dSM -dSC -dSCGA -f -m6 -t -l -2 -w64
            -x0 -y0 -h40 -nPrefTS)

geos_art(pref_prefts)
