##############################################################################
#
# canon4832.gp -- the Canon BJ-130 48-jet printer driver, Port32 (C) build.
#
# The C port of canon48.gp: the common printer driver code beside its asm in
# PrintCom/ and the driver's own code here (printcomDriver.h,
# canon48ControlCodes.c, made by Port32/tools/ctlcodes2c.py).  The device
# and font tables are generated from the asm (Port32/tools/dmdevinfo2c.py),
# so there are no info resources.  Not ported: the print UI (the options
# box) and color correction.
#
##############################################################################
name	canon48.drvr
longname "Canon BJ-130 48-jet driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
