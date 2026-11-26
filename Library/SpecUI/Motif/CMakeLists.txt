# Motif (motif.geo): the code of CommonUI and its own, in C beside the
# asm.  motif32.gp has the parameters; the 16-bit build keeps motif.gp.
add_subdirectory(Win)
add_subdirectory(View)
geos_get_sources(_motif_goc motif_goc)
# The objects of the .ui files (uic -g).  Of cmainManager.ui only the
# resources whose classes are in C so far.  The binary directory first:
# "../CommonUI/CArt/mkr*.ui" are then the pictures cvtpcx made of the
# PCX files (CArt/CMakeLists.txt), not the ones checked in.
set(_common ${PROJECT_SOURCE_DIR}/Library/SpecUI/CommonUI)
geos_uic(cmainManager ${_common}/CMain/cmainManager.ui
         PROLOG ${_common}/CMain/cmainManagerUI.goh
         RESOURCES AppTCMonikerResource AppTMMonikerResource
                   AppTCGAMonikerResource OLDocumentControlUI
                   OLDocumentControlToolboxUI ControlStrings
                   DocumentDialogUI DocumentOpenUI DocumentUseTemplateUI
                   DocumentSaveAsUI DocumentCopyToUI DocumentMoveToUI
                   DocumentTypeUI DocumentPasswordUI DocumentStringsUI
                   DocumentNewUI DocumentGetPasswordUI
                   DocumentProtoProgressUI DocumentConfirmSaveUI
                   DocumentUserNotesUI DocumentRenameUI
                   DocumentQuickBackupUI LowDiskUI
         INCLUDES ${CMAKE_CURRENT_BINARY_DIR} ${_common}/CMain ${_common}
         DEFINITIONS MOTIF
         DEPENDS cart_art)
# Of cspecCUAS.ui the monikers and pictures (also those of the standard
# dialogs, cspecFile.ui) and the window menus; StandardDialogUI is
# CSpec/cspecStdDialog.goc.  The binary directory first, as above; the
# source directory for "../CommonUI/CSpec/mkr*.ui".  The Esp constants
# the conditions in its gstrings use, as CommonUI/cConstant.def has them
# for Motif (uic -g decides them).  HINT_SYS_ICON and
# ATTR_SYSTEM_MENU_CLOSE (OLButtonClass's, on GenTriggers) are not
# written: CWin/cwinClassCUAS.goc adds them to each window's copy.
geos_uic(cspecCUAS ${_common}/CSpec/cspecCUAS.ui
         PROLOG ${_common}/cGeode.goh
         RESOURCES AppSCMonikerResource AppSMMonikerResource
                   AppSCGAMonikerResource AppYMMonikerResource
                   DrawBitmapOptrSCResource DrawBitmapOptrSMResource
                   DrawBitmapOptrSCGAResource AppFileSCMonikerResource
                   AppFileSMMonikerResource AppFileSCGAMonikerResource
                   StandardMonikers StandardWindowMenuResource
                   DisplayWindowMenuResource
         INCLUDES ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
                  ${_common}
         DEFINITIONS MOTIF THREE_DIMENSIONAL_BORDERS=0
         DEPENDS cart_art
         OMIT_VARDATA HINT_SYS_ICON ATTR_SYSTEM_MENU_CLOSE)
geos_geode(motif GP motif32.gp
           GOC ${_motif_goc} ${cmainManager_UI_GOC} ${cspecCUAS_UI_GOC}
           GOC_DEPENDS ${cmainManager_UI_GOH} ${cspecCUAS_UI_GOH}
           INCLUDES ${_common} ${_common}/CMain ${cmainManager_UI_DIR}
           PROTOCOL 727.0)
