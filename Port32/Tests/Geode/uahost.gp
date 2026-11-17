##############################################################################
#
# uahost.gp -- a process geode for the library.ui.app test (Port32/Tests):
# it checks the system the UI's process started and launches the
# application uiapp through the UI (UserLoadApplication).
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
export uaHostState
export UAHostStep
