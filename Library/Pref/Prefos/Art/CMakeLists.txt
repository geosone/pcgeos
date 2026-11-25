# The pictures of Library/Pref/Prefos (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_prefos checks that both are
# the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefOS.ui prefos.pcx -2 -dSM -dSC -dSCGA -m13
            -Sicon-sstandard -t -l -x0 -y0 -w64 -h40 -nPrefOS)

geos_art(pref_prefos)
