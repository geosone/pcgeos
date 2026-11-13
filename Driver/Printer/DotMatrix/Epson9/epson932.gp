##############################################################################
#
# epson932.gp -- the Epson 9-pin printer driver, Port32 (C) build.
#
# The C port of epson9.gp: the common printer driver code beside its asm
# in PrintCom/ and the driver's own code here (printcomDriver.h,
# epson9ControlCodes.c).  The device and font tables are generated from
# the asm (Port32/tools/dmdevinfo2c.py), so there are no info resources.
# Not ported: the print UI (the options and country boxes) and color
# correction (gamma30, CorrectInk).
#
##############################################################################
name	epson9.drvr
longname "Epson 9-pin driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
