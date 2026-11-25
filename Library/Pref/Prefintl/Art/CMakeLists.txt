# The pictures of Library/Pref/Prefintl (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_prefintl checks that both
# are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefIntl.ui prefmgr2.pcx -dSM -dSC -dSCGA -f -m13 -t -l -2
            -w64 -y0 -h40 -nPrefIntl)

geos_art(pref_prefintl)
