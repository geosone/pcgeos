##############################################################################
#
# ffaref.gp -- geode parameters of the FloatFloatToAscii reference app.
#
# The app runs the cases of ffa_cases.h through the 16-bit original of the
# math library, writes FFAREF.TXT in the GEOS top directory and shuts
# GEOS down.  Built and run by Port32/tools/geosref.sh; see README.md.
#
##############################################################################
name     ffaref.app
longname "FFA Reference"
tokenchars "FFAR"
tokenid    0
type	appl, process, single
class	FfaRefProcessClass
appobj	FfaRefApp
heapspace 2000
library	geos
library	ui
library ansic
library math
resource AppResource ui-object
