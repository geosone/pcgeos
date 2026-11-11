# Cross build for GEOS_HOST=linux on armhf (32-bit ARM, hard float) with a
# GNU cross toolchain; tests run through qemu-user.
#
#   cmake -S . -B build-armhf -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-armhf.cmake
#
# GEOS_ARMHF_TRIPLE overrides the toolchain prefix (default: the first of
# armv7a-unknown-linux-gnueabihf, arm-linux-gnueabihf,
# armv6j-hardfloat-linux-gnueabi found on PATH).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT GEOS_ARMHF_TRIPLE)
  foreach(_t armv7a-unknown-linux-gnueabihf arm-linux-gnueabihf
             armv6j-hardfloat-linux-gnueabi)
    find_program(_cc_${_t} ${_t}-gcc)  # one cache entry per candidate
    if(_cc_${_t})
      set(GEOS_ARMHF_TRIPLE ${_t})
      break()
    endif()
  endforeach()
endif()
set(CMAKE_C_COMPILER ${GEOS_ARMHF_TRIPLE}-gcc)

find_program(_qemu qemu-arm)
foreach(_root /usr/${GEOS_ARMHF_TRIPLE} /usr/${GEOS_ARMHF_TRIPLE}/sys-root)
  if(EXISTS ${_root}/lib)
    set(_sysroot ${_root})
    break()
  endif()
endforeach()
if(_qemu)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${_qemu} -L ${_sysroot})
endif()
