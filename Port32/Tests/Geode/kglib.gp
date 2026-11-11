##############################################################################
#
# kglib.gp -- a library geode for the kernel.geode test (Port32/Tests):
# loaded by the C kernel's GeodeLoad as the library of kgapp.
#
##############################################################################
name     kglib.lib
longname "Kernel Test Library"
tokenchars "KGLB"
tokenid    0
type   library, single
entry  KGLibEntry
library geos
export KGLibAdd
export KGLibGetCalls as KGLibCalls
export KGCounterClass
