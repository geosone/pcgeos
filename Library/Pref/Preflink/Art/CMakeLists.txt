# The pictures of Library/Pref/Preflink (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_preflink checks that both
# are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefLink.ui preflink.pcx -dSM -dSC -dSCGA -f -m13 -t -l -2
            -w64 -x0 -y123 -h40 -nPrefLink)

geos_art(pref_preflink)
