# The pictures of Library/Pen (the .ui files) made of the PCX files here
# by cvtpcx at build time, into the binary directory: convert in CMake
# (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the 16-bit
# build's; the ctest library.art.pen checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrClear.ui PCX/gntaker.pcx -f -m13 -t -z -x0 -y32 -nClear)
geos_cvtpcx(mkrPencil.ui PCX/gntaker.pcx -f -m13 -t -z -x0 -y112 -nPencil)
geos_cvtpcx(mkrEraser.ui PCX/gntaker.pcx -f -m13 -t -z -x0 -y128 -nEraser)
geos_cvtpcx(mkrSelector.ui PCX/pengrid.pcx -f -m13 -t -z -x0 -y0 -nSelector)

geos_art(pen)
