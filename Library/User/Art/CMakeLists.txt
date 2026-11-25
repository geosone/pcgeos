# The pictures of the UI library (mkr*.ui) made of the PCX files here by
# cvtpcx at build time, into the binary directory: convert and
# convert2.bat (in that order) in CMake, as in
# Library/SpecUI/CommonUI/CArt.  The .ui files checked in are the 16-bit
# build's; the ctest library.art.user checks that both are the same.
#
# Where the files checked in were made otherwise than the scripts say,
# the line is as they were made (noted).  The colour icons of the
# express menu came of an 8-bit startmenu.pcx with the GEOS palette,
# replaced later by a 16-colour one (which the mono icons come of): it
# is in Color8/PCX, made again of the .ui files
# (Port32/tools/ui2pcx.py), as PCX/first.pcx, PCX/last.pcx (one line
# short, as the original) and PCX/Dialer.pcx are.
include(GeosCvtpcx)

# convert
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrCut.ui PCX/editools.pcx -2 -f -m13 -t -z -x0 -y0 -nCut -dTM
            -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrUndo.ui PCX/editools.pcx -2 -f -m13 -t -z -x0 -y16 -nUndo
            -dTM -dTCGA)
# (as checked in: -dTM -dTCGA -u besides the script's options)
geos_cvtpcx(mkrDelete.ui PCX/editools.pcx -2 -f -m13 -t -z -x0 -y32 -nDelete
            -dTM -dTCGA -u)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrCopy.ui PCX/editools.pcx -2 -f -m13 -t -z -x0 -y48 -nCopy
            -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrPaste.ui PCX/editools.pcx -2 -f -m13 -t -z -x0 -y64 -nPaste
            -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrSelectAll.ui PCX/editools.pcx -2 -f -m13 -t -z -x0 -y80
            -nSelectAll -dTM -dTCGA)

# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrZoomIn.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y0 -nZoomIn -dTM
            -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrNormalScale.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y16
            -nNormalScale -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrZoomOut.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y32 -nZoomOut
            -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrScaleToFit.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y48
            -nScaleToFit -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrPageRight.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y64
            -nPageRight -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrPageLeft.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y80 -nPageLeft
            -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrPageUp.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y96 -nPageUp
            -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrPageDown.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y112
            -nPageDown -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrPreviousPage.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y128
            -nPreviousPage -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrNextPage.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y144
            -nNextPage -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrRedraw.ui PCX/view.pcx -2 -f -m13 -t -z -x0 -y160 -nRedraw
            -dTM -dTCGA)

# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrApplyToAll.ui PCX/docopt2.pcx -2 -f -m13 -t -z -x0 -y96
            -nApplyToAll -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrShowHorizontal.ui PCX/docopt2.pcx -2 -f -m13 -t -z -x0 -y112
            -nShowHorizontal -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrShowVertical.ui PCX/docopt2.pcx -2 -f -m13 -t -z -x0 -y128
            -nShowVertical -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrAdjustAspect.ui PCX/docopt2.pcx -2 -f -m13 -t -z -x0 -y144
            -nAdjustAspect -dTM -dTCGA)

# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrOverlapping.ui PCX/display2.pcx -2 -f -m13 -t -z -x0 -y0
            -nOverlapping -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrFullSize.ui PCX/display2.pcx -2 -f -m13 -t -z -x0 -y16
            -nFullSize -dTM -dTCGA)
# (as checked in: -dTM -dTCGA besides the script's options)
geos_cvtpcx(mkrTile.ui PCX/display2.pcx -2 -f -m13 -t -z -x0 -y32 -nTile
            -dTM -dTCGA)

geos_cvtpcx(mkrHardIconTools.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y1 -nHardIconTools)
geos_cvtpcx(mkrHardIconMenuBar.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y22 -nHardIconMenuBar)
geos_cvtpcx(mkrHardIconKeyboard.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20
            -b -x0 -y43 -nHardIconKeyboard)
geos_cvtpcx(mkrHardIconCalendar.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20
            -b -x0 -y64 -nHardIconCalendar)
geos_cvtpcx(mkrHardIconAddrBook.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20
            -b -x0 -y85 -nHardIconAddrBook)
geos_cvtpcx(mkrHardIconNotepad.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y106 -nHardIconNotepad)
geos_cvtpcx(mkrHardIconCalc.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y127 -nHardIconCalc)
geos_cvtpcx(mkrHardIconClock.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y148 -nHardIconClock)
geos_cvtpcx(mkrHardIconDict.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y169 -nHardIconDict)
geos_cvtpcx(mkrHardIconHelp.ui PCX/gzoom03.pcx -2 -f -m13 -t -h20 -w20 -b
            -x0 -y190 -nHardIconHelp)

