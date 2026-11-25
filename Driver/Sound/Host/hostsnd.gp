##############################################################################
#
# PROJECT:	PC GEOS (Port32)
# MODULE:	Host sound driver
# FILE:		hostsnd.gp
#
# DESCRIPTION:	The sound of the host (GHAL ghal_audio: PipeWire on
#		Linux): FM voices with the Sound Blaster's instruments and
#		DACs for sample streams, mixed in software.
#
##############################################################################
name	hostsnd.drvr
type	driver, single
library	geos
library	stream
longname	"Host Sound Driver"
tokenchars	"SNDD"
tokenid		0
