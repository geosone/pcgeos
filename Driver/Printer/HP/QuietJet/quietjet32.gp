##############################################################################
#
# quietjet32.gp -- the HP QuietJet printer driver, Port32 (C) build.
#
# The C port of quietjet.gp: the common printer driver code beside its asm in
# PrintCom/ and the driver's own code here (printcomDriver.h,
# quietjetControlCodes.c, made by Port32/tools/ctlcodes2c.py).  The device
# tables are generated from the asm (Port32/tools/dmdevinfo2c.py), so
# there are no info resources.  Not ported: the print UI (the options
# box).
#
##############################################################################
name	quietjet.drvr
longname "HP QuietJet driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
