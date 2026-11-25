# The pictures of the specific UIs (mkr*.ui) made of the PCX files here
# by cvtpcx at build time, into the binary directory: the scripts
# convert and convert2.bat (run in that order: convert2.bat replaces
# some of convert's) in CMake.  The .ui files checked in beside them are
# the 16-bit build's, the CMake build's are these; the ctest
# library.art.cart checks that both are the same.
include(GeosCvtpcx)

# convertBigLevels / convertBig yPos name ignoreColor file: a large
# colour, a large mono and a CGA icon of a column; SKIP the formats
# (IC, IM, ICGA) convert2.bat makes of other files
function(_cart_big x_cga y name mask pcx)
  cmake_parse_arguments(PARSE_ARGV 5 B "" "" "SKIP")
  if(NOT "IC" IN_LIST B_SKIP)
    geos_cvtpcx(mkr${name}IC.ui ${pcx} -2 -f -g -m${mask} -t -x1 -y${y}
                -w64 -h40 -n${name}IC)
  endif()
  if(NOT "IM" IN_LIST B_SKIP)
    geos_cvtpcx(mkr${name}IM.ui ${pcx} -2 -f -g -m${mask} -t -b -x66
                -y${y} -w64 -h40 -n${name}IM)
  endif()
  if(NOT "ICGA" IN_LIST B_SKIP)
    geos_cvtpcx(mkr${name}ICGA.ui ${pcx} -2 -f -g -m${mask} -t -b
                -averySquished -x${x_cga} -y${y} -w64 -h20 -n${name}ICGA)
  endif()
  set(GEOS_CVTPCX_OUTPUTS ${GEOS_CVTPCX_OUTPUTS} PARENT_SCOPE)
endfunction()
macro(convertBigLevels)
  _cart_big(229 ${ARGN})
endmacro()
macro(convertBig)
  _cart_big(131 ${ARGN})
endmacro()

# convert
geos_cvtpcx(mkrMOPinColor.ui pushpin.pcc -f -w32 -h13 -x0 -y0 -nMOPinColor
            -u)
#cvtpcx -f -m2 -l -dLM -dLC -dLCGA -nStdDialogQuestion question.pcc
#cvtpcx -f -m2 -l -dLM -dLC -dLCGA -nStdDialogWarning warning.pcc
#cvtpcx -f -m2 -l -dLM -dLC -dLCGA -nStdDialogNotification info.pcc
#cvtpcx -f -m2 -l -dLM -dLC -dLCGA -nStdDialogError error.pcc
#new art that includes Zoomer sized icons - brianc 5/18/93
geos_cvtpcx(mkrStdDialogNotification.ui gmessage.pcx -f -m2 -x0 -y82 -L -dLM
            -dLC -dLCGA -dYC -dTM -dTCGA -nStdDialogNotification)
geos_cvtpcx(mkrStdDialogWarning.ui gmessage.pcx -f -m2 -x0 -y123 -L -dLM
            -dLC -dLCGA -dYC -dTM -dTCGA -nStdDialogWarning)
geos_cvtpcx(mkrStdDialogQuestion.ui gmessage.pcx -f -m2 -x0 -y164 -L -dLM
            -dLC -dLCGA -dYC -dTM -dTCGA -nStdDialogQuestion)
geos_cvtpcx(mkrStdDialogError.ui gmessage.pcx -f -m2 -x0 -y205 -L -dLM -dLC
            -dLCGA -dYC -dTM -dTCGA -nStdDialogError)

geos_cvtpcx(mkrNew.ui docopt2.pcx -2 -f -m13 -t -z -x0 -y0 -nNew)
geos_cvtpcx(mkrUseTemplate.ui docopt2.pcx -2 -f -m13 -t -z -x0 -y16
            -nUseTemplate)
