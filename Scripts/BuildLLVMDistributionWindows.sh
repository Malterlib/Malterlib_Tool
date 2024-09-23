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

LLVMVersion=19.1.0

ExtraCMake="-G Ninja"
RootDir="$ScriptDir"
RootOutputDir="$ScriptDir"

if [[ "$MalterlibPlatform" == "Windows" ]]; then
	RootOutputDir="/c/CompiledFiles/llvm"
fi

NCPUS=`nproc || sysctl -n hw.ncpu`
echo Number of CPUs: ${NCPUS}

PerlPath=`find /c/Apps/strawberry-perl-*-portable/perl -maxdepth 1 -name bin`
echo "PerlPath: $PerlPath"
export PATH="$PerlPath:$PATH"

echo "RootDir: $RootDir"
echo "RootOutputDir: $RootOutputDir"
echo "DistributionDir: $DistributionDir"

pushd "$ScriptDir"

if [[ "$BuildIncremental" != "true" ]]; then
	rm -rf "$RootOutputDir/build"
	rm -rf "$DistributionDir/"*
fi

mkdir -p "$RootOutputDir/build"
pushd "$RootOutputDir/build"
	cmd.exe //C `cygpath -m "$MalterlibRoot/External/llvm-project/llvm/utils/release/build_llvm_release.bat"` --version $LLVMVersion --$MalterlibArch --skip-checkout --local-python
	SourceDistributionDir=`find "$RootOutputDir/build"/llvm_package*/build*/_CPack_Packages/*/NSIS/LLVM* -maxdepth 0 -type d`

	"$MToolExecutable" DiffCopy `cygpath -m "$SourceDistributionDir/"`"*" "$DistributionDir"
popd
