# This file sets up a CMakeCache for a simple distribution bootstrap build.

# Only build the native target in stage1 since it is a throwaway build.
set(LLVM_TARGETS_TO_BUILD Native CACHE STRING "")

# Optimize the stage1 compiler, but don't LTO it because that wastes time.
set(CMAKE_BUILD_TYPE Release CACHE STRING "")

# Setup vendor-specific settings.
set(PACKAGE_VENDOR malterlib.org CACHE STRING "")

#Enable LLVM projects and runtimes
set(LLVM_ENABLE_PROJECTS "clang;clang-tools-extra;lld;polly" CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind" CACHE STRING "")

if ($ENV{MalterlibPlatform} MATCHES "Linux" AND $ENV{MalterlibArch} MATCHES "x86")
  set(BOOTSTRAP_LLVM_ENABLE_LTO OFF CACHE BOOL "")

  set(SANITIZER_ALLOW_CXXABI OFF CACHE BOOL "")
  set(LLVM_HOST_TRIPLE i686-unknown-linux-gnu CACHE STRING "")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "-latomic" CACHE STRING "")
  set(CMAKE_SHARED_LINKER_FLAGS_INIT "-latomic" CACHE STRING "")
else()
  if ($ENV{StandaloneBuild} MATCHES "true")
    set(BOOTSTRAP_LLVM_ENABLE_LTO OFF CACHE BOOL "")
  else()
    # Setting up the stage2 LTO option needs to be done on the stage1 build so that
    # the proper LTO library dependencies can be connected.
    set(BOOTSTRAP_LLVM_ENABLE_LTO "Thin" CACHE STRING "")
  endif()
  if (NOT APPLE)
    # Since LLVM_ENABLE_LTO is ON we need a LTO capable linker
    set(BOOTSTRAP_LLVM_ENABLE_LLD "Thin" CACHE BOOL "")
  endif()
endif()

set(COMPILER_RT_INCLUDE_TESTS OFF CACHE BOOL "")
set(DLLVM_BUILD_BENCHMARKS OFF CACHE BOOL "")
set(DLLVM_BUILD_TESTS OFF CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(LIBCLANG_BUILD_STATIC ON CACHE BOOL "")
set(LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")

# Expose stage2 targets through the stage1 build configuration.
set(CLANG_BOOTSTRAP_TARGETS
  check-all
  check-llvm
  check-clang
  llvm-config
  test-suite
  test-depends
  llvm-test-depends
  clang-test-depends
  distribution
  install-distribution
  clang CACHE STRING "")

# Setup the bootstrap build.
set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")

if(STAGE2_CACHE_FILE)
  set(CLANG_BOOTSTRAP_CMAKE_ARGS
    -C ${STAGE2_CACHE_FILE}
    CACHE STRING "")
else()
  set(CLANG_BOOTSTRAP_CMAKE_ARGS
    -C ${CMAKE_CURRENT_LIST_DIR}/Distribution-stage2.cmake
    CACHE STRING "")
endif()
