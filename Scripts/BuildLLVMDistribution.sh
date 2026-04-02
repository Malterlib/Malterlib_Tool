#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e

cd "$( dirname "${BASH_SOURCE[0]}" )"
echo Directory: $PWD
ScriptDir="$PWD"

pushd ../../..
	MalterlibRoot="$PWD"
popd

source "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh"
DistributionDir="$MalterlibRoot/Binaries/MalterlibLLVM/$MalterlibPlatform/$MalterlibArch"

# Install dependencies for Linux
InstallLinuxDependencies()
{
	echo "Installing Linux build dependencies..."

	if command -v apt-get &> /dev/null; then
		sudo apt-get update
		sudo apt-get install -y \
			build-essential \
			cmake \
			ninja-build \
			python3 \
			python3-dev \
			libcurl4-openssl-dev \
			libzstd-dev \
			libedit-dev \
			libncurses5-dev \
			swig \
			libxml2-dev \
			zlib1g-dev \
			binutils-dev \
			libpfm4-dev

		# Install dev packages for all installed Python3 versions
		PYTHON_DEV_PACKAGES=""
		for pyver in $(ls /usr/bin/python3.* 2>/dev/null | grep -oP 'python3\.\d+' | sort -u); do
			PYTHON_DEV_PACKAGES="$PYTHON_DEV_PACKAGES ${pyver}-dev"
		done
		if [ -n "$PYTHON_DEV_PACKAGES" ]; then
			echo "Installing Python dev packages: $PYTHON_DEV_PACKAGES"
			sudo apt-get install -y $PYTHON_DEV_PACKAGES
		fi

		# Install the latest available Lua dev package (prefer 5.4, then 5.3)
		if apt-cache show lua5.4-dev &> /dev/null; then
			echo "Installing Lua 5.4"
			sudo apt-get install -y lua5.4 lua5.4-dev
		elif apt-cache show lua5.3-dev &> /dev/null; then
			echo "Installing Lua 5.3"
			sudo apt-get install -y lua5.3 lua5.3-dev
		fi

	elif command -v dnf &> /dev/null; then
		sudo dnf install -y \
			gcc \
			gcc-c++ \
			make \
			cmake \
			ninja-build \
			python3 \
			python3-devel \
			libcurl-devel \
			libzstd-devel \
			libedit-devel \
			ncurses-devel \
			swig \
			libxml2-devel \
			zlib-devel \
			binutils-devel \
			libpfm-devel \
			lua-devel

		# Install dev packages for all installed Python3 versions
		PYTHON_DEV_PACKAGES=""
		for pyver in $(rpm -qa | grep -oP 'python3\.\d+' | sort -u); do
			PYTHON_DEV_PACKAGES="$PYTHON_DEV_PACKAGES ${pyver}-devel"
		done
		if [ -n "$PYTHON_DEV_PACKAGES" ]; then
			echo "Installing Python dev packages: $PYTHON_DEV_PACKAGES"
			sudo dnf install -y $PYTHON_DEV_PACKAGES
		fi

	elif command -v pacman &> /dev/null; then
		sudo pacman -Sy --noconfirm \
			base-devel \
			cmake \
			ninja \
			python \
			curl \
			zstd \
			libedit \
			ncurses \
			swig \
			libxml2 \
			zlib \
			libpfm \
			lua
	else
		echo "Warning: Unknown package manager. Please install dependencies manually:"
		echo "  build-essential, cmake, ninja-build, python3, python3-dev,"
		echo "  libcurl4-openssl-dev, libzstd-dev, libedit-dev, libncurses5-dev,"
		echo "  swig, libxml2-dev, zlib1g-dev, binutils-dev, libpfm4-dev, lua5.4-dev"
	fi

	echo "Dependencies installed."
}

if [[ "$MalterlibPlatform" == "Linux" ]] && [[ "$InstallDependencies" == "true" ]]; then
	InstallLinuxDependencies
fi

RootDir="$ScriptDir"
echo "RootDir: $RootDir"
echo "DistributionDir: $DistributionDir"

pushd "$ScriptDir"

NCPUS=`nproc || sysctl -n hw.ncpu`
echo Number of CPUs: ${NCPUS}

if [[ "$BuildIncremental" != "true" ]]; then
	rm -rf "$RootDir/build"
	rm -rf "$DistributionDir/"*
fi

if [[ "$MalterlibPlatform" == "macOS" ]] ; then
	export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
	"$MalterlibRoot/External/llvm-project/lldb/scripts/macos-setup-codesign.sh"
fi

ExtraCMake="-G Ninja"

LLVMProjects="clang;clang-tools-extra;lld;lldb"
LLVMRuntimes="compiler-rt;libcxx;libcxxabi;libunwind"

if [[ "$MalterlibPlatform" == "Linux" ]] && [[ "$MalterlibArch" != "x86" ]]; then
	LLVMProjects="$LLVMProjects;bolt"
