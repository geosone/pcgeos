##############################################################################
#
# eps32.gp -- the PostScript translation library, Port32 (C) build.
#
# The C port of eps.gp so far: the export routines the PostScript printer
# driver uses (Export/*.c) and the library entry (Lib/libMain.c).  Not
# ported yet: the import side, the export UI, TransExport and the text,
# bitmap and font emitters (Port32/PLAN.md).  Imports resolve by name,
# so the order of the exports does not follow eps.gp.
#
##############################################################################
name	eps.lib
longname "PostScript Translation Library"
tokenchars "TLPS"
tokenid 0
type	library, single
entry	TransLibraryEntry
library	geos
export	EPSExportLow
export	EPSExportRaw
export	EPSExportHeader
export	EPSExportTrailer
export	EPSExportBeginPage
export	EPSExportEndPage
export	EPSNormalizeFilename
export	TransGetExportOptions
