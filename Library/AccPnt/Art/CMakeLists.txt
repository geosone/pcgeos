# The pictures of Library/AccPnt (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert in
# CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.accpnt checks that both are the
# same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrTelnetItem.ui telnli.pcx -N -2 -g -x0 -y0 -w30 -h18 -m15
            -nTelnetItem)
geos_cvtpcx(mkrTerminalItem.ui termli.pcx -N -2 -g -x0 -y0 -w30 -h18 -m15
            -nTerminalItem)

geos_art(accpnt)
