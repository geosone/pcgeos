##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	Host power management driver
# FILE:		hostpwr.gp
#
# DESCRIPTION:	The power of the host (GAPI gapi_power_status): the
#		mains, the batteries and the charge left.
#
##############################################################################
name	hostpwr.drvr
type	driver, single, system
library	geos
longname	"Host Power Management Driver"
tokenchars	"PWRD"
tokenid		0
