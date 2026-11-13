##############################################################################
#
# spool32.gp -- the print spool library, Port32 (C) build.
#
# The C port of spool.gp so far: a library with a process (SpoolProcClass)
# that prints the jobs of its queues on threads of their own, without the
# UI (the PrintControl, the PageSizeControl, the control panel, the error
# boxes: see spoolLocal.h).  The entry numbers are those of spool.gp; what
# is not ported yet is skipped and its entry is empty.
#
##############################################################################
name	spool.lib
longname "Print Spool Library"
tokenchars "SL00"
tokenid 0
type	process, library, single
class	SpoolProcClass
entry	SpoolLibraryEntry
library	geos
skip	13	# the controller classes, the paper size routines
export	SpoolGetNumPrinters
export	SpoolGetPrinterString
export	SpoolGetPrinterInfo
export	SpoolCreatePrinter
export	SpoolDeletePrinter
export	SpoolGetDefaultPrinter
export	SpoolSetDefaultPrinter
export	SpoolCreateSpoolFile
skip	1	# SpoolSetDocSize
export	SpoolAddJob
export	SpoolDelJob
export	SpoolInfo
export	SpoolHurryJob
export	SpoolDelayJob
export	SpoolModifyPriority
export	SpoolVerifyPrinterPort
