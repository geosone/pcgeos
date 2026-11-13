##############################################################################
#
# mercury32.gp -- the HP DeskJet KCMY printer driver, Port32 (C) build.
#
# The C port of mercury.gp: the common printer driver code beside its asm in
# PrintCom/ and the driver's own code here (printcomDriver.h,
# mercuryControlCodes.c, made by Port32/tools/ctlcodes2c.py).  The device
# tables are generated from the asm (Port32/tools/dmdevinfo2c.py), so
# there are no info resources.  Not ported: the print UI (the options
# box) and the color correction table.
#
##############################################################################
name	mercury.drvr
longname "HP DeskJet KCMY driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
