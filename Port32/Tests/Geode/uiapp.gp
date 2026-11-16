##############################################################################
#
# uiapp.gp -- an application for the library.ui.app test (Port32/Tests):
# a process of GenProcessClass with its application object, launched,
# quit and detached on the field of the process geode uahost.
#
##############################################################################
name     uiapp.app
longname "UI Application Test"
tokenchars "UIAP"
tokenid    0
type   appl, process, single
class  UAProcessClass
appobj UAApp
library geos
library ui
resource UAAppObjects object
