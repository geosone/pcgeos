##############################################################################
#
# tosh2432.gp -- the Toshiba 24-pin printer driver, Port32 (C) build.
#
# The C port of tosh24.gp: the common printer driver code beside its asm in
# PrintCom/ and the driver's own code here (printcomDriver.h,
# toshiba24ControlCodes.c, made by Port32/tools/ctlcodes2c.py).  The device
# and font tables are generated from the asm (Port32/tools/dmdevinfo2c.py),
# so there are no info resources.  Not ported: the print UI (the options
# box) and color correction.
#
##############################################################################
name	tosh24.drvr
longname "Toshiba 24-pin driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