fi

ExtraCMake="$ExtraCMake -DLLVM_ENABLE_CURL=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_ENABLE_CURL=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_CURL=ON"

ExtraCMake="$ExtraCMake -DLLVM_USE_STATIC_ZSTD=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_USE_STATIC_ZSTD=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLVM_USE_STATIC_ZSTD=ON"

ExtraCMake="$ExtraCMake -DLLVM_ENABLE_ZSTD=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_ENABLE_ZSTD=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_ZSTD=ON"

ExtraCMake="$ExtraCMake -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_INSTALL_TOOLCHAIN_ONLY=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLVM_INSTALL_TOOLCHAIN_ONLY=ON"

# Enable Python support for LLDB
ExtraCMake="$ExtraCMake -DLLDB_ENABLE_PYTHON=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLDB_ENABLE_PYTHON=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLDB_ENABLE_PYTHON=ON"

if [[ "$MalterlibPlatform" == "macOS" ]]; then
	# Use system Python on macOS to avoid Homebrew dependency
	ExtraCMake="$ExtraCMake -DPython3_EXECUTABLE=/usr/bin/python3"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_Python3_EXECUTABLE=/usr/bin/python3"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_Python3_EXECUTABLE=/usr/bin/python3"
fi

ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_RUNTIMES=$LLVMRuntimes"
ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_PROJECTS=$LLVMProjects"

if [[ "$MalterlibPlatform" == "Linux" ]] && [[ "$MalterlibArch" == "x86" ]]; then
	ExtraCMake="$ExtraCMake -DLLVM_ENABLE_LTO=OFF"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_ENABLE_LTO=OFF"
 	ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_BOLTOPT=OFF"

	ExtraCMake="$ExtraCMake -DSANITIZER_ALLOW_CXXABI=OFF"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_SANITIZER_ALLOW_CXXABI=OFF"

	ExtraCMake="$ExtraCMake -DLLVM_HOST_TRIPLE=i686-unknown-linux-gnu"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_HOST_TRIPLE=i686-unknown-linux-gnu"

	ExtraCMake="$ExtraCMake -DCMAKE_EXE_LINKER_FLAGS_INIT=-latomic"
	ExtraCMake="$ExtraCMake -DCMAKE_SHARED_LINKER_FLAGS_INIT=-latomic"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_EXE_LINKER_FLAGS_INIT=-latomic"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_SHARED_LINKER_FLAGS_INIT=-latomic"

	ExtraCMake="$ExtraCMake -DLLVM_TARGETS_TO_BUILD=X86"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_TARGETS_TO_BUILD=X86"

fi

