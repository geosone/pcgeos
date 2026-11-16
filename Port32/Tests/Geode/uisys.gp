##############################################################################
#
# uisys.gp -- a process geode for the library.ui.system test
# (Port32/Tests): the UI's system object with a screen, fields and
# applications, built by the specific UI tspui.
#
##############################################################################
name     uisys.app
longname "UI System Test Process"
tokenchars "UIST"
tokenid    0
type   process, single
class  USProcessClass
appobj USApp
library geos
library ui
resource USObjects object
export usState
export USStep
