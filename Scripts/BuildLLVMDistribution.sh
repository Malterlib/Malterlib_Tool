#!/bin/bash

set -e

cd "$( dirname "${BASH_SOURCE[0]}" )"
echo Directory: $PWD
ScriptDir="$PWD"

pushd ../../..
	MalterlibRoot="$PWD"
popd

source "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh"
DistributionDir="$MalterlibRoot/Binaries/MalterlibLLVM/$MalterlibPlatform/$MalterlibArch"

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

BuildCompilerLTO()
{
	mkdir -p "$RootDir/build/dist_temp"
	pushd "$RootDir/build"
		local BuildDir="$PWD"
	popd

	if [[ "$BuildIncremental" != "true" ]]; then
		pushd "$BuildDir/dist_temp"
			echo Building compiler

			local ExtraCMake="-G Ninja"

			(cmake $ExtraCMake -C "$RootDir/cmake_caches/PGO.cmake" "$MalterlibRoot/External/llvm-project/llvm")

			time ninja -j${NCPUS} stage2-instrumented
		popd

		# Generate profiling data
		pushd "$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins"
			#ninja check-all || true
		popd

		pushd "$MalterlibRoot"
			#export EnableReleaseConfig=true
			export EnableReleaseTestingConfig=true
			export MalterlibDisableBuildSystemGeneration=true
			export PlatformToolsetCompiler="$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins/bin/clang"
			export MalterlibImportUpdateCache=false

			if [[ "$MalterlibPlatform" == "macOS" ]]; then
				export EnableArchitecture_x86=false
				export EnableArchitecture_x64=true
				export EnableArchitecture_arm64=true
				export EnablePlatform_macOS=true
				export EnablePlatform_Linux=true

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
			        pushd "$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins"
					ninja check-clang || true
			        popd

			elif [[ "$MalterlibPlatform" == "Windows" ]]; then
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

		# Merge profiling data
		"$BuildDir/dist_temp/bin/llvm-profdata" merge "-output=$RootDir/build/merged.profdata" "$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins/profiles/"*.profraw
	fi

	mkdir -p "$RootDir/build/dist_temp2"
	pushd "$RootDir/build/dist_temp2"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_INSTALL_PREFIX=$DistributionDir"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_PROFDATA_FILE=$RootDir/build/merged.profdata"

		(cmake $ExtraCMake -C "$RootDir/cmake_caches/Distribution.cmake" "$MalterlibRoot/External/llvm-project/llvm")

		time ninja -j${NCPUS} ClangDriverOptions
		time ninja -j${NCPUS} stage2-install-distribution
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
		local ExtraCMake="-G Ninja"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_INSTALL_PREFIX=$DistributionDir"

		(cmake $ExtraCMake -C "$RootDir/cmake_caches/Distribution.cmake" "$MalterlibRoot/External/llvm-project/llvm")

		time ninja -j${NCPUS} ClangDriverOptions
		time ninja -j${NCPUS} stage2-install-distribution
	popd
}

if [[ "$MalterlibPlatform/$MalterlibArch" == "Linux/x86" ]] || [[ "$1" == "debug" ]]; then
	BuildCompiler
else
	BuildCompilerLTO
fi
