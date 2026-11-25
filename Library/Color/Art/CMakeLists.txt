# The pictures of Library/Color (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert in
# CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.color checks that both are the
# same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrAreaColor.ui grobj.pcx -f -m2 -t -x333 -y1 -w27 -h18
            -nAreaColor)
geos_cvtpcx(mkrAreaMask.ui grobj.pcx -f -b -m2 -t -x333 -y20 -w27 -h18
            -nAreaMask)
geos_cvtpcx(mkrAreaPattern.ui grobj.pcx -f -b -m2 -t -x333 -y39 -w27 -h18
            -nAreaPattern)
geos_cvtpcx(mkrLineColor.ui grobj.pcx -f -m2 -t -x333 -y58 -w27 -h18
            -nLineColor)
geos_cvtpcx(mkrLineMask.ui grobj.pcx -f -b -m2 -t -x333 -y77 -w27 -h18
            -nLineMask)
geos_cvtpcx(mkrTextColor.ui grobj.pcx -f -m2 -t -x333 -y96 -w27 -h18
            -nTextColor)
geos_cvtpcx(mkrTextMask.ui grobj.pcx -f -b -m2 -t -x333 -y115 -w27 -h18
            -nTextMask)
geos_cvtpcx(mkrTextPattern.ui grobj.pcx -f -b -m2 -t -x333 -y134 -w27 -h18
            -nTextPattern)

geos_art(color)