geos_cvtpcx(mkrApplicationsSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13
            -x1 -y1 -w24 -h24 -nApplicationsSC)
geos_cvtpcx(mkrApplicationsSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y1
            -w24 -h24 -b -nApplicationsSM)
geos_cvtpcx(mkrApplicationsTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13
            -x51 -y1 -w15 -h15 -Stool -nApplicationsTC)
geos_cvtpcx(mkrApplicationsTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y1
            -w15 -h15 -b -Stool -nApplicationsTM)

geos_cvtpcx(mkrDocumentsSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x1
            -y26 -w24 -h24 -nDocumentsSC)
geos_cvtpcx(mkrDocumentsSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y26 -w24
            -h24 -b -nDocumentsSM)
geos_cvtpcx(mkrDocumentsTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x51
            -y26 -w15 -h15 -Stool -nDocumentsTC)
geos_cvtpcx(mkrDocumentsTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y26 -w15
            -h15 -b -Stool -nDocumentsTM)

geos_cvtpcx(mkrSettingsSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x1
            -y51 -w24 -h24 -nSettingsSC)
geos_cvtpcx(mkrSettingsSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y51 -w24
            -h24 -b -nSettingsSM)
geos_cvtpcx(mkrSettingsTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x51
            -y51 -w15 -h15 -Stool -nSettingsTC)
geos_cvtpcx(mkrSettingsTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y51 -w15
            -h15 -b -Stool -nSettingsTM)

geos_cvtpcx(mkrFindSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x1 -y76
            -w24 -h24 -nFindSC)
geos_cvtpcx(mkrFindSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y76 -w24 -h24
            -b -nFindSM)
geos_cvtpcx(mkrFindTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x51 -y76
            -w15 -h15 -Stool -nFindTC)
geos_cvtpcx(mkrFindTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y76 -w15 -h15
            -b -Stool -nFindTM)

geos_cvtpcx(mkrHelpSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x1 -y101
            -w24 -h24 -nHelpSC)
geos_cvtpcx(mkrHelpSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y101 -w24
            -h24 -b -nHelpSM)
geos_cvtpcx(mkrHelpTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x51
            -y101 -w15 -h15 -Stool -nHelpTC)
geos_cvtpcx(mkrHelpTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y101 -w15
            -h15 -b -Stool -nHelpTM)

geos_cvtpcx(mkrLaunchSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x1
            -y126 -w24 -h24 -nLaunchSC)
geos_cvtpcx(mkrLaunchSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y126 -w24
            -h24 -b -nLaunchSM)
geos_cvtpcx(mkrLaunchTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x51
            -y126 -w15 -h15 -Stool -nLaunchTC)
geos_cvtpcx(mkrLaunchTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y126 -w15
            -h15 -b -Stool -nLaunchTM)

geos_cvtpcx(mkrExitSC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x1 -y151
            -w24 -h24 -nExitSC)
geos_cvtpcx(mkrExitSM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y151 -w24
            -h24 -b -nExitSM)
geos_cvtpcx(mkrExitTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13 -x51
            -y151 -w15 -h15 -Stool -nExitTC)
geos_cvtpcx(mkrExitTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y151 -w15
            -h15 -b -Stool -nExitTM)

geos_cvtpcx(mkrPreferencesTC.ui PCX/startmenu.pcx IN Color8 -2 -f -t -m13
            -x51 -y176 -w15 -h15 -Stool -nPreferencesTC)
geos_cvtpcx(mkrPreferencesTM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y176
            -w15 -h15 -b -Stool -nPreferencesTM)


# convert2.bat
# (mkrApplicationsSC.ui of A040-8.pcx: the one of convert is checked in)
# (mkrApplicationsTC.ui of A040-8.pcx: the one of convert is checked in)
# (mkrDocumentsSC.ui of A041-8.pcx: the one of convert is checked in)
# (mkrDocumentsTC.ui of A041-8.pcx: the one of convert is checked in)
# (mkrSettingsSC.ui of A042-8.pcx: the one of convert is checked in)
# (mkrFindSC.ui of A043-8.pcx: the one of convert is checked in)
# (mkrHelpSC.ui of A044-8.pcx: the one of convert is checked in)
geos_cvtpcx(mkrChooseUISC.ui A045-8.pcx -m1 -x1 -y1 -2 -nChooseUISC -w24
            -h24)

geos_cvtpcx(mkrNormalScaleTC.ui A111-8.pcx -m225 -x0 -y0 -2 -nNormalScaleTC
            -w27 -h16 -Stool -stiny)
geos_cvtpcx(mkrScaleToFitTC.ui A112-8.pcx -m225 -x0 -y0 -2 -nScaleToFitTC
            -w18 -h16 -Stool -stiny)
geos_cvtpcx(mkrZoomOutTC.ui A113-8.pcx -m225 -x0 -y0 -2 -nZoomOutTC -w18
            -h16 -Stool -stiny)
