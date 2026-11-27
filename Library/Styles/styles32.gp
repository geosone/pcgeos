##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	Styles library
# FILE:		styles32.gp
#
# DESCRIPTION:	The parameters of styles.geo in C (Port32).  The 16-bit
#		build keeps styles.gp.  Port32 imports by name, so the
#		exports here grow with the port, in the order of styles.gp
#		(StyleSheetControlClass comes with the controllers).
#
##############################################################################
name styles.lib

library geos
library ui

type	library, single

longname	"Styles Library"
tokenchars	"STYL"
tokenid		0

resource ControlStrings		lmem read-only shared

export StyleSheetCopyElement
export StyleSheetImportStyles
export StyleSheetDescribeStyle
export StyleSheetDescribeAttrs
export StyleSheetGetStyle
export StyleSheetGetStyleCounts
export StyleSheetRequestEntryMoniker
export StyleSheetUpdateModifyBox
export StyleSheetModifyStyle
export StyleSheetDeleteStyle
export StyleSheetDefineStyle
export StyleSheetRedefineStyle
export StyleSheetGetStyleToApply

export StyleSheetGetNotifyCounter
export StyleSheetIncNotifyCounter

export StyleSheetGenerateChecksum
export StyleSheetCallDescribeRoutines
export StyleSheetCallMergeRoutines

export StyleSheetDescribeExclusiveWord
export StyleSheetDescribeNonExclusiveWord

export StyleSheetDescribeWWFixed
export StyleSheetDescribeDistance
export StyleSheetAddNameFromChunk
export StyleSheetAddCharToDescription
export StyleSheetAddAttributeHeader
export StyleSheetLockStyleChunk
export StyleSheetUnlockStyleChunk
export StyleSheetOpenFileForImport
export StyleSheetAddNameFromPtr
export StyleSheetAddWord
export StyleSheetSaveStyle
export StyleSheetPrepareForRecallStyle