geos_cvtpcx(mkrOpen.ui docopt2.pcx -2 -f -m13 -t -z -x0 -y32 -nOpen)
geos_cvtpcx(mkrClose.ui docopt2.pcx -2 -f -m13 -t -z -x0 -y48 -nClose)
geos_cvtpcx(mkrQuickBackup.ui docopt2.pcx -2 -f -m13 -t -z -x0 -y64
            -nQuickBackup)
geos_cvtpcx(mkrSave.ui docopt2.pcx -2 -f -m13 -t -z -x0 -y80 -nSave)

convertBigLevels(1 Level0 13 glevels.pcx)
convertBigLevels(42 Level1 13 glevels.pcx)
convertBigLevels(83 Level2 13 glevels.pcx)
convertBigLevels(124 Level3 6 glevels.pcx)
convertBigLevels(165 DialogUserLevel 6 glevels.pcx SKIP IC)

convertBig(1 DialogOpenPM 13 1opendbc.pcx)
convertBig(42 DialogEmptyPM 13 1opendbc.pcx)
convertBig(83 DialogUseTemplatePM 13 1opendbc.pcx)
convertBig(124 DialogImportPM 13 1opendbc.pcx)
convertBig(165 DialogOpenDefaultPM 13 1opendbc.pcx)

convertBig(206 DialogOpen 13 1opendbc.pcx SKIP IC)
convertBig(247 DialogEmpty 13 1opendbc.pcx SKIP IC)
convertBig(288 DialogUseTemplate 13 1opendbc.pcx SKIP IC)
convertBig(329 DialogImport 13 1opendbc.pcx SKIP IC)
convertBig(370 DialogOpenDefault 13 1opendbc.pcx SKIP IC)

#
# Stuff for help
#
geos_cvtpcx(mkrHelpTrigSC.ui help.pcx -2 -u -f -g -m13 -t -x3 -y19 -w13 -h12
            -t -nHelpTrigSC)
geos_cvtpcx(mkrHelpTrigSM.ui help.pcx -2 -u -f -g -m13 -t -b -x18 -y19 -w13
            -h12 -t -nHelpTrigSM)
geos_cvtpcx(mkrHelpTrigSCGA.ui help.pcx -2 -u -f -g -m13 -t -b -x35 -y18
            -w13 -h8 -t -nHelpTrigSCGA -averySquished)
geos_cvtpcx(mkrHelpPrimarySC.ui ghelp.pcx -2 -u -f -g -m13 -t -x1 -y1 -w22
            -h13 -t -nHelpPrimarySC)
geos_cvtpcx(mkrHelpPrimarySM.ui ghelp.pcx -2 -u -f -g -m13 -t -b -x26 -y1
            -w22 -h13 -t -nHelpPrimarySM)
geos_cvtpcx(mkrHelpPrimarySCGA.ui ghelp.pcx -2 -u -f -g -m13 -t -b -x50 -y1
            -w22 -h8 -t -nHelpPrimarySCGA -averySquished)

#
# PenInputControl (Floating Keyboard) stuff
#
geos_cvtpcx(mkrKeyboard.ui pengrid.pcx -2 -f -m13 -t -z -dTCGA -x0 -y16
            -nKeyboard)
# (as checked in: -P, packed though not smaller)
geos_cvtpcx(mkrStdChars.ui pengrid.pcx -2 -f -m13 -t -z -dTCGA -x0 -y32
            -nStdChars -P)
geos_cvtpcx(mkrSymbolChars.ui pengrid.pcx -2 -f -m13 -t -z -dTCGA -x0 -y48
            -nSymbolChars)
geos_cvtpcx(mkrInternationalChars.ui pengrid.pcx -2 -f -m13 -t -z -dTCGA -x0
            -y64 -nInternationalChars)
geos_cvtpcx(mkrMathChars.ui pengrid.pcx -2 -f -m13 -t -z -dTCGA -x0 -y80
            -nMathChars)
geos_cvtpcx(mkrHWREntry.ui pengrid.pcx -2 -f -m13 -t -z -dTCGA -x0 -y96
            -nHWREntry)

