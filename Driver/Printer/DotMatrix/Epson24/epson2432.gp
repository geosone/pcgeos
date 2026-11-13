##############################################################################
#
# epson2432.gp -- the Epson 24-pin printer driver, Port32 (C) build.
#
# The C port of epson24.gp: the common printer driver code beside its asm
# in PrintCom/ and the driver's own code here (printcomDriver.h,
# epson24ControlCodes.c).  The device and font tables are generated from
# the asm (Port32/tools/dmdevinfo2c.py), so there are no info resources.
# Not ported: the print UI (the options and country boxes) and color
# correction (CorrectInk).
#
##############################################################################
name	epson24.drvr
longname "Epson 24-pin driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
