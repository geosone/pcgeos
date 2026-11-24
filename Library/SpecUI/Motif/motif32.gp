##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	Motif specific UI
# FILE:		motif32.gp
#
# DESCRIPTION:	The parameters of motif.geo in C (Port32).  The 16-bit
#		build keeps motif.gp.  The UI calls the first exports by
#		number (SpecificUIRoutine, CInclude/Internal/specUI.h), so
#		they keep motif.gp's order; a routine not ported yet is
#		skipped (its entry is empty: the generic object of that
#		type cannot be built yet).
#
##############################################################################
name motif.spui
type	library, single
library	geos
library	ui
library	text
longname	"Motif \xa8 Specific UI"
tokenchars	"SPUI"
tokenid		0

resource PointerImages lmem read-only shared
resource StandardMonikers lmem read-only shared
resource ResizeRegions lmem read-only shared
resource StandardWindowMenuResource read-only shared object
resource DisplayWindowMenuResource read-only shared object
resource AppSCMonikerResource read-only shared lmem
resource AppSMMonikerResource read-only shared lmem
resource AppSCGAMonikerResource read-only shared lmem
resource AppSCMonikerDialogResource read-only shared lmem
resource AppSMMonikerDialogResource read-only shared lmem
resource AppSCGAMonikerDialogResource read-only shared lmem
resource AppYMMonikerDialogResource read-only shared lmem
resource StandardDialogUI read-only shared object
resource DrawBitmapOptrSCResource read-only shared lmem
resource DrawBitmapOptrSMResource read-only shared lmem
resource DrawBitmapOptrSCGAResource read-only shared lmem

#
# Export routines called by UI, by offset (*must* be first)
#
export OLBuildTrigger
export OLBuildDisplay
export OLBuildApplication
export OLBuildField
export OLBuildScreen
export OLBuildSystem
export OLBuildPane
export OLBuildInteraction
export OLBuildGlyphDisplay
export OLBuildText
export OLBuildDisplayControl
export OLBuildPrimary
export OLBuildGadget
export OLBuildContent
# OLBuildUIDocumentControl, OLBuildAppDocumentControl, OLBuildDocument,
# OLBuildFileSelector
skip 4
export OLBuildBooleanGroup
export OLBuildItemGroup
export OLBuildDynamicList
export OLBuildItem
export OLBuildBoolean
export OLBuildValue

#
# exported routines to do specific UI stuff
#
export SpecGetTextKbdBindings
skip 1
# SpecGetTextPointerImage
export SpecGetDisplayScheme
export SpecDrawTextCursor
skip 1
# SpecGetDocControlOptions
export SpecGetWindowOptions

# the classes
export OLSystemClass
export OLScreenClass
export OLFieldClass
export OLApplicationClass
export OLCtrlClass
export OLWinClass
export OLMenuedWinClass
export OLBaseWinClass
export OLGadgetAreaClass
export OLTitleGroupClass
export OLPopupWinClass
export OLDialogWinClass
export OLReplyBarClass
export OLMenuItemGroupClass
export OLButtonClass
export OLItemGroupClass
export OLItemClass
export OLCheckedItemClass
export OLMenuWinClass
export OLMenuButtonClass
export OLMenuBarClass
export OLWinIconClass
export OLWinGlyphDisplayClass
export OLDisplayGroupClass
export OLDisplayWinClass
export OLScrollbarClass
export OLPaneClass
export OLContentClass
export OLScrollListClass
export OLGlyphDisplayClass
export OLScrollableItemClass
export OLTextClass
export OLGadgetClass
export OLGadgetCompClass
export OLSpinGadgetClass
export OLValueClass
export OLSliderClass
