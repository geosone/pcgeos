##############################################################################
#
# pcl432.gp -- the HP PCL 4 printer driver, Port32 (C) build.
#
# The C port of pcl4.gp: the common printer driver code beside its asm
# in PrintCom/ and the driver's own code here (printcomDriver.h,
# pcl4ControlCodes.c, pcl4Tables.c).  The device tables are generated from the asm
# (Port32/tools/dmdevinfo2c.py), so there are no info resources.  Not
# ported: the print UI (the main and options boxes).
#
##############################################################################
name	pcl4.drvr
longname "HP PCL Level 4 driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
library	spool