geos_cvtpcx(mkrStart.ui pengrid.pcx -2 -f -m13 -t -z -dTC -dTCGA -x0 -y112
            -nStart)
geos_cvtpcx(mkrFastBack.ui pengrid.pcx -2 -f -m13 -t -z -dTC -dTCGA -x0
            -y128 -nFastBack)
geos_cvtpcx(mkrBack.ui pengrid.pcx -2 -f -m13 -t -z -dTC -dTCGA -x0 -y144
            -nBack)
geos_cvtpcx(mkrForward.ui pengrid.pcx -2 -f -m13 -t -z -dTC -dTCGA -x0 -y160
            -nForward)
geos_cvtpcx(mkrFastForward.ui pengrid.pcx -2 -f -m13 -t -z -dTC -dTCGA -x0
            -y176 -nFastForward)
geos_cvtpcx(mkrEnd.ui pengrid.pcx -2 -f -m13 -t -z -dTC -dTCGA -x0 -y192
            -nEnd)
geos_cvtpcx(mkrInsert.ui insert.pcx -2 -f -m13 -t -h15 -w28 -b -x0 -y0
            -nInsert)

# convert2.bat
geos_cvtpcx(mkrDialogEmptyIC.ui A050-8.pcx -g -m1 -x0 -y0 -2 -nDialogEmptyIC
            -w48 -h30)
geos_cvtpcx(mkrDialogOpenIC.ui A051-8.pcx -g -m1 -x0 -y0 -2 -nDialogOpenIC
            -w48 -h30)
geos_cvtpcx(mkrDialogUseTemplateIC.ui DA000-8.pcx -g -m1 -x1 -y1 -2
            -nDialogUseTemplateIC -w48 -h30)

geos_cvtpcx(mkrMinimizeColor.ui A032-8.pcx -x0 -y1 -2 -nMinimizeColor -w16
            -h10)
geos_cvtpcx(mkrMinimizeGray.ui A032G-8.pcx -x0 -y1 -2 -nMinimizeColor -w16
            -h10)
geos_cvtpcx(mkrMaximizeColor.ui A033A-8.pcx -x0 -y1 -2 -nMaximizeColor -w16
            -h10)
geos_cvtpcx(mkrMaximizeGray.ui A033AG-8.pcx -x0 -y1 -2 -nMaximizeColor -w16
            -h10)
geos_cvtpcx(mkrRestoreColor.ui A033B-8.pcx -x0 -y1 -2 -nRestoreColor -w16
            -h10)
geos_cvtpcx(mkrRestoreGray.ui A033BG-8.pcx -x0 -y1 -2 -nRestoreColor -w16
            -h10)
geos_cvtpcx(mkrCloseColor.ui A034-8.pcx -x0 -y1 -2 -nCloseColor -w16 -h10)
geos_cvtpcx(mkrCloseGray.ui A034G-8.pcx -x0 -y1 -2 -nCloseColor -w16 -h10)
geos_cvtpcx(mkrHelpColor.ui A031-8.pcx -x0 -y1 -2 -nHelpColor -w16 -h10)
geos_cvtpcx(mkrHelpGray.ui A031G-8.pcx -x0 -y1 -2 -nHelpColor -w16 -h10)

geos_cvtpcx(mkrFSDocumentDir.ui A060-8.pcx -x0 -y0 -2 -m1 -nFSDocumentDir
            -w15 -h13)
geos_cvtpcx(mkrFSFloppyDrive.ui A062-8.pcx -x0 -y0 -2 -m1 -nFSFloppyDrive
            -w15 -h13)
geos_cvtpcx(mkrFSCDDrive.ui A063-8.pcx -x0 -y0 -2 -m1 -nFSCDDrive -w15 -h13)
geos_cvtpcx(mkrFSArchiveDir.ui A061-8.pcx -x0 -y0 -2 -m1 -nFSArchiveDir -w15
            -h13)
geos_cvtpcx(mkrFSOpenFolder.ui A064-8.pcx -x0 -y0 -2 -m1 -g -nFSOpenFolder
            -w15 -h13)
