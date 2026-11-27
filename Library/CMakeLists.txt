# The libraries in C (each beside its asm).
add_subdirectory(Math)
add_subdirectory(Kernel)
add_subdirectory(Trans)
add_subdirectory(Spool)
add_subdirectory(Sound)
add_subdirectory(User)
add_subdirectory(SpecUI)
add_subdirectory(Styles)
add_subdirectory(Text)

# The pictures (cvtpcx, cmake/GeosCvtpcx.cmake) of the libraries not in
# C yet; those in C add their own.
foreach(_art AccPnt Bitmap Chart Color ConView GrObj IDialC ParentC Pen
             Pref Pref/HPrefCN Pref/Prefbg Pref/Prefint Pref/Prefintl
             Pref/Prefkbd Pref/Preflang Pref/Preflf Pref/Preflink Pref/Preflo
             Pref/Preflvl Pref/Prefmous Pref/Prefos Pref/PrefPntC Pref/Prefsnd
             Pref/Preftd Pref/Prefts Pref/Prefvid Pref/TweakUI Spell Spline
             Spreadsheet)
  add_subdirectory(${_art}/Art)
endforeach()
