# The pictures of the NewUI specific UI (mkr*.ui) made of the PCX files
# here by cvtpcx at build time: convert and convert2.bat in CMake (see
# CommonUI/CArt).  Not made here: mkrTitleClose.ui is text drawn in a
# gstring now, written by hand (convert2.bat's line for C031-8.pcx is
# from before); mkrPPPConnectedSC.ui and mkrPPPUnconnectedSC.ui (from
# c003a-8.pcx, c003b-8.pcx, not here) are used nowhere.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrStdDialogQuestion.ui questionPM.pcc -f -m13 -l -dLM -dLC
            -dLCGA -nStdDialogQuestion)
geos_cvtpcx(mkrStdDialogWarning.ui warningPM.pcc -f -m13 -l -dLM -dLC
            -dLCGA -nStdDialogWarning)
geos_cvtpcx(mkrStdDialogNotification.ui infoPM.pcc -f -m2 -l -dLM -dLC
            -dLCGA -nStdDialogNotification)
geos_cvtpcx(mkrStdDialogError.ui errorPM.pcc -f -m13 -l -dLM -dLC -dLCGA
            -nStdDialogError)

# convert2.bat
geos_cvtpcx(mkrTitleDone.ui C004-8.pcx -x0 -y3 -2 -nTitleDoneSC -w45 -h10)
geos_cvtpcx(mkrTitleHelp.ui C005-8.pcx -x0 -y3 -2 -nTitleHelpSC -w45 -h10)

geos_cvtpcx(mkrSave.ui C006-8.pcx -m1 -x0 -y0 -2 -nSaveSC -w48 -h30)
geos_cvtpcx(mkrPrint.ui C007-8.pcx -m1 -x0 -y0 -2 -nPrintSC -w48 -h30)
geos_cvtpcx(mkrBack.ui C008-8.pcx -m1 -x0 -y0 -2 -nBackSC -w48 -h30)
geos_cvtpcx(mkrDone.ui C009-8.pcx -m1 -x0 -y0 -2 -nDoneSC -w48 -h30)

add_custom_target(newui_art ALL DEPENDS ${GEOS_CVTPCX_OUTPUTS})
