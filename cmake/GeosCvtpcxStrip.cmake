# GeosCvtpcxStrip.cmake -- cmake -D FILE=<file.ui> -P GeosCvtpcxStrip.cmake
# The chunk name and the braces of a cvtpcx -g picture taken out, as
# Library/Kernel/Art/convert does with sed after cvtpcx (geos_cvtpcx
# STRIP_CHUNK, GeosCvtpcx.cmake):
#   sed -e 's/chunk [a-zA-Z_]*Moniker = data {//' | sed -e 's/}//'
file(READ ${FILE} t)
string(REGEX REPLACE "chunk [a-zA-Z_]*Moniker = data {" "" t "${t}")
string(REGEX REPLACE "}" "" t "${t}")
file(WRITE ${FILE} "${t}")
