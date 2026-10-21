#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The LLVM tools a distribution installs, in LLVMToolchainTools as a CMake list. With LLVM_INSTALL_TOOLCHAIN_ONLY, LLVM installs
# only the tools in LLVM_TOOLCHAIN_TOOLS, and setting that replaces LLVM's default list in llvm/cmake/modules/AddLLVM.cmake, so
# the default list is repeated here.
LLVMToolchainToolsList=(
	# LLVM's default list
	llvm-ar
	llvm-cov
	llvm-cxxfilt
	llvm-dlltool
	llvm-dwp
	llvm-ranlib
	llvm-lib
	llvm-mca
	llvm-ml
	llvm-ml64
	llvm-nm
	llvm-objcopy
	llvm-objdump
	llvm-pdbutil
	llvm-rc
	llvm-readobj
	llvm-size
	llvm-strings
	llvm-strip
	llvm-profdata
	llvm-profgen
	llvm-symbolizer
	addr2line
	ar
	c++filt
	ranlib
	nm
	objcopy
	objdump
	readelf
	size
	strings
	strip
	llvm-mt # The default list adds it when libxml2 is enabled

	# Mach-O object files
	dsymutil
	llvm-lipo
	llvm-otool
	llvm-install-name-tool
	llvm-libtool-darwin
	llvm-bitcode-strip
	llvm-readtapi

	# Debug info
	llvm-dwarfdump
	llvm-dwarfutil
	llvm-gsymutil
	llvm-debuginfod
	llvm-debuginfod-find

	# ELF and other object files
	llvm-readelf
	llvm-addr2line
	llvm-ifs
	llvm-cxxdump
	llvm-cxxmap

	# COFF object files and resources
	llvm-cvtres
	llvm-windres
	llvm-undname
)

LLVMToolchainTools="$(IFS=';'; echo "${LLVMToolchainToolsList[*]}")"
