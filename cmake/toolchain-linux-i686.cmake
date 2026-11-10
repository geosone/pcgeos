# 32-bit x86 Linux build with the native multilib compiler (gcc/clang -m32).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
