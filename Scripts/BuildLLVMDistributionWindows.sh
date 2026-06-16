#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e
set -o pipefail

# This script relies on MSYS2 argument path conversion when invoking Windows
# batch files through cmd.exe. Some non-interactive shells inherit this variable
# and then pass C:/... paths to cmd.exe literally, which breaks batch dispatch.
unset MSYS_NO_PATHCONV

cd "$( dirname "${BASH_SOURCE[0]}" )"
echo Directory: $PWD
ScriptDir="$PWD"

pushd ../../..
	MalterlibRoot="$PWD"
popd

source "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh"

DetectedMalterlibPlatform="$MalterlibPlatform"
DetectedMalterlibArch="$MalterlibArch"
MalterlibPlatform="${MalterlibLLVMPlatform:-$MalterlibPlatform}"
MalterlibArch="${MalterlibLLVMArch:-$MalterlibArch}"

echo "Host: $DetectedMalterlibPlatform/$DetectedMalterlibArch"
echo "Target: $MalterlibPlatform/$MalterlibArch"

DistributionDir="$MalterlibRoot/Binaries/MalterlibLLVM/$MalterlibPlatform/$MalterlibArch"

# LLVM version is deduced from the source tree so it can't drift out of sync
# with External/llvm-project. Override by exporting LLVMVersion.
fg_DetectLLVMVersion()
{
	local VersionFile="$MalterlibRoot/External/llvm-project/cmake/Modules/LLVMVersion.cmake"
	local Major Minor Patch
	Major=`sed -n 's/.*set(LLVM_VERSION_MAJOR \([0-9][0-9]*\)).*/\1/p' "$VersionFile"`
	Minor=`sed -n 's/.*set(LLVM_VERSION_MINOR \([0-9][0-9]*\)).*/\1/p' "$VersionFile"`
	Patch=`sed -n 's/.*set(LLVM_VERSION_PATCH \([0-9][0-9]*\)).*/\1/p' "$VersionFile"`
	if [[ -z "$Major" || -z "$Minor" || -z "$Patch" ]]; then
		echo "Unable to parse LLVM version from $VersionFile" >&2
		exit 1
	fi
	echo "$Major.$Minor.$Patch"
}

LLVMVersion="${LLVMVersion:-`fg_DetectLLVMVersion`}"

# Space-optimization toggles. Each can be forced with its own env var; they
# default to on under CI (the GitHub Actions workflow exports RunningCI=true)
# and off for local builds.
if [[ "${RunningCI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
	LLVMCIDefault=true
else
	LLVMCIDefault=false
fi

LLVMDisableSymbols="${LLVMDisableSymbols:-$LLVMCIDefault}"         # strip -g debug info (largest size win)
LLVMCleanupDuringBuild="${LLVMCleanupDuringBuild:-$LLVMCIDefault}" # delete throwaway build files as we go
LLVMEnableASan="${LLVMEnableASan:-false}"                # diagnostic final-stage AddressSanitizer build
LLVMBuildStage="${LLVMBuildStage:-all}"

ExtraCMake="-G Ninja"
RootDir="${MalterlibLLVMBuildRoot:-$ScriptDir}"
RootOutputDir="$RootDir"

if [[ "$MalterlibPlatform" == "Windows" ]] && [[ -z "${MalterlibLLVMBuildRoot:-}" ]]; then
	RootOutputDir="/c/CompiledFiles/llvm"
fi

NCPUS=`nproc || sysctl -n hw.ncpu`
echo Number of CPUs: ${NCPUS}

PerlPath=`find /c/Apps/strawberry-perl-*-portable/perl -maxdepth 1 -name bin 2>/dev/null || true`
if [[ -n "$PerlPath" ]]; then
	echo "PerlPath: $PerlPath"
	export PATH="$PerlPath:$PATH"
elif ! command -v perl >/dev/null 2>&1; then
	echo "Unable to find Perl. Install Strawberry Perl or add perl to PATH."
	exit 1
fi

BuildDir="$RootOutputDir/build"
LLVMPackageDir="$BuildDir/llvm_package_$LLVMVersion"
LLVMInstallDir="$LLVMPackageDir/install"
LLVMReleaseBat="$MalterlibRoot/External/llvm-project/llvm/utils/release/build_llvm_release.bat"
LLDBPDCursesSourceDir="$MalterlibRoot/External/PDCursesMod"
LLDBPDCursesTarget="${LLDBPDCursesTarget:-wincon_pdcursesstatic}"
BundledBsdTar="$MalterlibRoot/Binaries/Malterlib/$DetectedMalterlibPlatform/$DetectedMalterlibArch/bsdtar.exe"

if [[ ! -f "$BundledBsdTar" ]]; then
	echo "Unable to find bundled bsdtar at: $BundledBsdTar" >&2
	exit 1
fi
LLVM_BSDTAR=`cygpath -w "$BundledBsdTar"`
export LLVM_BSDTAR

echo "LLVMVersion: $LLVMVersion"
echo "RootDir: $RootDir"
echo "RootOutputDir: $RootOutputDir"
echo "DistributionDir: $DistributionDir"
echo "BuildDir: $BuildDir"
echo "LLDBPDCursesSourceDir: $LLDBPDCursesSourceDir"
echo "LLDBPDCursesTarget: $LLDBPDCursesTarget"
echo "LLVM_BSDTAR: $LLVM_BSDTAR"
echo "LLVMBuildStage: $LLVMBuildStage"
echo "Optimizations -> DisableSymbols: $LLVMDisableSymbols  CleanupDuringBuild: $LLVMCleanupDuringBuild  EnableASan: $LLVMEnableASan"

pushd "$ScriptDir"

if [[ ! -f "$LLDBPDCursesSourceDir/CMakeLists.txt" ]]; then
	echo "Unable to find LLDB PDCurses source at: $LLDBPDCursesSourceDir" >&2
	exit 1
fi

case "$LLVMBuildStage" in
	all|stage0|pgo|final)
		;;
	*)
		echo "Unsupported LLVMBuildStage: $LLVMBuildStage" >&2
		exit 1
		;;
