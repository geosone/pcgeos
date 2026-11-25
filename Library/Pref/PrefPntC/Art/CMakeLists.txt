# The pictures of Library/Pref/PrefPntC (the .ui files) made of the PCX
# files here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): cvtpcx calls found for those no convert
# script here makes (it gives the same files).  The .ui files checked in
# are the 16-bit build's; the ctest library.art.pref_prefpntc checks
# that both are the same.
#
# Not made here (the CMake build uses the files checked in):
#   mkrpctrl.ui: written by hand
include(GeosCvtpcx)

# not in a convert script: found
geos_cvtpcx(mkrdialog.ui PR023-8.pcx -2 -ndialog -m1 -x0 -y0 -w38 -h42)
geos_cvtpcx(mkrgreen.ui PR021-8.pcx -2 -e -r -ngreen -m1 -x0 -y0 -w23 -h23)
geos_cvtpcx(mkrred.ui PR022-8.pcx -2 -e -r -nred -m1 -x0 -y0 -w23 -h23)
geos_cvtpcx(mkryellow.ui PR020-8.pcx -2 -e -r -nyellow -m1 -x0 -y0 -w23
            -h23)
geos_cvtpcx(old-mkrpctrl.ui PR011-8.pcx -2 -e -npctrl -m1 -x0 -y0 -w64 -h40)

geos_art(pref_prefpntc EXCEPT mkrpctrl.ui)
