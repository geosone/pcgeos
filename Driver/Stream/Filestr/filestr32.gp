##############################################################################
#
# filestr32.gp -- the file stream driver, Port32 (C) build.
#
# The C port of filestr.gp (filestrMain.c): the port the spooler prints
# to a file through.  Self-contained: it writes the file itself instead
# of going through the Stream driver (see filestrMain.c).
#
##############################################################################
name	filestr.drvr
type	driver, single
library	geos
longname	"File Stream Driver"
tokenchars	"STRD"
tokenid		0