esac

fg_GetLLVMStage0Dir()
{
	case "$MalterlibArch" in
		x86)
			echo "$LLVMPackageDir/build32_stage0"
			;;
		x64)
			echo "$LLVMPackageDir/build_amd64_stage0"
			;;
		arm64)
			echo "$LLVMPackageDir/build_arm64_stage0"
			;;
		*)
			echo "Unsupported LLVM architecture: $MalterlibArch" >&2
			exit 1
			;;
	esac
}

fg_GetLLVMFinalStageDir()
{
	case "$MalterlibArch" in
		x86)
			echo "$LLVMPackageDir/build32"
			;;
		x64)
			echo "$LLVMPackageDir/build_amd64"
			;;
		arm64)
			echo "$LLVMPackageDir/build_arm64"
			;;
		*)
			echo "Unsupported LLVM architecture: $MalterlibArch" >&2
			exit 1
			;;
	esac
}

fg_LLVMStage0Ready()
{
	local Stage0Dir=`fg_GetLLVMStage0Dir`

	[[ -f "$Stage0Dir/bin/clang-cl.exe" ]] \
		&& [[ -f "$Stage0Dir/bin/lld-link.exe" ]] \
		&& [[ -f "$Stage0Dir/bin/llvm-lib.exe" ]] \
		&& [[ -f "$Stage0Dir/bin/llvm-windres.exe" ]] \
		&& [[ -f "$Stage0Dir/libxmlbuild/install/lib/libxml2s.lib" ]]
}

fg_LLVMProfileReady()
{
	local FinalStageDir=`fg_GetLLVMFinalStageDir`

	[[ -f "$FinalStageDir/profile.profdata" ]]
}

fg_GetDefaultPythonHome()
{
	case "$MalterlibArch" in
		x64)
			echo "/c/Users/$USERNAME/AppData/Local/Programs/Python/Python311"
			;;
		arm64)
			echo "/c/Users/$USERNAME/AppData/Local/Programs/Python/Python311-arm64"
			;;
		*)
			echo "/c/Users/$USERNAME/AppData/Local/Programs/Python/Python311"
			;;
	esac
}

fg_InstallPython()
{
	local InstallerArch
	local Installer
	local PythonInstallTarget

	case "$MalterlibArch" in
		x64)
			InstallerArch=amd64
			;;
		arm64)
			InstallerArch=arm64
			;;
		*)
			echo "Unsupported Python installer architecture for $MalterlibArch" >&2
			exit 1
			;;
	esac

	LLVMPythonVersion="${LLVMPythonVersion:-3.11.9}"
	Installer="$RootOutputDir/python-$LLVMPythonVersion-$InstallerArch.exe"
	PythonInstallTarget=`cygpath -w "$LLVMPythonHome"`

	echo "Installing python.org Python $LLVMPythonVersion ($InstallerArch) to $LLVMPythonHome"
	mkdir -p "$RootOutputDir"
	curl -fL "https://www.python.org/ftp/python/$LLVMPythonVersion/python-$LLVMPythonVersion-$InstallerArch.exe" -o "$Installer"

	INSTALLER=`cygpath -w "$Installer"` \
	PYTHON_INSTALL_TARGET="$PythonInstallTarget" \
	powershell -NoProfile -NonInteractive -Command '
		$ErrorActionPreference = "Stop"
		$Arguments = @(
			"/quiet"
			"InstallAllUsers=0"
			"PrependPath=0"
			"Include_launcher=0"
			"Include_test=0"
			"TargetDir=$env:PYTHON_INSTALL_TARGET"
		)
		$Process = Start-Process -FilePath $env:INSTALLER -ArgumentList $Arguments -Wait -PassThru
		if ($Process.ExitCode -ne 0) {
			throw "Python installer failed with exit code $($Process.ExitCode)"
		}
	'
}