geos_cvtpcx(mkrFSClosedFolder.ui A065-8.pcx -x0 -y0 -2 -m1 -g
            -nFSClosedFolder -w15 -h13)
geos_cvtpcx(mkrFSFile.ui A066-8.pcx -x0 -y0 -2 -m1 -g -nFSFile -w15 -h13)
geos_cvtpcx(mkrFSFloppyFile.ui A062-8.pcx -x0 -y0 -2 -m1 -g -nFSFloppyFile
            -w15 -h13)

geos_cvtpcx(mkrMOWindowMenuColor.ui A024-8.pcx -x50 -y1 -2 -m1
            -nMOWindowMenuColor -w15 -h15)
geos_cvtpcx(mkrMODisplayWindowMenuColor.ui A024-8.pcx -x50 -y1 -2 -m1
            -nMODisplayWindowMenuColor -w15 -h15)

geos_cvtpcx(mkrNewTC.ui A123-8.pcx -m225 -x0 -y0 -2 -nNewTC -w16 -h16 -Stool
            -stiny)
geos_cvtpcx(mkrOpenTC.ui A124-8.pcx -m225 -x0 -y0 -2 -nOpenTC -w19 -h16
            -Stool -stiny)
geos_cvtpcx(mkrCloseTC.ui A125-8.pcx -m225 -x0 -y0 -2 -nCloseTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrSaveTC.ui A126-8.pcx -m225 -x0 -y0 -2 -nSaveTC -w17 -h16
            -Stool -stiny)

geos_cvtpcx(mkrHelpWindowMenuColor.ui HP000-8.pcx -x50 -y1 -2 -m1
            -nHelpWindowMenuColor -w15 -h15)

geos_cvtpcx(mkrDialogImportIC.ui import3.pcx -2 -g -m13 -x1 -y1 -w48 -h30
            -nDialogImportIC)
geos_cvtpcx(mkrDialogImport4b.ui import4b.pcx -g -m13 -x1 -y1 -2
            -nDialogImport4b -w64 -h40)
geos_cvtpcx(mkrDialogOpenDefaultIC.ui default3.pcx -2 -g -m13 -x1 -y1 -w48
            -h30 -nDialogOpenDefaultIC)
geos_cvtpcx(mkrDialogOpenDefault4b.ui deflt4b.pcx -g -m13 -x1 -y1 -2
            -nDialogOpenDefault4b -w64 -h40)
geos_cvtpcx(mkrDialogUserLevelIC.ui levels3.pcx -2 -g -m13 -x1 -y1 -w48 -h30
            -nDialogUserLevelIC)
geos_cvtpcx(mkrDialogUserLevel4b.ui levels4b.pcx -g -m6 -x1 -y1 -2
            -nDialogUserLevel4b -w64 -h40)
geos_cvtpcx(mkrDialogEmpty4b.ui empty4b.pcx -g -m13 -x1 -y1 -2
            -nDialogEmpty4b -w64 -h40)
geos_cvtpcx(mkrDialogOpen4b.ui open4b.pcx -g -m13 -x1 -y1 -2 -nDialogOpen4b
            -w64 -h40)
geos_cvtpcx(mkrDialogUseTemplate4b.ui templ4b.pcx -g -m13 -x1 -y1 -2
            -nDialogUseTemplate4b -w64 -h40)

# (its PCX is gone; the .def is not used)
# geos_cvtpcx(mkrProgramLevelTrayIcon.def ProgramLevelTrayIcon.pcx -j -x0 -y1 -2 -m1 -nProgramLevelTrayIcon -w15 -h15)

geos_cvtpcx(mkrSystrayGrip.ui SystrayGrip.pcx -2 -nSystrayGrip -x0 -y0 -m7
            -w16 -h18)

geos_cvtpcx(mkrExit.ui Exit.pcx -2 -nExitComputerSC -x0 -y0 -m13 -w32 -h32)

geos_art(cart)
