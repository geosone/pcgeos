##############################################################################
#
# uivis.gp -- a process geode for the library.ui.vis test (Port32/Tests):
# a visual tree of the UI library in a window.
#
##############################################################################
name     uivis.app
longname "UI Vis Test Process"
tokenchars "UIVT"
tokenid    0
type   process, single
class  UVProcessClass
appobj UVTop
library geos
library ui
resource UVObjects object
export uvState
export UVStep