# LLDB is built with Python scripting enabled. Verify the toolchain prerequisites
# up front so a multi-hour build can't fail late (or silently drop scripting).
# Without --local-python the release bat uses a fixed per-user Python 3.11; SWIG
# is required to generate the bindings. Override the path with LLVMPythonHome.
LLVMPythonHome="${LLVMPythonHome:-`fg_GetDefaultPythonHome`}"
if [[ ! -x "$LLVMPythonHome/python.exe" && "${GITHUB_ACTIONS:-}" == "true" ]]; then
	fg_InstallPython
fi
if [[ ! -x "$LLVMPythonHome/python.exe" ]]; then
	echo "Python 3.11 not found at: $LLVMPythonHome" >&2
	echo "Install the python.org Windows 3.11 (per-user) installer, or set LLVMPythonHome." >&2
	echo "LLDB Python scripting needs Python 3.11 with development headers/libs." >&2
	exit 1
fi
if [[ ! -f "$LLVMPythonHome/libs/python3.lib" ]]; then
	echo "Python import library missing: $LLVMPythonHome/libs/python3.lib" >&2
	echo "Use the full python.org Windows 3.11 installer, not the embeddable package." >&2
	exit 1
fi
export PATH="$LLVMPythonHome:$LLVMPythonHome/Scripts:$PATH"
if ! command -v swig >/dev/null 2>&1; then
	echo "SWIG not on PATH; installing it into the build Python via pip..."
	"$LLVMPythonHome/python.exe" -m pip install --upgrade swig || true
	# The 'swig' wheel ships a self-contained swig.exe in the Python Scripts dir.
	export PATH="$LLVMPythonHome/Scripts:$PATH"
fi
if ! command -v swig >/dev/null 2>&1; then
	echo "SWIG is required for LLDB's Python bindings but is unavailable and the" >&2
	echo "pip install failed. Install it manually (e.g. 'choco install swig') and" >&2
	echo "ensure swig is on PATH." >&2
	exit 1
fi

# Point the release bat at the same Python we just validated (it honors these).
case "$MalterlibArch" in
	x64) export python64_dir=`cygpath -w "$LLVMPythonHome"` ;;
	arm64) export pythonarm64_dir=`cygpath -w "$LLVMPythonHome"` ;;
esac

if [[ "$BuildIncremental" != "true" ]]; then
	case "$LLVMBuildStage" in
		all|stage0)
			rm -rf "$BuildDir"
			;;
	esac
fi

if [[ "$LLVMBuildStage" == "all" || "$LLVMBuildStage" == "final" ]]; then
	DIST_DIR=$(cygpath -w "$DistributionDir") \
	powershell -NoProfile -NonInteractive -Command '
		$ErrorActionPreference = "Stop"
		if (Test-Path $env:DIST_DIR) {
			$Keep = @(".git", ".gitattributes", ".gitignore")
			Get-ChildItem -LiteralPath $env:DIST_DIR -Force | Where-Object { $Keep -notcontains $_.Name } | ForEach-Object {
				if ($_.PSIsContainer -and -not $_.LinkType) {
					Get-ChildItem -LiteralPath $_.FullName -Force | Remove-Item -Recurse -Force
				} else {
					Remove-Item -LiteralPath $_.FullName -Recurse -Force
				}
			}
		}
	'
fi

