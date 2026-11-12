##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	CUPS port driver
# FILE:		cups.gp
#
# Parameters file for: cups.geo -- a custom port of the spooler that
# hands the printer driver's output to a CUPS queue of the host (GAPI).
#
##############################################################################
name	cups.drvr
type	driver, single
library	geos
longname	"CUPS Port Driver"
tokenchars	"STRD"
tokenid		0
