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
type	library, single
library	geos
longname	"Generic User Interface"
tokenchars	"UI00"
tokenid		0

export VisEmptyClass
export VisClass
export VisCompClass

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
