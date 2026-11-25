# The pictures of Library/Pref/HPrefCN (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_hprefcn checks that both
# are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrHPrefCN.ui prefcn.pcx -f -dSM -dSC -dSCGA -m13 -t -l -x0
            -y123 -nHPrefCN)

geos_art(pref_hprefcn)
