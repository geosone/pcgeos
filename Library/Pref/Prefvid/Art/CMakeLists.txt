# The pictures of Library/Pref/Prefvid (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): cvtpcx calls found for those no convert
# script here makes (it gives the same files).  The .ui files checked in
# are the 16-bit build's; the ctest library.art.pref_prefvid checks that
# both are the same.
#
# Not made here (the CMake build uses the files checked in):
#   mkrPrefVid.ui: written by hand
include(GeosCvtpcx)

# not in a convert script: found
geos_cvtpcx(mkrTvDown.ui VD002-4.pcx -2 -nTvDown -m13 -x0 -y0 -w31 -h28)
geos_cvtpcx(mkrTvLeft.ui VD003-4.pcx -2 -nTvLeft -m13 -x0 -y0 -w28 -h31)
geos_cvtpcx(mkrTvNarrower.ui VD007-4.pcx -2 -nTvNarrower -m13 -x0 -y0 -w67
            -h41)
geos_cvtpcx(mkrTvRight.ui VD004-4.pcx -2 -nTvRight -m13 -x0 -y0 -w28 -h31)
geos_cvtpcx(mkrTvShorter.ui VD008-4.pcx -2 -nTvShorter -m13 -x0 -y0 -w45
            -h63)
geos_cvtpcx(mkrTvTaller.ui VD006-4.pcx -2 -nTvTaller -m13 -x0 -y0 -w35 -h80)
geos_cvtpcx(mkrTvUp.ui VD001-4.pcx -2 -nTvUp -m13 -x0 -y0 -w31 -h28)
geos_cvtpcx(mkrTvWider.ui VD005-4.pcx -2 -nTvWider -m13 -x0 -y0 -w110 -h36)
geos_cvtpcx(mkrPrefGPC.ui PR010-8.pcx -2 -e -nPrefGPC -m1 -x0 -y0 -w64 -h40)

geos_art(pref_prefvid EXCEPT mkrPrefVid.ui)
