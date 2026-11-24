##############################################################################
#
# gocres.gp -- geode parameters of the goc -cg test (Port32/Tests/Goc).
# Tools/gpres makes the resource numbers from it.
#
##############################################################################
name gocres.app
longname "goc -cg test"
type appl, process, single
class GocTestProcessClass
appobj GocTestApp
library geos
library ui
resource AppResource ui-object
resource Interface ui-object
