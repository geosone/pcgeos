##############################################################################
#
# deskjet32.gp -- the HP DeskJet printer driver, Port32 (C) build.
#
# The C port of deskjet.gp: the common printer driver code beside its asm
# in PrintCom/ and the driver's own code here (printcomDriver.h,
# deskjetControlCodes.c).  The device tables are generated from the asm
# (Port32/tools/dmdevinfo2c.py), so there are no info resources.  Not
# ported: the print UI (the options box) and the gamma correction of the
# medium and high resolutions (Color/Correct).
#
##############################################################################
name	deskjet.drvr
longname "HP DeskJet driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
