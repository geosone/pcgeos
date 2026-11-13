##############################################################################
#
# dj500c32.gp -- the HP DeskJet CMY printer driver, Port32 (C) build.
#
# The C port of dj500c.gp: the common printer driver code beside its asm in
# PrintCom/ and the driver's own code here (printcomDriver.h,
# dj500cControlCodes.c, made by Port32/tools/ctlcodes2c.py).  The device
# tables are generated from the asm (Port32/tools/dmdevinfo2c.py), so
# there are no info resources.  Not ported: the print UI (the options
# box) and the color correction table.
#
##############################################################################
name	dj500c.drvr
longname "HP DeskJet CMY driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
