##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	Text library
# FILE:		text32.gp
#
# DESCRIPTION:	The parameters of text.geo in C (Port32).  The 16-bit
#		build keeps text.gp.  Port32 imports by name, so the
#		exports here grow with the port, in the order of text.gp.
#
##############################################################################
name text.lib
type	library, single
library	geos
library	ui
library	styles
entry	TextLibraryEntry
longname	"Text Library"
tokenchars	"TEXL"
tokenid		0

resource TextTransStrings lmem read-only shared
resource TextStyleStrings lmem read-only shared

export VisTextClass
export TextAllocClipboardObject
export TextFinishWithClipboardObject
export VisLargeTextClass
export VTFClearSmartQuotes
export MsgVisTextLoadStyleSheetParams
