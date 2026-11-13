##############################################################################
#
# eps32.gp -- the PostScript translation library, Port32 (C) build.
#
# The C port of eps.gp so far: the export routines the PostScript printer
# driver uses (Export/*.c) and the library entry (Lib/libMain.c).  Not
# ported yet: the import side, the export UI, TransExport and the text,
# bitmap and font emitters (Port32/PLAN.md).  The entry numbers are those
# of eps.gp (the TransRoutine numbers, TR_* in xlatPS.h): the printer
# drivers call the library through ProcGetLibraryEntry.  What is not
# ported yet is skipped; its entry is empty.
#
##############################################################################
name	eps.lib
longname "PostScript Translation Library"
tokenchars "TLPS"
tokenid 0
type	library, single
entry	TransLibraryEntry
library	geos
skip	5	# TransGetImportUI .. TransGetImportOptions
export	TransGetExportOptions
skip	3	# TransImport, TransExport, TransGetFormat
export	EPSExportLow
export	EPSExportRaw
export	EPSExportHeader
export	EPSExportTrailer
export	EPSExportBeginPage
export	EPSExportEndPage
export	EPSNormalizeFilename
skip	1	# EPSExportBitmap
