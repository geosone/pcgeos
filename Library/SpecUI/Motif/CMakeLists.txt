# Motif (motif.geo): the code of CommonUI and its own, in C beside the
# asm.  motif32.gp has the parameters; the 16-bit build keeps motif.gp.
add_subdirectory(Win)
add_subdirectory(View)
geos_get_sources(_motif_goc motif_goc)
# The objects of the .ui files (uic -g).  Of cmainManager.ui only the
# resources whose classes are in C so far.
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
         INCLUDES ${_common}/CMain ${_common}
         DEFINITIONS MOTIF)
geos_geode(motif GP motif32.gp
           GOC ${_motif_goc} ${cmainManager_UI_GOC}
           GOC_DEPENDS ${cmainManager_UI_GOH}
           INCLUDES ${_common} ${_common}/CMain ${cmainManager_UI_DIR}
           PROTOCOL 727.0)
