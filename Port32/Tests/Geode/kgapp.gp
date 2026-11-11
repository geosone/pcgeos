##############################################################################
#
# kgapp.gp -- a process geode for the kernel.geode test (Port32/Tests):
# loaded by the C kernel's GeodeLoad, uses the library kglib.
#
##############################################################################
name     kgapp.app
longname "Kernel Test Process"
tokenchars "KGAP"
tokenid    0
type   process, single
class  KGAppProcessClass
appobj KGAppCounter
library geos
library kglib
resource KGObjects object
resource KGData lmem
export kgAppState
