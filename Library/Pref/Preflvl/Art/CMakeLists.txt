# The pictures of Library/Pref/Preflvl (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory:
# convert.bat in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked
# in are the 16-bit build's; the ctest library.art.pref_preflvl checks
# that both are the same.
include(GeosCvtpcx)

# convert.bat
geos_cvtpcx(mkrPreflvl.ui PR032-8.pcx -f -m1 -w64 -h40 -t -2 -nPreflvl)

geos_art(pref_preflvl)
