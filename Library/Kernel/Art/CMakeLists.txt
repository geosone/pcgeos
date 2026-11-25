# The pictures of Library/Kernel (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert in
# CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.kernel checks that both are the
# same.
include(GeosCvtpcx)

# convert
# (and the sed after it)
geos_cvtpcx(mkrSysWarning.ui inforti.pcx STRIP_CHUNK -N -2 -f -g -t -j -u
            -x4 -y0 -w24 -h20 -nSysWarning)

geos_art(kernel)
