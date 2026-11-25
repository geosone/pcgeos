# The pictures of the ISUI specific UI (mkr*.ui) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert2.bat
# in CMake.  As CommonUI/CArt: the .ui files checked in are the 16-bit
# build's, the ctest library.art.isui checks that both are the same.
include(GeosCvtpcx)

geos_cvtpcx(mkrMinimize.ui minimize.pcx -x0 -y1 -m13 -2 -nMinimizeColor
            -w16 -h11)
geos_cvtpcx(mkrMaximize.ui maximize.pcx -x0 -y1 -m13 -2 -nMaximizeColor
            -w16 -h11)
geos_cvtpcx(mkrRestore.ui restore.pcx -x0 -y1 -m13 -2 -nRestoreColor
            -w16 -h11)
geos_cvtpcx(mkrHelp.ui help.pcx -x0 -y1 -m13 -2 -nHelpColor -w16 -h11)
geos_cvtpcx(mkrClose.ui close.pcx -x0 -y1 -m13 -2 -nCloseColor -w16 -h11)
geos_cvtpcx(mkrStdDialogError.ui errorPM.pcc -f -m13 -x0 -y0 -l -dLM -dLC
            -dLCGA -nStdDialogError)
geos_cvtpcx(mkrStdDialogNotification.ui infoPM.pcc -f -m2 -x0 -y0 -l -dLM
            -dLC -dLCGA -nStdDialogNotification)
geos_cvtpcx(mkrStdDialogQuestion.ui questionPM.pcc -f -m13 -x0 -y0 -l -dLM
            -dLC -dLCGA -nStdDialogQuestion)
geos_cvtpcx(mkrStdDialogWarning.ui warningPM.pcc -f -m13 -x0 -y0 -l -dLM
            -dLC -dLCGA -nStdDialogWarning)

add_custom_target(isui_art ALL DEPENDS ${GEOS_CVTPCX_OUTPUTS})
