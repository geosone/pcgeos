##############################################################################
#
# pscript32.gp -- the PostScript printer driver, Port32 (C) build.
#
# The C port of pscript.gp: the common printer driver code beside its asm
# in PrintCom/ and the driver's own code here (pscript.h).  The device
# tables are generated from the asm (Port32/tools/psdevinfo2c.py), so
# there are no info resources.  The translation library (eps.geo) is
# loaded per job, as "library eps noload" meant.  Not ported: the spool
# library's UI (no import needed yet) and ps2pdf (the host prints through
# the CUPS port driver instead).
#
##############################################################################
name	pscript.drvr
longname "PostScript driver"
tokenchars "PRDR"
tokenid 0
type	driver, single
library	geos
