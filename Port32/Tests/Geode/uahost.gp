##############################################################################
#
# uahost.gp -- a process geode for the library.ui.app test (Port32/Tests):
# the UI's system object with a screen and a field, on which it launches
# the application uiapp.
#
##############################################################################
name     uahost.app
longname "UI App Test Host"
tokenchars "UIAH"
tokenid    0
type   process, single
class  UAHostClass
library geos
library ui
resource UAHostObjects object
export uaHostState
export UAHostStep
