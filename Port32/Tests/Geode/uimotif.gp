##############################################################################
#
# uimotif.gp -- a process geode for the library.ui.motif test
# (Port32/Tests): the UI's system object with a screen and a field,
# built by the Motif specific UI (motif.geo).  No application object:
# the specific UI asks the geode's for its size (OLApplicationClass is
# not ported yet); UMApp only holds the objects.
#
##############################################################################
name     uimotif.app
longname "UI Motif Test Process"
tokenchars "UIMT"
tokenid    0
type   process, single
class  UMProcessClass
library geos
library ui
library motif
resource UMObjects object
export umState
export UMStep
