# cvtpcx, PCX pictures to monikers (.ui) for uic: the pictures of the
# specific UIs (Library/SpecUI/CommonUI/CArt) are made at build time.
add_executable(cvtpcx cvtpcx.c)
target_link_libraries(cvtpcx PRIVATE geos_tool_utils)