geos_cvtpcx(mkrZoomInTC.ui A114-8.pcx -m225 -x0 -y0 -2 -nZoomInTC -w18 -h16
            -Stool -stiny)
geos_cvtpcx(mkrRedrawTC.ui A115-8.pcx -m225 -x0 -y0 -2 -nRedrawTC -w18 -h16
            -Stool -stiny)
geos_cvtpcx(mkrPageLeftTC.ui A116-8.pcx -m225 -x0 -y0 -2 -nPageLeftTC -w18
            -h16 -Stool -stiny)
geos_cvtpcx(mkrPageRightTC.ui A117-8.pcx -m225 -x0 -y0 -2 -nPageRightTC -w18
            -h16 -Stool -stiny)
geos_cvtpcx(mkrPageUpTC.ui A118-8.pcx -m225 -x0 -y0 -2 -nPageUpTC -w18 -h16
            -Stool -stiny)
geos_cvtpcx(mkrPageDownTC.ui A119-8.pcx -m225 -x0 -y0 -2 -nPageDownTC -w18
            -h16 -Stool -stiny)
geos_cvtpcx(mkrApplyToAllTC.ui A120-8.pcx -m225 -x0 -y0 -2 -nApplyToAllTC
            -w18 -h16 -Stool -stiny)
geos_cvtpcx(mkrShowHorizontalTC.ui A121-8.pcx -m225 -x0 -y0 -2
            -nShowHorizontalTC -w18 -h16 -Stool -stiny)
geos_cvtpcx(mkrShowVerticalTC.ui A122-8.pcx -m225 -x0 -y0 -2
            -nShowVerticalTC -w18 -h16 -Stool -stiny)
geos_cvtpcx(mkrFirstPageTC.ui PDF010-4.pcx -m13 -x0 -y0 -2 -nFirstPageTC -w15
            -h15 -Stool -stiny)
geos_cvtpcx(mkrLastPageTC.ui PDF011-4.pcx -m13 -x0 -y0 -2 -nLastPageTC -w15
            -h15 -Stool -stiny)

geos_cvtpcx(mkrUndoTC.ui A129-8.pcx -m225 -x0 -y0 -2 -nUndoTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrCutTC.ui A130-8.pcx -m225 -x0 -y0 -2 -nCutTC -w16 -h16 -Stool
            -stiny)
geos_cvtpcx(mkrCopyTC.ui A131-8.pcx -m225 -x0 -y0 -2 -nCopyTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrPasteTC.ui A132-8.pcx -m225 -x0 -y0 -2 -nPasteTC -w16 -h16
            -Stool -stiny)
geos_cvtpcx(mkrSelectAllTC.ui A133-8.pcx -m225 -x0 -y0 -2 -nSelectAllTC -w17
            -h16 -Stool -stiny)
geos_cvtpcx(mkrDeleteTC.ui A134-8.pcx -m225 -x0 -y0 -2 -nDeleteTC -w17 -h16
            -Stool -stiny)


# made so, as checked in, though in neither script
geos_cvtpcx(mkrChooseUISM.ui PCX/startmenu.pcx -2 -f -t -m13 -x26 -y201 -w24
            -h24 -b -nChooseUISM)
geos_cvtpcx(mkrChooseUITC.ui PCX/startmenu.pcx -2 -f -t -m13 -x51 -y201 -w15
            -h15 -Stool -nChooseUITC)
geos_cvtpcx(mkrChooseUITM.ui PCX/startmenu.pcx -2 -f -t -m13 -x76 -y201 -w15
            -h15 -b -Stool -nChooseUITM)
geos_cvtpcx(mkrDesignAssistantSC.ui A046-8.pcx -2 -f -t -m1 -x1 -y1 -w24
            -h24 -nDesignAssistantSC)
geos_cvtpcx(mkrDesignAssistantTC.ui A046-8.pcx -2 -f -t -m1 -x26 -y1 -w15
            -h15 -Stool -nDesignAssistantTC)
geos_cvtpcx(mkrDialUpTC.ui "PCX\\Dialer.pcx" -2 -Stool -m13 -x0 -y0 -w15
            -h15 -nDialUpTC)
geos_cvtpcx(mkrFirstPage.ui PCX/first.pcx -2 -z -dTM -dTCGA -m13 -x0 -y0
            -nFirstPage)
geos_cvtpcx(mkrLastPage.ui PCX/last.pcx -2 -z -dTM -dTCGA -m13 -x0 -y0
            -nLastPage)
geos_cvtpcx(mkrGPCPreferencesTC.ui PR000-8.pcx -2 -Stool -m1 -x50 -y1 -w15
            -h15 -nGPCPreferencesTC)

geos_art(user)
