# Cross build for GEOS_HOST=linux on arm64 (aarch64) with a GNU cross
# toolchain; tests run through qemu-user.
#
#   cmake -S . -B build-arm64 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-aarch64.cmake
#
# GEOS_AARCH64_TRIPLE overrides the toolchain prefix (default: the first of
# aarch64-unknown-linux-gnu, aarch64-linux-gnu found on PATH).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT GEOS_AARCH64_TRIPLE)
  foreach(_t aarch64-unknown-linux-gnu aarch64-linux-gnu)
    find_program(_cc ${_t}-gcc)
    if(_cc)
      set(GEOS_AARCH64_TRIPLE ${_t})
      break()
    endif()
  endforeach()
endif()
set(CMAKE_C_COMPILER ${GEOS_AARCH64_TRIPLE}-gcc)

find_program(_qemu qemu-aarch64)
foreach(_root /usr/${GEOS_AARCH64_TRIPLE} /usr/${GEOS_AARCH64_TRIPLE}/sys-root)
  if(EXISTS ${_root}/lib)
    set(_sysroot ${_root})
    break()
  endif()
endforeach()
if(_qemu)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${_qemu} -L ${_sysroot})
endif()
