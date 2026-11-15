##############################################################################
#
# tspui.gp -- a specific UI library for the library.ui.gen test
# (Port32/Tests): the classes generic objects of uigen are built into.
# The first exports are the build routines in SpecificUIRoutine order
# (Internal/specUI.h).
#
##############################################################################
name     tspui.lib
longname "Test Specific UI"
tokenchars "TSPU"
tokenid    0
type   library, single
library geos
library ui
export TSBuildTrigger
export TSBuildDisplay
export TSBuildApplication
export tspState
export TSWinClass
export TSButtonClass
export TSAppClass
