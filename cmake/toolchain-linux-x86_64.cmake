# Build for GEOS_HOST=linux on amd64 (x86_64).  On an amd64 host this is
# the native build and needs no toolchain file; the file names the target
# explicitly, for a build from another host (a GNU cross toolchain, tests
# through qemu-user) or with the -m64 multilib of an x86 compiler.
#
#   cmake -S . -B build-amd64 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-x86_64.cmake
#
# GEOS_X86_64_TRIPLE overrides the toolchain prefix (default: the first of
# x86_64-pc-linux-gnu, x86_64-unknown-linux-gnu, x86_64-linux-gnu found on
# PATH).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT GEOS_X86_64_TRIPLE)
  foreach(_t x86_64-pc-linux-gnu x86_64-unknown-linux-gnu x86_64-linux-gnu)
    find_program(_cc_${_t} ${_t}-gcc)  # one cache entry per candidate
    if(_cc_${_t})
      set(GEOS_X86_64_TRIPLE ${_t})
      break()
    endif()
  endforeach()
endif()
if(GEOS_X86_64_TRIPLE)
  set(CMAKE_C_COMPILER ${GEOS_X86_64_TRIPLE}-gcc)
endif()
set(CMAKE_C_FLAGS_INIT "-m64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m64")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m64")

# not on an amd64 host: run the tests through qemu-user
cmake_host_system_information(RESULT _host_arch QUERY OS_PLATFORM)
if(NOT _host_arch MATCHES "x86_64|AMD64|amd64")
  find_program(_qemu qemu-x86_64)
  foreach(_root /usr/${GEOS_X86_64_TRIPLE} /usr/${GEOS_X86_64_TRIPLE}/sys-root)
    if(EXISTS ${_root}/lib)
      set(_sysroot ${_root})
      break()
    endif()
  endforeach()
  if(_qemu AND _sysroot)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${_qemu} -L ${_sysroot})
  elseif(_qemu)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${_qemu})
  endif()
endif()
