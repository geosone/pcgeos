# The pictures of Library/ConView (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert in
# CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.conview checks that both are the
# same.
#
# Not made here (the CMake build uses the files checked in):
#   back.ui: written by hand
#   begin.ui: written by hand
#   find.ui: written by hand
#   mkrTurnCMoniker.ui: cvtpcx does not make it the same (yet)
#   next.ui: written by hand
#   prev.ui: written by hand
#   revisit.ui: written by hand
#   send.ui: written by hand
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrBackCMoniker.ui buttons.pcx -x236 -y234 -m3 -w32 -h32 -Stool
            -anormal -2 -nBackC)
geos_cvtpcx(mkrHistoryCMoniker.ui buttons.pcx -x282 -y285 -m3 -w32 -h32
            -Stool -anormal -2 -nHistoryC)
geos_cvtpcx(mkrBeginCMoniker.ui buttons.pcx -x332 -y233 -m3 -w32 -h32 -Stool
            -anormal -2 -nBeginC)
geos_cvtpcx(mkrPrevCMoniker.ui buttons.pcx -x464 -y241 -m6 -w18 -h16 -Stool
            -anormal -2 -nPrevC)
geos_cvtpcx(mkrNextCMoniker.ui buttons.pcx -x487 -y241 -m6 -w18 -h16 -Stool
            -anormal -2 -nNextC)
geos_cvtpcx(mkrSendCMoniker.ui buttons.pcx -x376 -y281 -m3 -w32 -h32 -Stool
            -anormal -2 -nSendC)
geos_cvtpcx(mkrFindCMoniker.ui buttons.pcx -x420 -y233 -m13 -w32 -h32 -Stool
            -anormal -2 -nFindC)
geos_cvtpcx(mkrBackMMoniker.ui btnbw.pcx -x45 -y44 -w32 -h32 -Stool -anormal
            -2 -b -nBackM)
geos_cvtpcx(mkrHistoryMMoniker.ui btnbw.pcx -x90 -y43 -w32 -h32 -Stool
            -anormal -2 -b -nHistoryM)
geos_cvtpcx(mkrBeginMMoniker.ui btnbw.pcx -x128 -y43 -w32 -h32 -Stool
            -anormal -2 -b -nBeginM)
geos_cvtpcx(mkrPrevMMoniker.ui btnbw.pcx -x264 -y52 -w18 -h16 -Stool
            -anormal -2 -b -nPrevM)
geos_cvtpcx(mkrNextMMoniker.ui btnbw.pcx -x287 -y52 -w18 -h16 -Stool
            -anormal -2 -b -nNextM)
geos_cvtpcx(mkrSendMMoniker.ui btnbw.pcx -x173 -y44 -w32 -h32 -Stool
            -anormal -2 -b -nSendM)
geos_cvtpcx(mkrFindMMoniker.ui btnbw.pcx -x220 -y44 -w32 -h32 -Stool
            -anormal -2 -b -nFindM)
geos_cvtpcx(mkrTurnMMoniker.ui turnpage.pcx -x35 -y26 -w46 -h14 -Stool
            -anormal -2 -b -nTurnM)

geos_art(conview EXCEPT
  back.ui begin.ui find.ui mkrTurnCMoniker.ui next.ui prev.ui revisit.ui send.ui)
