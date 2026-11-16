##############################################################################
#
# tspui.gp -- a specific UI library for the library.ui.gen and
# library.ui.system tests (Port32/Tests): the classes the generic objects
# of uigen and uisys are built into.
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
export TSBuildField
export TSBuildScreen
export TSBuildSystem
export tspState
export TSWinClass
export TSButtonClass
export TSAppClass
export TSFieldClass
export TSScreenClass
export TSSystemClass
