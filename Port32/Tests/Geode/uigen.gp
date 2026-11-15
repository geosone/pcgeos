##############################################################################
#
# uigen.gp -- a process geode for the library.ui.gen test (Port32/Tests):
# a generic tree of the UI library, built by the specific UI tspui.
#
##############################################################################
name     uigen.app
longname "UI Gen Test Process"
tokenchars "UIGT"
tokenid    0
type   process, single
class  UGProcessClass
appobj UGApp
library geos
library ui
resource UGObjects object
export ugState
export UGStep
