##############################################################################
#
# stream32.gp -- the stream driver, Port32 (C) build (stream.c).
#
# A library too: the drivers that work on a stream directly (the sound
# library) import its routines ("library stream" in place of glue's
# "driver stream").
#
##############################################################################
name	stream.drvr
type	driver, library, single
library	geos
longname	"Stream Driver"
tokenchars	"STRD"
tokenid		0
export	StreamNotify
export	StreamStrategy
export	StreamWriteDataNotify
export	StreamReadDataNotify
export	StreamShutdown
export	StreamFree
