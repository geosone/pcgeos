# The pictures of Library/Pref (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert in
# CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.pref checks that both are the
# same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrTaskSwitchSC.ui gprefs3.pcx -f -m6 -t -2 -w69 -x1 -y1 -h42
            -nTaskSwitchSC)
geos_cvtpcx(mkrTaskSwitchSM.ui gprefs3.pcx -f -m6 -t -2 -w69 -x71 -y1 -h42
            -nTaskSwitchSM -b)
geos_cvtpcx(mkrTaskSwitchSCGA.ui gprefs3.pcx -f -m6 -t -2 -w69 -x141 -y1
            -h20 -nTaskSwitchSCGA -b -averySquished)
geos_cvtpcx(mkrLinkSC.ui gprefs3.pcx -f -m6 -t -2 -w69 -x1 -y44 -h42
            -nLinkSC)
geos_cvtpcx(mkrLinkSM.ui gprefs3.pcx -f -m6 -t -2 -w69 -x71 -y44 -h42
            -nLinkSM -b)
geos_cvtpcx(mkrLinkSCGA.ui gprefs3.pcx -f -m6 -t -2 -w69 -x141 -y44 -h20
            -nLinkSCGA -b -averySquished)

geos_art(pref)
