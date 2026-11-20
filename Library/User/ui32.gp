##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	User Interface library
# FILE:		ui32.gp
#
# DESCRIPTION:	The parameters of ui.geo in C (Port32).  The 16-bit build
#		keeps ui.gp.  Port32 imports by name, so the exports here
#		grow with the port, in the order of ui.gp.
#
##############################################################################
name ui.lib
type	appl, library, process, single
class	UserClass
appobj	UIApp
entry	UILibraryEntry
library	geos
longname	"Generic User Interface"
tokenchars	"UI00"
tokenid		0

export VisEmptyClass
export VisClass
export VisCompClass
export VisContentClass
export FlowClass
export GenClass
export GenApplicationClass
export GenFieldClass
export GenScreenClass
export GenSystemClass
export GenTriggerClass
export GenInteractionClass
export GenDisplayClass
export GenDisplayGroupClass
export GenPrimaryClass
export GenBooleanGroupClass
export GenItemGroupClass
export GenItemClass
export GenBooleanClass
export GenViewClass
export GenContentClass
export GenProcessClass
export UserClass

# the Vis routines in C (visC.goh, GEOS_PORT32)
export VisCheckIfVisGrown
export VisCheckIfSpecBuilt
export VisGetSize
export VisGetBounds
export VisGetBoundsInsideMargins
export VisSetSize
export VisSetPosition
export VisGetCenter
export VisTestPointInBounds
export VisFindParent
export VisFindParentOfVisType
export VisQueryWindow
export VisQueryParentWin
export VisGetParentGeometry
export VisCallParent
export VisSendToChildren
export VisIfFlagSetCallVisChildren
export VisIfFlagSetCallGenChildren
export VisMarkInvalid
export VisMarkInvalidOnParent
export VisMarkFullyInvalid
export VisRemove
export VisCallChildUnderPoint
export VisGrabMouse
export VisForceGrabMouse
export VisGrabLargeMouse
export VisForceGrabLargeMouse
export VisReleaseMouse
export VisGrabKbd
export VisForceGrabKbd
export VisReleaseKbd
export VisTakeGadgetExclAndGrab
export VisDrawMoniker
export VisGetMonikerPos
export VisGetMonikerSize
export VisConvertSpecVisSize
export VisSetupSizeArgs
export VisApplyInitialSizeArgs
export VisApplySizeArgsToWidth
export VisApplySizeArgsToHeight
export VisApplySizeHints
export VisSpecBuildSetEnabledState
export VisGetVisParent
export VisAddChildRelativeToGen
export VisNavigateCommon
export VisCheckMnemonic
export VisFindMoniker
export VisConvertRatioToCoords
export VisRecalcSizeAndInvalIfNeeded
export VisSendPositionAndInvalIfNeeded
export VisSendCenter
export VisGetParentCenter
export VisCompRecalcSize
export VisCompPosition
export VisCompGetCenter

# the Flow routines (uiInputC.goh)
export FlowForceGrab
export FlowRequestGrab
export FlowReleaseGrab
export FlowAlterHierarchicalGrab
export FlowUpdateHierarchicalGrab
export FlowGetTargetAtTargetLevel
export FlowDestroyMessage
export FlowMessageClassedEvent
export FlowDispatchSendOnOrDestroyClassedEvent
export MetaGrabFocusExclLow
export MetaReleaseFocusExclLow
export MetaGrabTargetExclLow
export MetaReleaseTargetExclLow
export MetaGrabModelExclLow
export MetaReleaseModelExclLow
export MetaReleaseFTExclLow
export FlowTranslatePassiveButton
export FlowCheckKbdShortcut
export FlowGetUIButtonFlags

# the Gen routines (genC.goh, GEOS_PORT32)
export GenCopyChunk
export GenInsertChild
export GenSetUpwardLink
export GenRemoveDownwardLink
export GenSpecShrink
export GenProcessGenAttrsBeforeAction
export GenProcessGenAttrsAfterAction
export GenFindObjectInTree
export GenCheckIfSpecGrown
export GenCallParent
export GenSendToChildren
export GenSpecShrinkBranch
export GenSpecGrowParents
export GenCheckIfFullyUsable
export GenCheckIfFullyEnabled
export GenCheckKbdAccelerator
export GenQueryUICallSpecificUI
export UserGetSpecificUI
export UserFreeSpecificUI
export UserCallSystem

# the system object and the UI's state (gSysC.goh, ui.goh)
export UserCreateSystemObject
export UserDestroySystemObject
export UserGetDisplayType
export UserGetDefaultUILevel
export UserGetDefaultLaunchLevel
export UserGetLaunchModel
export UserGetLaunchOptions
export UserGetInterfaceOptions

# the UI's process (userMain.goc, userScreen.goc, userUtils.goc)
export UserLoadApplication
export UserAllocObjBlock
export UserScreenRegister
export UserGetFlowObject
export UserGetSystemObject
export UserSetDefaultMonikerFont
export UserGetDefaultMonikerFont
export UserSendToApplicationViaProcess
export UserGetKbdAcceleratorMode

resource ApplicationUI object
resource SystemFieldUI object
