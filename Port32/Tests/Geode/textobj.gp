##############################################################################
#
# textobj.gp -- a process geode for the library.text test (Port32/Tests):
# text objects of the text library.
#
##############################################################################
name     textobj.app
longname "Text Test Process"
tokenchars "TXOT"
tokenid    0
type   process, single
class  TOProcessClass
library geos
library ui
library text
library styles
resource TOObjects object
export toState
export TOStep
