##############################################################################
#
# spool32.gp -- the print spool library, Port32 (C) build.
#
# The C port of spool.gp so far: a library with a process (SpoolProcClass)
# that prints the jobs of its queues on threads of their own, without the
# UI (the PrintControl, the PageSizeControl, the control panel, the error
# boxes: see spoolLocal.h).  The entry numbers are those of spool.gp; what
# is not ported yet is skipped and its entry is empty.  The C stubs of the
# asm (SPOOLGETPAPERSIZE ...) are the C routines under a second entry.
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
skip	5	# the controller classes
export	SpoolGetNumPaperSizes
export	SpoolGetPaperString
export	SpoolGetPaperSize
export	SpoolConvertPaperSize
export	SpoolGetPaperSizeOrder
export	SpoolSetPaperSizeOrder
export	SpoolCreatePaperSize
export	SpoolDeletePaperSize
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
skip	3	# SpoolMapToPrinterFont, SpoolUpdateTranslationTable,
		# SPOOLSETDOCSIZE
export	SpoolGetDefaultPageSizeInfo
export	SpoolSetDefaultPageSizeInfo
# the C stubs of the asm are the same C routines
export	SpoolGetDefaultPageSizeInfo as SPOOLGETDEFAULTPAGESIZEINFO
export	SpoolSetDefaultPageSizeInfo as SPOOLSETDEFAULTPAGESIZEINFO
export	SpoolGetNumPaperSizes as SPOOLGETNUMPAPERSIZES
export	SpoolGetPaperString as SPOOLGETPAPERSTRING
export	SpoolGetPaperSize as SPOOLGETPAPERSIZE
export	SpoolConvertPaperSize as SPOOLCONVERTPAPERSIZE
export	SpoolGetPaperSizeOrder as SPOOLGETPAPERSIZEORDER
export	SpoolSetPaperSizeOrder as SPOOLSETPAPERSIZEORDER
export	SpoolCreatePaperSize as SPOOLCREATEPAPERSIZE
export	SpoolDeletePaperSize as SPOOLDELETEPAPERSIZE
export	SpoolGetNumPrinters as SPOOLGETNUMPRINTERS
export	SpoolGetPrinterString as SPOOLGETPRINTERSTRING
export	SpoolCreatePrinter as SPOOLCREATEPRINTER
export	SpoolDeletePrinter as SPOOLDELETEPRINTER
export	SpoolSetDefaultPrinter as SPOOLSETDEFAULTPRINTER
export	SpoolCreateSpoolFile as SPOOLCREATESPOOLFILE
export	SpoolDelJob as SPOOLDELJOB
export	SpoolInfo as SPOOLINFO
export	SpoolHurryJob as SPOOLHURRYJOB
export	SpoolDelayJob as SPOOLDELAYJOB
export	SpoolModifyPriority as SPOOLMODIFYPRIORITY
export	SpoolVerifyPrinterPort as SPOOLVERIFYPRINTERPORT
