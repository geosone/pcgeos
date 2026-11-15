##############################################################################
#
# uiflow.gp -- a process geode for the library.ui.flow test (Port32/Tests):
# the UI library's flow object between the input manager and two geodes.
#
##############################################################################
name     uiflow.app
longname "UI Flow Test Process"
tokenchars "UIFT"
tokenid    0
type   process, single
class  UFProcessClass
appobj UFAppA
library geos
library ui
resource UFObjects object
export ufState
export UFStep
