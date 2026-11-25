# The pictures of Library/Pref/Prefbg (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): convert in CMake, and cvtpcx calls found for
# those no convert script here makes (it gives the same files).  The .ui
# files checked in are the 16-bit build's; the ctest
# library.art.pref_prefbg checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefBG.ui prefmgr.pcx -f -dSM -dSC -dSCGA -m2 -t -l -x0 -y123
            -nPrefBG)

# not in a convert script: found
geos_cvtpcx(mkrGPC.ui PR001-8.pcx -2 -e -nPrefGPC -m1 -x0 -y0 -w64 -h40)

geos_art(pref_prefbg)
