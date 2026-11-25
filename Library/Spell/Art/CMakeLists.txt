# The pictures of Library/Spell (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert and
# convert2.bat in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked
# in are the 16-bit build's; the ctest library.art.spell checks that
# both are the same.
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrSpell.ui newdict.pcx -2 -f -m13 -t -z -x0 -y0 -nSpell)

# convert2.bat
geos_cvtpcx(mkrSpellTC.ui A100-8.pcx -m225 -x0 -y0 -2 -nSpellTC -w24 -h16
            -Stool -stiny)

geos_art(spell)