mkdir -p "$BuildDir"
pushd "$BuildDir"
	# Build the argument list for LLVM's release batch script. The space
	# optimizations are implemented as flags in build_llvm_release.bat itself
	# (--no-debug, --install-prefix, --cleanup).
	BuildArgs="--version $LLVMVersion --$MalterlibArch --skip-checkout"
	if [[ "$LLVMDisableSymbols" == "true" ]]; then
		BuildArgs="$BuildArgs --no-debug"
	fi
	BuildArgs="$BuildArgs  --install-prefix `cygpath -m "$LLVMInstallDir"`"
	if [[ "$LLVMCleanupDuringBuild" == "true" ]]; then
		BuildArgs="$BuildArgs --cleanup"
	fi
	if [[ "$BuildIncremental" == "true" && ( "$LLVMBuildStage" == "all" || "$LLVMBuildStage" == "final" ) ]]; then
		if fg_LLVMStage0Ready; then
			if fg_LLVMProfileReady; then
				echo "BuildIncremental=true: stage0 and PGO profile found; rebuilding only the final LLVM stage"
				BuildArgs="$BuildArgs --final-stage-only"
			else
				echo "BuildIncremental=true: stage0 found but PGO profile is missing; rebuilding PGO and final LLVM stages"
				BuildArgs="$BuildArgs --skip-stage0"
			fi
		else
			echo "BuildIncremental=true: stage0 compiler/dependencies are missing; building stage0 before later LLVM stages"
		fi
	else
		case "$LLVMBuildStage" in
			stage0)
				BuildArgs="$BuildArgs --stage0-only"
				;;
			pgo)
				BuildArgs="$BuildArgs --pgo-only"
				;;
			final)
				BuildArgs="$BuildArgs --final-stage-only"
				;;
		esac
	fi
	if [[ "$LLVMEnableASan" == "true" ]]; then
		BuildArgs="$BuildArgs --asan"
	fi

	LLDB_PDCURSES_SOURCE_DIR=`cygpath -w "$LLDBPDCursesSourceDir"` \
	LLDB_PDCURSES_TARGET="$LLDBPDCursesTarget" \
	cmd.exe //C `cygpath -m "$LLVMReleaseBat"` $BuildArgs

	if [[ "$LLVMBuildStage" == "stage0" || "$LLVMBuildStage" == "pgo" ]]; then
		popd
		exit 0
	fi

	SourceDistributionDir="$LLVMInstallDir"

	if [[ ! -d "$SourceDistributionDir/bin" ]]; then
		echo "LLVM distribution is missing its bin directory: $SourceDistributionDir" >&2
		exit 1
	fi

	"$MToolExecutable" DiffCopy `cygpath -m "$SourceDistributionDir/"`"*" "$DistributionDir"

	if [[ "$LLVMEnableASan" == "true" ]]; then
		ASanRuntimeDlls=()
		while IFS= read -r -d '' ASanRuntimeDll; do
			ASanRuntimeDlls+=("$ASanRuntimeDll")
		done < <(find "$DistributionDir/lib/clang" -path '*/lib/windows/clang_rt.asan_dynamic-*.dll' -print0)

		if [[ ${#ASanRuntimeDlls[@]} -eq 0 ]]; then
			echo "ASan build is missing clang_rt.asan_dynamic runtime DLLs under: $DistributionDir/lib/clang" >&2
			exit 1
		fi

		for ASanRuntimeDll in "${ASanRuntimeDlls[@]}"; do
			cp -f "$ASanRuntimeDll" "$DistributionDir/bin/"
		done
	fi
popd

# Bundle a relocatable Python runtime so the distributed LLDB has working scripting
# on machines without Python installed. LLDB is built with LLDB_PYTHON_HOME=python,
# so embedded Python uses a python subdirectory relative to liblldb as PYTHONHOME.
if [[ -f "$DistributionDir/bin/liblldb.dll" ]]; then
	# The standalone Python bindings (lib/site-packages/lldb/native/_lldb*.pyd) are a
	# byte-identical copy of liblldb.dll - liblldb exports PyInit__lldb, so it can
	# serve as the _lldb module directly. Replace the ~100MB copy with a symlink to
	# bin/liblldb.dll: reclaims the space and keeps the checked-in distribution small.
	# (CodeLLDB and lldb.exe use the embedded built-in _lldb and never touch this.)
	PY_NATIVE=$(cygpath -w "$DistributionDir/lib/site-packages/lldb/native") \
	powershell -NoProfile -NonInteractive -Command '
		$ErrorActionPreference = "Stop"
		if (Test-Path $env:PY_NATIVE) {
			function New-RelativeSymlink($Path, $Target) {
				$parent = Split-Path -Parent $Path
				$leaf = Split-Path -Leaf $Path
				$mklinkTarget = $Target.Replace("/", "\")
				Push-Location $parent
				try {
					cmd.exe /d /c "mklink ""$leaf"" ""$mklinkTarget""" | Out-Null
					if ($LASTEXITCODE -ne 0) {
						throw "mklink failed: $Path -> $Target"
					}
				}
				finally {
					Pop-Location
				}
			}

			Get-ChildItem -LiteralPath $env:PY_NATIVE -Filter "_lldb*.pyd" -ErrorAction SilentlyContinue | ForEach-Object {
				Remove-Item -Force -LiteralPath $_.FullName
				New-RelativeSymlink $_.FullName "../../../../bin/liblldb.dll"
			}
		}
	'

	case "$MalterlibArch" in
		x64) PyEmbedArch=amd64 ;;
		arm64) PyEmbedArch=arm64 ;;
		*) PyEmbedArch= ;;
	esac
	if [[ -n "$PyEmbedArch" ]]; then
		PyVer=$("$LLVMPythonHome/python.exe" -c "import sys; print('.'.join(map(str, sys.version_info[:3])))")
		PyEmbedZip="$BuildDir/python-$PyVer-embed-$PyEmbedArch.zip"
		echo "Bundling relocatable Python $PyVer ($PyEmbedArch) for LLDB"
		curl -fL "https://www.python.org/ftp/python/$PyVer/python-$PyVer-embed-$PyEmbedArch.zip" -o "$PyEmbedZip"
		PY_ZIP=$(cygpath -w "$PyEmbedZip") \
		PY_BIN=$(cygpath -w "$DistributionDir/bin") \
		PY_LIB=$(cygpath -w "$DistributionDir/lib") \
		powershell -NoProfile -NonInteractive -Command '
			$ErrorActionPreference = "Stop"
			$bin = $env:PY_BIN
			$lib = $env:PY_LIB
			$runtime = Join-Path $bin "python"
			$tmp = Join-Path $bin "_python_embed_tmp"

			function New-RelativeSymlink($Path, $Target) {
				$parent = Split-Path -Parent $Path
				$leaf = Split-Path -Leaf $Path
				$mklinkTarget = $Target.Replace("/", "\")
				Push-Location $parent
				try {
					cmd.exe /d /c "mklink ""$leaf"" ""$mklinkTarget""" | Out-Null
					if ($LASTEXITCODE -ne 0) {
						throw "mklink failed: $Path -> $Target"
					}
				}
				finally {
					Pop-Location
				}
			}

			function New-RelativeDirectorySymlink($Path, $Target) {
				$parent = Split-Path -Parent $Path
				$leaf = Split-Path -Leaf $Path
				$mklinkTarget = $Target.Replace("/", "\")
				Push-Location $parent
				try {
					cmd.exe /d /c "mklink /D ""$leaf"" ""$mklinkTarget""" | Out-Null
					if ($LASTEXITCODE -ne 0) {
						throw "mklink /D failed: $Path -> $Target"
					}
				}
				finally {
					Pop-Location
				}
			}

			if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
			if (Test-Path $runtime) { Remove-Item -Recurse -Force $runtime }
			New-Item -ItemType Directory -Force -Path $tmp | Out-Null
			New-Item -ItemType Directory -Force -Path $runtime | Out-Null
			Expand-Archive -Force -LiteralPath $env:PY_ZIP -DestinationPath $tmp

			$skip = @("python.exe","pythonw.exe","python.cat","python311._pth")
			Get-ChildItem -LiteralPath $tmp -File | ForEach-Object {
				if ($skip -notcontains $_.Name) {
					Move-Item -Force $_.FullName (Join-Path $runtime $_.Name)
				}
			}
			Remove-Item -Recurse -Force $tmp

			New-RelativeSymlink (Join-Path $bin "python311.dll") "python/python311.dll"
			Set-Content (Join-Path $bin "python311._pth") -Value @("python\python311.zip","python","import site") -Encoding ASCII

			New-Item -ItemType Directory -Force -Path $lib | Out-Null
			New-RelativeSymlink (Join-Path $lib "liblldb.dll") "../bin/liblldb.dll"
			New-RelativeDirectorySymlink (Join-Path $lib "python") "../bin/python"
			New-RelativeSymlink (Join-Path $lib "python311._pth") "../bin/python311._pth"
			$rootDlls = @(
				(Get-ChildItem -LiteralPath $runtime -File -Filter "python*.dll" | ForEach-Object { $_.Name })
				(Get-ChildItem -LiteralPath $runtime -File -Filter "vcruntime*.dll" | ForEach-Object { $_.Name })
			) | Sort-Object -Unique
			$rootDlls | ForEach-Object {
				New-RelativeSymlink (Join-Path $lib $_) "../bin/python/$_"
			}
		'
	fi
fi
