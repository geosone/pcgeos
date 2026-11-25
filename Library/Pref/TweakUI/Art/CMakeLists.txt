# The pictures of Library/Pref/TweakUI (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory: convert
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref_tweakui checks that both
# are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrTweakUI.ui tweakui.pcx -dSM -dSC -dSCGA -f -m13 -t -l -2 -w64
            -y0 -h40 -nTweakUI)

geos_art(pref_tweakui)