BuildCompilerLTO()
{
	mkdir -p "$RootDir/build/dist_temp"
	pushd "$RootDir/build"
		local BuildDir="$PWD"
	popd

	if [[ "$BuildIncremental" != "true" ]]; then
		ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_PGO=ON"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_VP_COUNTERS_PER_SITE=3"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_CMAKE_INSTALL_PREFIX=$DistributionDir"

		if [[ "$MalterlibPlatform" != "Linux" ]]; then
			ExtraCMake="$ExtraCMake -DCLANG_PGO_TRAINING_PROFILES_DIR=$BuildDir/profiles/"
			ExtraCMake="$ExtraCMake -DBOOTSTRAP_CLANG_PGO_TRAINING_PROFILES_DIR=$BuildDir/profiles/"
			ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_CLANG_PGO_TRAINING_PROFILES_DIR=$BuildDir/profiles/"
		fi

		pushd "$BuildDir/dist_temp"
			echo Building compiler

			(cmake $ExtraCMake -C "$MalterlibRoot/External/llvm-project/clang/cmake/caches/Release.cmake" "$MalterlibRoot/External/llvm-project/llvm")

			time ninja -j${NCPUS} stage2-instrumented
		popd

		# Generate profiling data

		rm -rf "$BuildDir/profiles"
		mkdir -p "$BuildDir/profiles"
		if [[ "$MalterlibPlatform" == "macOS" ]]; then
			export LLVM_PROFILE_FILE="$BuildDir/profiles/malterlib-%${NCPUS}m%c.profraw"
		else
			export LLVM_PROFILE_FILE="$BuildDir/profiles/malterlib-%${NCPUS}m.profraw"
		fi
		pushd "$MalterlibRoot"

			#export EnableReleaseConfig=true
			export EnableReleaseTestingConfig=true
			export MalterlibDisableBuildSystemGeneration=true
			export PlatformToolsetCompiler="$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins/bin/clang"
			export MalterlibImportUpdateCache=false

			if [[ "$MalterlibPlatform" == "macOS" ]]; then
				(source "$MalterlibRoot/BuildSystem/SharedBuildSettings.sh"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Int"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Out")

				export EnablePlatform_macOS=true
				export EnablePlatform_Linux=true

				export EnableArchitecture_x86=false
				export EnableArchitecture_x64=true
				export EnableArchitecture_arm64=true

				export EnableDebugConfig=true
				export EnableReleaseConfig=false
				export EnableReleaseTestingConfig=true

				./mib generate --no-use-user-settings Tests --reconcile-removed=*:leave

				#./mib build "Tests" "macOS" "arm64" "Release (Tests)" || true # Crashes
				./mib build "Tests" "macOS" "arm64" "Debug" || true
				./mib build "Tests" "macOS" "arm64" "Release Testing (Tests)" || true
				#./mib build "Tests" "macOS" "x64" "Release (Tests)" || true # Crashes
				./mib build "Tests" "macOS" "x64" "Debug" || true
				./mib build "Tests" "macOS" "x64" "Release Testing (Tests)" || true
				#./mib build "Tests" "Linux" "x64" "Release (Tests)" || true # Crashes
				./mib build "Tests" "Linux" "x64" "Debug" || true
				./mib build "Tests" "Linux" "x64" "Release Testing (Tests)" || true
			elif [[ "$MalterlibPlatform" == "Linux" ]]; then
				export EnableArchitecture_x86=false
				export EnableArchitecture_x64=true
				export EnableArchitecture_arm64=false
				export EnablePlatform_macOS=false
				export EnablePlatform_Linux=true

				#./mib generate --no-use-user-settings Tests --reconcile-removed=*:leave
				#./mib build "Tests" "Linux" "x64" "Release (Tests)" || true # Crashes
				#./mib build "Tests" "Linux" "x64" "Debug" || true
				#./mib build "Tests" "Linux" "x64" "Release Testing (Tests)" || true
				#pushd "$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins"
				#	ninja check-clang || true
				#popd

			elif [[ "$MalterlibPlatform" == "Windows" ]]; then
				(source "$MalterlibRoot/BuildSystem/SharedBuildSettings.sh"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Int"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Out")

				export EnableArchitecture_x86=true
				export EnableArchitecture_x64=true
				export EnableArchitecture_arm64=false
				export EnablePlatform_Windows=true

				./mib generate --no-use-user-settings Tests --reconcile-removed=*:leave

				#./mib build "Tests" "Windows" "x64" "Release (Tests)" || true # Crashes
				./mib build "Tests" "Windows" "x64" "Debug" || true
				./mib build "Tests" "Windows" "x64" "Release Testing (Tests)" || true
				./mib build "Tests" "Windows" "x86" "Debug" || true
				./mib build "Tests" "Windows" "x86" "Release Testing (Tests)" || true
			else
				echo "Unknow platform"
				exit 1
			fi
		popd
	fi

	pushd "$BuildDir/dist_temp"
		time ninja -j${NCPUS} stage2-install
	popd
}

BuildCompiler()
{
	export StandaloneBuild=true
	mkdir -p "$RootDir/build/dist_temp2"
	pushd "$RootDir/build"
		local BuildDir="$PWD"
	popd
	pushd "$RootDir/build/dist_temp2"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_INSTALL_PREFIX=$DistributionDir"
		ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_PGO=OFF"
		ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_LTO=OFF"

		(cmake $ExtraCMake -C "$MalterlibRoot/External/llvm-project/clang/cmake/caches/Release.cmake" "$MalterlibRoot/External/llvm-project/llvm")

		time ninja -j${NCPUS} stage2-install
	popd
}

BuildDevCompiler()
{
	BuildType="${BuildType:-Debug}"
	export StandaloneBuild=true
	mkdir -p "$RootDir/build/dist_temp2"
	pushd "$RootDir/build"
		local BuildDir="$PWD"
	popd
	pushd "$RootDir/build/dist_temp2"
		ExtraCMake="$ExtraCMake -DCMAKE_INSTALL_PREFIX=$DistributionDir"
		ExtraCMake="$ExtraCMake -DLLVM_ENABLE_PGO=OFF"
		ExtraCMake="$ExtraCMake -DLLVM_ENABLE_LTO=OFF"
		ExtraCMake="$ExtraCMake -DLLVM_ENABLE_RUNTIMES=$LLVMRuntimes"
		ExtraCMake="$ExtraCMake -DLLVM_ENABLE_PROJECTS=$LLVMProjects"
		ExtraCMake="$ExtraCMake -DCMAKE_BUILD_TYPE=$BuildType"
		ExtraCMake="$ExtraCMake -DLLVM_ENABLE_ASSERTIONS=ON"

		(cmake $ExtraCMake "$MalterlibRoot/External/llvm-project/llvm")

		time ninja -j${NCPUS} install
	popd
}

if [[ "$1" == "dev" ]]; then
	BuildDevCompiler
elif [[ "$MalterlibPlatform/$MalterlibArch" == "Linux/x86" ]] || [[ "$1" == "debug" ]]; then
	BuildCompiler
else
	BuildCompilerLTO
fi
