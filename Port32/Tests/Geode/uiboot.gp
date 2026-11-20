##############################################################################
#
# uiboot.gp -- an application for the library.ui.boot test (Port32/Tests):
# the UI booted with the Motif specific UI starts it ("[ui]
# execOnStartup"); its primary window holds a display group with a
# document window.
#
##############################################################################
name     uiboot.app
longname "UI Boot Test"
tokenchars "UIBT"
tokenid    0
type   appl, process, single
class  UBProcessClass
appobj UBApp
library geos
library ui
library motif
resource UBAppObjects object
export ubState
export UBStep
