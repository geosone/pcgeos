##############################################################################
#
# eplx932.gp -- the Epson LX 9-pin printer driver, Port32 (C) build.
#
# The C port of eplx9.gp: the common printer driver code beside its asm in
# PrintCom/ and the driver's own code here (printcomDriver.h,
# eplx9ControlCodes.c, made by Port32/tools/ctlcodes2c.py).  The device
# and font tables are generated from the asm (Port32/tools/dmdevinfo2c.py),
# so there are no info resources.  Not ported: the print UI (the options
# box) and color correction.
#
##############################################################################
name	eplx9.drvr
longname "Epson LX 9-pin driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
