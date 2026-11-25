# The pictures of Library/Spline (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert2.bat
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.spline checks that both are the
# same.
include(GeosCvtpcx)

# convert2.bat
geos_cvtpcx(mkrSquare.ui PCX/SS002-8.pcx -m1 -x0 -y0 -2 -nListSquare -w17
            -h17)
geos_cvtpcx(mkrCross1.ui PCX/SS003-8.pcx -m1 -x0 -y0 -2 -nListCross1 -w17
            -h17)
geos_cvtpcx(mkrDiamond.ui PCX/SS004-8.pcx -m1 -x0 -y0 -2 -nListDiamond -w17
            -h17)
geos_cvtpcx(mkrCross2.ui PCX/SS005-8.pcx -m1 -x0 -y0 -2 -nListCross2 -w17
            -h17)
geos_cvtpcx(mkrTriangle.ui PCX/SS006-8.pcx -m1 -x0 -y0 -2 -nListTriangle
            -w17 -h17)
geos_cvtpcx(mkrBar1.ui PCX/SS007-8.pcx -m1 -x0 -y0 -2 -nListBar1 -w17 -h17)
geos_cvtpcx(mkrBar2.ui PCX/SS008-8.pcx -m1 -x0 -y0 -2 -nListBar2 -w17 -h17)
geos_cvtpcx(mkrCircle.ui PCX/SS009-8.pcx -m1 -x0 -y0 -2 -nListCircle -w17
            -h17)

geos_art(spline)
