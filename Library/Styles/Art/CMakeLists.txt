# The pictures of Library/Styles (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory
# (cmake/GeosCvtpcx.cmake): convert in CMake, and cvtpcx calls found for
# those no convert script here makes (it gives the same files).  The .ui
# files checked in are the 16-bit build's; the ctest library.art.styles
# checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrStore.ui 1stylsts.pcx -f -m2 -t -z -x0 -y0 -nStore)
geos_cvtpcx(mkrRecall.ui 1stylsts.pcx -f -m2 -t -z -x0 -y16 -nRecall)
geos_cvtpcx(mkrRedefine.ui 1stylsts.pcx -f -m2 -t -z -x0 -y32 -nRedefine)
geos_cvtpcx(mkrReturnToBase.ui 1stylsts.pcx -f -m2 -t -z -x0 -y48
            -nReturnToBase)

# not in a convert script: found
geos_cvtpcx(mkrRecallTC.ui A138-8.pcx -2 -nRecallTC -m225 -x0 -y0 -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrRedefineTC.ui A135-8.pcx -2 -nRedefineTC -m225 -x0 -y0 -w16
            -h16 -Stool -stiny)
geos_cvtpcx(mkrReturnToBaseTC.ui A136-8.pcx -2 -nReturnToBaseTC -m225 -x0
            -y0 -w16 -h16 -Stool -stiny)
geos_cvtpcx(mkrStoreTC.ui A137-8.pcx -2 -nStoreTC -m225 -x0 -y0 -w16 -h16
            -Stool -stiny)

geos_art(styles)
