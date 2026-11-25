# The pictures of the PM specific UI (mkr*.ui) made of the PCX files
# here by cvtpcx at build time: convert in CMake (see CommonUI/CArt).
include(GeosCvtpcx)

geos_cvtpcx(mkrStdDialogQuestion.ui questionPM.pcc -f -m13 -l -dLM -dLC
            -dLCGA -nStdDialogQuestion)
geos_cvtpcx(mkrStdDialogWarning.ui warningPM.pcc -f -m13 -l -dLM -dLC
            -dLCGA -nStdDialogWarning)
geos_cvtpcx(mkrStdDialogNotification.ui infoPM.pcc -f -m2 -l -dLM -dLC
            -dLCGA -nStdDialogNotification)
geos_cvtpcx(mkrStdDialogError.ui errorPM.pcc -f -m13 -l -dLM -dLC -dLCGA
            -nStdDialogError)

geos_art(pm)
