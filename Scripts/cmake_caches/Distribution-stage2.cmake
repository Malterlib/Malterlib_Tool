# This file sets up a CMakeCache for the second stage of a simple distribution
# bootstrap build.

set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")

set(LLVM_ENABLE_PROJECTS "clang;clang-tools-extra;lld;polly" CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind" CACHE STRING "")

if ($ENV{MalterlibPlatform} MATCHES "Linux" AND $ENV{MalterlibArch} MATCHES "x86")
  set(BOOTSTRAP_LLVM_ENABLE_LTO OFF CACHE BOOL "")
  set(SANITIZER_ALLOW_CXXABI OFF CACHE BOOL "")
  set(LLVM_TARGETS_TO_BUILD X86 CACHE STRING "")
  set(LLVM_HOST_TRIPLE i686-unknown-linux-gnu CACHE STRING "")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "-latomic" CACHE STRING "")
  set(CMAKE_SHARED_LINKER_FLAGS_INIT "-latomic" CACHE STRING "")
else()
  set(LLVM_TARGETS_TO_BUILD X86;ARM;AArch64 CACHE STRING "")
  if ($ENV{StandaloneBuild} MATCHES "true")
    set(BOOTSTRAP_LLVM_ENABLE_LTO OFF CACHE BOOL "")
  else()
    set(BOOTSTRAP_LLVM_ENABLE_LTO ON CACHE BOOL "")
  endif()
endif()

set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "")

set(COMPILER_RT_INCLUDE_TESTS OFF CACHE BOOL "")
set(DLLVM_BUILD_BENCHMARKS OFF CACHE BOOL "")
set(DLLVM_BUILD_TESTS OFF CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(LIBCLANG_BUILD_STATIC ON CACHE BOOL "")
set(LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")

# setup toolchain
set(LLVM_TOOLCHAIN_TOOLS
  dsymutil
  llvm-cov
  llvm-dwarfdump
  llvm-profdata
  llvm-objdump
  llvm-nm
  llvm-ar
  llvm-install-name-tool
  llvm-ranlib
  llvm-size
  llvm-strip
  llvm-lipo
  llvm-mca
  llvm-objcopy
  llvm-objdump
  CACHE STRING "")

set(LLVM_DISTRIBUTION_COMPONENTS
  clang
  LTO
  clang-check
  clang-tidy
  clang-format
  clang-resource-headers
  builtins
  runtimes
  ${LLVM_TOOLCHAIN_TOOLS}
  lld
  clangd
  compiler-rt
  CACHE STRING "")
