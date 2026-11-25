# The pictures of the spooler (mkr*.ui) made of the PCX files here by
# cvtpcx at build time, into the binary directory: convert and
# convert2.bat (in that order) in CMake, as in
# Library/SpecUI/CommonUI/CArt.  The .ui files checked in are the 16-bit
# build's; the ctest library.art.spool checks that both are the same.
#
# PCX/icons3.pcx has the GEOS palette (it had a Windows one, which
# cvtpcx maps to the GEOS colours since 1998; mkrPenPrint.ui was made
# before).
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrPaperPort.ui PCX/gpaper.pcx -2 -f -m13 -t -l -x0 -y0 -dLC
            -dLM -dLCGA -nPaperPort)
geos_cvtpcx(mkrPaperLand.ui PCX/gpaper.pcx -2 -f -m13 -t -l -x0 -y41 -dLC
            -dLM -dLCGA -nPaperLand)
geos_cvtpcx(mkrEnvRight.ui PCX/gpaper4.pcx -2 -f -m13 -t -l -x0 -y82 -dLC
            -dLM -dLCGA -nEnvRight)
geos_cvtpcx(mkrEnvLeft.ui PCX/gpaper4.pcx -2 -f -m13 -t -l -x0 -y123 -dLC
            -dLM -dLCGA -nEnvLeft)
geos_cvtpcx(mkrEnvUp.ui PCX/gpaper4.pcx -2 -f -m13 -t -l -x0 -y164 -dLC -dLM
            -dLCGA -nEnvUp)
geos_cvtpcx(mkrEnvDown.ui PCX/gpaper4.pcx -2 -f -m13 -t -l -x0 -y205 -dLC
            -dLM -dLCGA -nEnvDown)
geos_cvtpcx(mkrPrPaperPort.ui PCX/gpaper2.pcx -2 -f -m13 -t -l -x0 -y0 -dLC
            -dLM -dLCGA -nPrPaperPort)
geos_cvtpcx(mkrPrPaperLand.ui PCX/gpaper2.pcx -2 -f -m13 -t -l -x0 -y41 -dLC
            -dLM -dLCGA -nPrPaperLand)
geos_cvtpcx(mkrPrEnvRight.ui PCX/gpaper2.pcx -2 -f -m13 -t -l -x0 -y82 -dLC
            -dLM -dLCGA -nPrEnvRight)
geos_cvtpcx(mkrPrEnvLeft.ui PCX/gpaper2.pcx -2 -f -m13 -t -l -x0 -y123 -dLC
            -dLM -dLCGA -nPrEnvLeft)
geos_cvtpcx(mkrPrEnvUp.ui PCX/gpaper2.pcx -2 -f -m13 -t -l -x0 -y164 -dLC
            -dLM -dLCGA -nPrEnvUp)
geos_cvtpcx(mkrPrEnvDown.ui PCX/gpaper2.pcx -2 -f -m13 -t -l -x0 -y205 -dLC
            -dLM -dLCGA -nPrEnvDown)
geos_cvtpcx(mkrPrPaperPortDown.ui PCX/gpaper3.pcx -2 -f -m13 -t -l -x0 -y0
            -dLC -dLM -dLCGA -nPrPaperPortDown)
geos_cvtpcx(mkrPrPaperLandDown.ui PCX/gpaper3.pcx -2 -f -m13 -t -l -x0 -y41
            -dLC -dLM -dLCGA -nPrPaperLandDown)
geos_cvtpcx(mkrPrEnvRightDown.ui PCX/gpaper3.pcx -2 -f -m13 -t -l -x0 -y82
            -dLC -dLM -dLCGA -nPrEnvRightDown)
geos_cvtpcx(mkrPrEnvLeftDown.ui PCX/gpaper3.pcx -2 -f -m13 -t -l -x0 -y123
            -dLC -dLM -dLCGA -nPrEnvLeftDown)
geos_cvtpcx(mkrPrEnvUpDown.ui PCX/gpaper3.pcx -2 -f -m13 -t -l -x0 -y164
            -dLC -dLM -dLCGA -nPrEnvUpDown)
geos_cvtpcx(mkrPrEnvDownDown.ui PCX/gpaper3.pcx -2 -f -m13 -t -l -x0 -y205
            -dLC -dLM -dLCGA -nPrEnvDownDown)
geos_cvtpcx(mkrPrint.ui PCX/gpaper.pcx -2 -f -m13 -t -l -x0 -y246 -dLC -dLM
            -dLCGA -nPrint)
geos_cvtpcx(mkrFax.ui PCX/gpaper.pcx -2 -f -m13 -t -l -x0 -y287 -dLC -dLM
            -dLCGA -nFax)
geos_cvtpcx(mkrPrintTool.ui PCX/print.pcx -2 -f -m13 -t -z -x0 -y0
            -nPrintTool)
geos_cvtpcx(mkrFaxTool.ui PCX/print.pcx -2 -f -m13 -t -z -x0 -y16 -nFaxTool)
geos_cvtpcx(mkrPageSizeTool.ui PCX/filemenu.pcx -2 -f -m13 -t -z -x0 -y0
            -nPageSizeTool)
geos_cvtpcx(mkrPenPrint.ui PCX/icons3.pcx -2 -f -t -m11 -x236 -y5 -w28 -h28
            -nPenPrint)


# convert2.bat
geos_cvtpcx(mkrPrintToolTC.ui A128-8.pcx -m225 -x0 -y0 -2 -nPrintToolTC -w20
            -h16 -Stool -stiny)


add_custom_target(spool_art ALL DEPENDS ${GEOS_CVTPCX_OUTPUTS})
