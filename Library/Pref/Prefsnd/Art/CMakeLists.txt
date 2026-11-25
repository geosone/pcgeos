# The pictures of Library/Pref/Prefsnd (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): convert in CMake, and cvtpcx calls found for
# those no convert script here makes (it gives the same files).  The .ui
# files checked in are the 16-bit build's; the ctest
# library.art.pref_prefsnd checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPrefSnd.ui prefsnd.pcx -dSM -dSC -dSCGA -f -m13 -t -l -2 -w64
            -y82 -h40 -nPrefSnd)

# not in a convert script: found
geos_cvtpcx(mkrPrefGPC.ui PR008-8.pcx -2 -e -nPrefGPC -m1 -x0 -y0 -w64 -h40)

geos_art(pref_prefsnd)
