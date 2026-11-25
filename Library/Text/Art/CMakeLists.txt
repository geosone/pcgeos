# The pictures of the text library (mkr*.ui) made of the PCX files here
# by cvtpcx at build time, into the binary directory: convert and
# convert2.bat (in that order) in CMake, as in
# Library/SpecUI/CommonUI/CArt.  The .ui files checked in are the 16-bit
# build's; the ctest library.art.text checks that both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPlain.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y0 -nPlain)
geos_cvtpcx(mkrItalic.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y16 -nItalic)
geos_cvtpcx(mkrBold.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y32 -nBold)
geos_cvtpcx(mkrUnderline.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y48
            -nUnderline)
geos_cvtpcx(mkrSubscript.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y64
            -nSubscript)
geos_cvtpcx(mkrSuperscript.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y80
            -nSuperscript)
geos_cvtpcx(mkrStrikethru.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y96
            -nStrikethru)
geos_cvtpcx(mkrSmaller.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y112 -nSmaller)
geos_cvtpcx(mkrLarger.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y128 -nLarger)
geos_cvtpcx(mkrCenter.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y144 -nCenter)
geos_cvtpcx(mkrLeft.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y160 -nLeft)
geos_cvtpcx(mkrFull.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y176 -nFull)
geos_cvtpcx(mkrRight.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y192 -nRight)
geos_cvtpcx(mkrSingleSpace.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y208
            -nSingleSpace)
geos_cvtpcx(mkrOneAndAHalfSpace.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y224
            -nOneAndAHalfSpace)
geos_cvtpcx(mkrDoubleSpace.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y240
            -nDoubleSpace)
geos_cvtpcx(mkrTripleSpace.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y256
            -nTripleSpace)
geos_cvtpcx(mkrBoxed.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y272 -nBoxed)
geos_cvtpcx(mkrButton.ui stylspac.pcx -2 -f -m13 -t -z -x0 -y288 -nButton)

geos_cvtpcx(mkrSpell.ui newdict.pcx -2 -f -m13 -t -z -x0 -y0 -nSpell)
geos_cvtpcx(mkrHyphenation.ui newdict.pcx -2 -f -m13 -t -z -x0 -y16
            -nHyphenation)
geos_cvtpcx(mkrSearchReplace.ui search.pcx -2 -f -m13 -t -z -x0 -y0
            -nSearchReplace)

geos_cvtpcx(mkrWildCardGString.ui s-rchar.pcx -2 -f -R -m13 -t -z -x0 -y96
            -j -dTC -dTCGA -nWildCardGString)
geos_cvtpcx(mkrPageBreakGString.ui s-rchar.pcx -2 -f -R -m13 -t -z -x0 -y0
            -j -dTC -dTCGA -nPageBreakGString)
geos_cvtpcx(mkrCRGString.ui s-rchar.pcx -2 -f -R -m13 -t -z -x0 -y16 -j -dTC
            -dTCGA -nCRGString)
geos_cvtpcx(mkrWildCharGString.ui s-rchar.pcx -2 -f -R -m13 -t -z -x0 -y112
            -j -dTC -dTCGA -nWildCharGString)
geos_cvtpcx(mkrGraphicGString.ui s-rchar.pcx -2 -f -R -m13 -t -z -x0 -y128
            -j -dTC -dTCGA -nGraphicGString)
geos_cvtpcx(mkrTabGString.ui s-rchar.pcx -2 -f -R -m13 -t -z -x0 -y144 -j
            -dTC -dTCGA -nTabGString)


# convert2.bat
geos_cvtpcx(mkrLeftTC.ui A101-8.pcx -m225 -x0 -y0 -2 -nLeftTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrCenterTC.ui A102-8.pcx -m225 -x0 -y0 -2 -nCenterTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrRightTC.ui A103-8.pcx -m225 -x0 -y0 -2 -nRightTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrFullTC.ui A104-8.pcx -m225 -x0 -y0 -2 -nFullTC -w16 -h16
            -Stool -stiny)

geos_cvtpcx(mkrSingleSpaceTC.ui A105-8.pcx -m225 -x0 -y0 -2 -nSingleSpaceTC
            -w16 -h16 -Stool -stiny)
geos_cvtpcx(mkrOneAndAHalfSpaceTC.ui A106-8.pcx -m225 -x0 -y0 -2
            -nOneAndAHalfSpaceTC -w16 -h16 -Stool -stiny)
geos_cvtpcx(mkrDoubleSpaceTC.ui A107-8.pcx -m225 -x0 -y0 -2 -nDoubleSpaceTC
            -w16 -h16 -Stool -stiny)
geos_cvtpcx(mkrTripleSpaceTC.ui A108-8.pcx -m225 -x0 -y0 -2 -nTripleSpaceTC
            -w16 -h16 -Stool -stiny)

geos_cvtpcx(mkrUnderlineTC.ui A109-8.pcx -m225 -x0 -y0 -2 -nUnderlineTC -w15
            -h16 -Stool -stiny)

geos_cvtpcx(mkrSearchReplaceTC.ui A127-8.pcx -m225 -x0 -y0 -2
            -nSearchReplaceTC -w18 -h16 -Stool -stiny)

geos_cvtpcx(mkrSmallerTC.ui A147-8.pcx -m225 -x0 -y0 -2 -nSmallerTC -w16
            -h16 -Stool -stiny)
geos_cvtpcx(mkrLargerTC.ui A146-8.pcx -m225 -x0 -y0 -2 -nLargerTC -w16 -h16
            -Stool -stiny)


add_custom_target(text_art ALL DEPENDS ${GEOS_CVTPCX_OUTPUTS})
