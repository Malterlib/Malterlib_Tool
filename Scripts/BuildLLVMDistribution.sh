#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e
set -o pipefail

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

# Install dependencies for Linux
InstallLinuxDependencies()
{
	echo "Installing Linux build dependencies..."

	RunAsRoot()
	{
		if [[ "$(id -u)" == "0" ]]; then
			"$@"
		else
			sudo "$@"
		fi
	}

	if command -v apt-get &> /dev/null; then
		RunAsRoot apt-get update
		RunAsRoot apt-get install -y \
			build-essential \
			cmake \
			ninja-build \
			python3 \
			python3-dev \
			python3-pip \
			python3-venv \
			git \
			patchelf \
			zstd \
			libssl-dev \
			ncurses-bin \
			libzstd-dev \
			liblzma-dev \
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
			RunAsRoot apt-get install -y $PYTHON_DEV_PACKAGES
		fi

	elif command -v dnf &> /dev/null; then
		RunAsRoot dnf install -y \
			gcc \
			gcc-c++ \
			make \
			cmake \
			ninja-build \
			python3 \
			python3-devel \
			python3-pip \
			git \
			patchelf \
			openssl-devel \
			ncurses \
			libzstd-devel \
			xz-devel \
			zlib-devel \
			binutils-devel \
			libpfm-devel

		# Install dev packages for all installed Python3 versions
		PYTHON_DEV_PACKAGES=""
		for pyver in $(rpm -qa | grep -oP 'python3\.\d+' | sort -u); do
			PYTHON_DEV_PACKAGES="$PYTHON_DEV_PACKAGES ${pyver}-devel"
		done
		if [ -n "$PYTHON_DEV_PACKAGES" ]; then
			echo "Installing Python dev packages: $PYTHON_DEV_PACKAGES"
			RunAsRoot dnf install -y $PYTHON_DEV_PACKAGES
		fi

	elif command -v pacman &> /dev/null; then
		RunAsRoot pacman -S --needed --noconfirm \
			base-devel \
			cmake \
			ninja \
			python \
			python-pip \
			git \
			patchelf \
			openssl \
			ncurses \
			zstd \
			zlib \
			libpfm
	else
		echo "Warning: Unknown package manager. Please install dependencies manually:"
		echo "  build-essential, cmake, ninja-build, python3, python3-dev,"
		echo "  patchelf, libssl-dev, ncurses-bin, libzstd-dev,"
		echo "  python3-venv, liblzma-dev, zlib1g-dev, binutils-dev, libpfm4-dev"
	fi

	echo "Dependencies installed."
}

if [[ "$MalterlibPlatform" == "Linux" ]] && [[ "$InstallDependencies" == "true" ]]; then
	InstallLinuxDependencies
fi

RootDir="${MalterlibLLVMBuildRoot:-$ScriptDir}"
LLVMBuildStage="${LLVMBuildStage:-all}"
echo "RootDir: $RootDir"
echo "DistributionDir: $DistributionDir"
echo "LLVMBuildStage: $LLVMBuildStage"

pushd "$ScriptDir"

NCPUS=`nproc || sysctl -n hw.ncpu`
echo Number of CPUs: ${NCPUS}

PrintPstreeForPid()
{
	local Pid="$1"

	pstree -w "$Pid"
}

GetProcessTreePidList()
{
	local Pid="$1"

	echo "$Pid"
	GetDescendantPids "$Pid"
}

GetProcessGroupRootPids()
{
	local ProcessGroupId="$1"

	ps -A -o pid= -o ppid= -o pgid= |
		awk -v ProcessGroupId="$ProcessGroupId" '
			$3 == ProcessGroupId {
				Pid[$1] = 1
				Parent[$1] = $2
			}

			END {
				for (ProcessId in Pid) {
					ParentId = Parent[ProcessId]
					if (!(ParentId in Pid))
						print ProcessId
				}
			}
		' |
		sort -n
}

PrintLinuxProcessForestForPid()
{
	local Pid="$1"
	local PidList

	PidList="$(GetProcessTreePidList "$Pid" | paste -sd, -)"
	if [[ -z "$PidList" ]]; then
		return
	fi

	ps --forest -p "$PidList" -o pid= -o ppid= -o pgid= -o stat= -o etime= -o time= -o command=
}

PrintProcessTreeForPid()
{
	local Pid="$1"

	case "$DetectedMalterlibPlatform" in
		Linux)
			PrintLinuxProcessForestForPid "$Pid"
			;;
		macOS)
			PrintPstreeForPid "$Pid"
			;;
	esac
}

PrintProcessGroupTree()
{
	local ProcessGroupId="$1"
	local PidList
	local RootPid

	case "$DetectedMalterlibPlatform" in
		Linux)
			PidList="$(
				ps -A -o pid= -o pgid= |
					awk -v ProcessGroupId="$ProcessGroupId" '$2 == ProcessGroupId { print $1 }' |
					sort -n |
					paste -sd, -
			)"

			if [[ -n "$PidList" ]]; then
				ps --forest -p "$PidList" -o pid= -o ppid= -o pgid= -o stat= -o etime= -o time= -o command=
			fi
			;;
		macOS)
			while read -r RootPid
			do
				if [[ -n "$RootPid" ]]; then
					PrintPstreeForPid "$RootPid"
				fi
			done < <(GetProcessGroupRootPids "$ProcessGroupId")
			;;
	esac
}

PrintMatchingProcessTrees()
{
	local Pattern="$1"
	local RootPid

	while read -r RootPid
	do
		if [[ -n "$RootPid" ]]; then
			PrintProcessTreeForPid "$RootPid"
		fi
	done < <(
		ps -A -o pid= -o ppid= -o comm= |
			awk -v Pattern="$Pattern" '
				{
					Command = $3
					sub(/^.*\//, "", Command)
				}

				Command ~ Pattern {
					Pid[$1] = 1
					Parent[$1] = $2
				}

				END {
					for (ProcessId in Pid) {
						ParentId = Parent[ProcessId]
						if (!(ParentId in Pid))
							print ProcessId
					}
				}
			' |
			sort -n
	)
}

PrintLLVMTimeoutDiagnostics()
{
	local RootPid="$1"
	local CommandLine="$2"
	local ProcessGroupId

	echo "::group::LLVM ninja timeout diagnostics"
	echo "Timed out command: $CommandLine"
	echo "Timed out process id: $RootPid"
	date
	uptime || true
	pwd
	df -h "$RootDir" "$DistributionDir" 2>/dev/null || df -h || true
	free -h 2>/dev/null || true
	swapon --show 2>/dev/null || true
	echo

	echo "Process tree rooted at $RootPid:"
	PrintProcessTreeForPid "$RootPid"
	echo

	ProcessGroupId="$(ps -o pgid= -p "$RootPid" 2>/dev/null | tr -d '[:space:]' || true)"
	if [[ -n "$ProcessGroupId" ]]; then
		echo "Process group tree $ProcessGroupId:"
		PrintProcessGroupTree "$ProcessGroupId"
		echo
	fi

	echo "Relevant build process trees:"
	PrintMatchingProcessTrees \
		'^(ninja|cmake|clang|clang[+][+]|clang-cl|ld[.]lld|lld|python[0-9.]*|llvm.*)$'
	echo "::endgroup::"
}

GetDescendantPids()
{
	local RootPid="$1"
	local ChildPid

	while read -r ChildPid
	do
		if [[ -n "$ChildPid" ]]; then
			echo "$ChildPid"
			GetDescendantPids "$ChildPid"
		fi
	done < <(pgrep -P "$RootPid" 2>/dev/null || true)
}

SendSignalToProcessTree()
{
	local RootPid="$1"
	local Signal="$2"
	local ProcessGroupId="$3"
	local ChildPid
	local Pids

	if [[ -n "$ProcessGroupId" ]]; then
		kill "-$Signal" "-$ProcessGroupId" 2>/dev/null || true
		return
	fi

	Pids=("$RootPid")
	while read -r ChildPid
	do
		if [[ -n "$ChildPid" ]]; then
			Pids+=("$ChildPid")
		fi
	done < <(GetDescendantPids "$RootPid")

	kill "-$Signal" "${Pids[@]}" 2>/dev/null || true
}

WaitForProcessExitOrTimeout()
{
	local Pid="$1"
	local WaitSeconds="$2"
	local EndSeconds
	local NowSeconds
	local SleepSeconds

	EndSeconds="$(( $(date +%s) + WaitSeconds ))"
	while kill -0 "$Pid" 2>/dev/null
	do
		NowSeconds="$(date +%s)"
		if (( NowSeconds >= EndSeconds )); then
			return 1
		fi

		SleepSeconds="$(( EndSeconds - NowSeconds ))"
		if (( SleepSeconds > 5 )); then
			SleepSeconds=5
		fi
		sleep "$SleepSeconds"
	done

	return 0
}

RunCommandWithTimeout()
{
	local TimeoutSeconds="$1"
	local KillDelaySeconds="$2"
	shift 2

	local CommandPid
	local ProcessGroupId=""
	local EndSeconds
	local NowSeconds
	local SleepSeconds
	local CommandLine="$*"

	if command -v setsid >/dev/null 2>&1; then
		setsid "$@" &
		CommandPid="$!"
		ProcessGroupId="$CommandPid"
	else
		"$@" &
		CommandPid="$!"
	fi

	EndSeconds="$(( $(date +%s) + TimeoutSeconds ))"
	while kill -0 "$CommandPid" 2>/dev/null
	do
		NowSeconds="$(date +%s)"
		if (( NowSeconds >= EndSeconds )); then
			PrintLLVMTimeoutDiagnostics "$CommandPid" "$CommandLine"
			SendSignalToProcessTree "$CommandPid" TERM "$ProcessGroupId"
			if ! WaitForProcessExitOrTimeout "$CommandPid" "$KillDelaySeconds"; then
				echo "LLVM ninja command ignored SIGTERM after ${KillDelaySeconds}s; sending SIGKILL"
				SendSignalToProcessTree "$CommandPid" KILL "$ProcessGroupId"
			fi
			wait "$CommandPid" >/dev/null 2>&1 || true
			return 124
		fi

		SleepSeconds="$(( EndSeconds - NowSeconds ))"
		if (( SleepSeconds > 10 )); then
			SleepSeconds=10
		fi
		sleep "$SleepSeconds"
	done

	wait "$CommandPid"
}

RunNinja()
{
	local TimeoutSeconds="${LLVMNinjaTimeoutSeconds:-}"

	if [[ -n "$TimeoutSeconds" ]] && ! [[ "$TimeoutSeconds" =~ ^[1-9][0-9]*$ ]]; then
		echo "LLVMNinjaTimeoutSeconds must be a positive integer, got: $TimeoutSeconds"
		exit 1
	fi

	if [[ -n "${LLVMNinjaTimeoutDeadlineSeconds:-}" ]]; then
		if ! [[ "$LLVMNinjaTimeoutDeadlineSeconds" =~ ^[1-9][0-9]*$ ]]; then
			echo "LLVMNinjaTimeoutDeadlineSeconds must be a positive integer, got: $LLVMNinjaTimeoutDeadlineSeconds"
			exit 1
		fi

		local NowSeconds
		local RemainingSeconds
		NowSeconds="$(date +%s)"
		RemainingSeconds="$((LLVMNinjaTimeoutDeadlineSeconds - NowSeconds))"
		if (( RemainingSeconds <= 0 )); then
			echo "LLVM ninja deadline has already passed: deadline=$LLVMNinjaTimeoutDeadlineSeconds now=$NowSeconds"
			exit 124
		fi

		if [[ -z "$TimeoutSeconds" ]] || (( RemainingSeconds < TimeoutSeconds )); then
			TimeoutSeconds="$RemainingSeconds"
		fi
	fi

	if [[ -z "$TimeoutSeconds" ]]; then
		time ninja -j${NCPUS} "$@"
		return
	fi

	set +e
	time RunCommandWithTimeout "$TimeoutSeconds" 420 ninja -j${NCPUS} "$@"
	local Result="$?"
	set -e

	if [[ "$Result" -eq 124 || "$Result" -eq 137 ]]; then
		echo "LLVM ninja command timed out after ${TimeoutSeconds}s: ninja -j${NCPUS} $*"
		exit 124
	fi

	return "$Result"
}

CleanDistributionDir()
{
	if [[ -z "$DistributionDir" || "$DistributionDir" == "/" ]]; then
		echo "Refusing to clean invalid DistributionDir: $DistributionDir"
		exit 1
	fi

	mkdir -p "$DistributionDir"
	rm -rf "$DistributionDir/"*
}

FindLLVMDistributionCMakeCacheValue()
{
	local Name="$1"
	local CacheFile
	local Value

	for CacheFile in \
		"$RootDir/build/dist_temp2/tools/clang/stage2-bins/CMakeCache.txt" \
		"$RootDir/build/dist_temp/tools/clang/stage2-bins/CMakeCache.txt" \
		"$RootDir/build/dist_temp2/CMakeCache.txt" \
		"$RootDir/build/dist_temp/CMakeCache.txt"
	do
		if [[ ! -f "$CacheFile" ]]; then
			continue
		fi

		Value="$(
			awk -F= -v Name="$Name" '
				index($1, Name ":") == 1 { print $2; exit }
			' "$CacheFile"
		)"

		if [[ -n "$Value" ]]; then
			echo "$Value"
			return
		fi
	done
}

IsLLVMDistributionCMakeCacheValueEnabled()
{
	local Value
	Value="$(FindLLVMDistributionCMakeCacheValue "$1" | tr '[:lower:]' '[:upper:]')"

	case "$Value" in
		1|ON|TRUE|YES)
			return 0
			;;
	esac

	return 1
}

FindLinuxLLDBPythonRelativePath()
{
	local CacheFile
	local PythonRelativePath

	for CacheFile in \
		"$RootDir/build/dist_temp2/tools/clang/stage2-bins/CMakeCache.txt" \
		"$RootDir/build/dist_temp/tools/clang/stage2-bins/CMakeCache.txt" \
		"$RootDir/build/dist_temp2/CMakeCache.txt" \
		"$RootDir/build/dist_temp/CMakeCache.txt"
	do
		if [[ ! -f "$CacheFile" ]]; then
			continue
		fi

		PythonRelativePath="$(
			awk -F= '
				/^LLDB_PYTHON_RELATIVE_PATH:[^=]*=/ { print $2; exit }
			' "$CacheFile"
		)"

		if [[ -n "$PythonRelativePath" ]]; then
			echo "$PythonRelativePath"
			return
		fi
	done

	echo "lib/lldb/python"
}

InstallLinuxRuntimeLibraryLinks()
{
	InstallLinuxRuntimeLibraryLinksInDir "$DistributionDir"
}

InstallLinuxRuntimeLibraryLinksInDir()
{
	if [[ "$MalterlibPlatform" != "Linux" ]]; then
		return
	fi

	local RuntimeRootDir="$1"
	local RuntimeLibraryDir
	local RuntimeLibraryPath
	local RuntimeLibraryName
	local RuntimeLibraryRelativeDir
	local nLinkedLibraries=0

	for RuntimeLibraryDir in "$RuntimeRootDir/lib/"*-unknown-linux-gnu
	do
		if [[ ! -d "$RuntimeLibraryDir" ]]; then
			continue
		fi

		RuntimeLibraryRelativeDir="$(basename "$RuntimeLibraryDir")"
		for RuntimeLibraryPath in "$RuntimeLibraryDir"/libc++.so* "$RuntimeLibraryDir"/libc++abi.so* "$RuntimeLibraryDir"/libunwind.so*
		do
			if [[ ! -e "$RuntimeLibraryPath" ]]; then
				continue
			fi

			RuntimeLibraryName="$(basename "$RuntimeLibraryPath")"
			ln -sf "$RuntimeLibraryRelativeDir/$RuntimeLibraryName" "$RuntimeRootDir/lib/$RuntimeLibraryName"
			((nLinkedLibraries += 1))
		done
	done

	echo "Installed Linux runtime library links in $RuntimeRootDir: $nLinkedLibraries"
}

GetLinuxLLVMTargetTriple()
{
	case "$MalterlibArch" in
		x64)
			echo "x86_64-unknown-linux-gnu"
			;;
		x86)
			echo "i686-unknown-linux-gnu"
			;;
		arm64)
			echo "aarch64-unknown-linux-gnu"
			;;
		*)
			echo "Unsupported Linux LLVM architecture: $MalterlibArch" >&2
			exit 1
			;;
	esac
}

ConfigureLinuxBootstrapBuildRuntimePath()
{
	if [[ "$MalterlibPlatform" != "Linux" ]]; then
		return
	fi

	local BootstrapBuildRoot="$1"
	local TargetTriple
	TargetTriple="$(GetLinuxLLVMTargetTriple)"
	local BuildRuntimePath="\$ORIGIN/../lib;\$ORIGIN/../lib/$TargetTriple;$BootstrapBuildRoot/lib;$BootstrapBuildRoot/lib/$TargetTriple"

	ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_BUILD_RPATH=$BuildRuntimePath"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_CMAKE_BUILD_RPATH=$BuildRuntimePath"
}

if [[ "$BuildIncremental" != "true" ]]; then
	case "$LLVMBuildStage" in
		all|stage1)
			rm -rf "$RootDir/build"
			;;
	esac

	case "$LLVMBuildStage" in
		all|final)
			CleanDistributionDir
			;;
	esac
fi

if [[ "$MalterlibPlatform" == "macOS" ]] ; then
	export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
	"$MalterlibRoot/External/llvm-project/lldb/scripts/macos-setup-codesign.sh"
fi

ExtraCMake="-G Ninja"

LLVMProjects="clang;clang-tools-extra;lld;lldb"
LLVMRuntimes="compiler-rt;libcxx;libcxxabi;libunwind"

AddCMakeCacheValue()
{
	local Name="$1"
	local Value="$2"

	ExtraCMake="$ExtraCMake -D${Name}=${Value}"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_${Name}=${Value}"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_${Name}=${Value}"
}

AddCMakeCacheUnset()
{
	local Name="$1"

	ExtraCMake="$ExtraCMake -U ${Name}"
	ExtraCMake="$ExtraCMake -U BOOTSTRAP_${Name}"
	ExtraCMake="$ExtraCMake -U BOOTSTRAP_BOOTSTRAP_${Name}"
}

AddBootstrapCMakeCacheValue()
{
	local Name="$1"
	local Value="$2"

	ExtraCMake="$ExtraCMake -DBOOTSTRAP_${Name}=${Value}"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_${Name}=${Value}"
}

if [[ "$MalterlibPlatform" != "Linux" ]]; then
	AddCMakeCacheValue "LLVM_ENABLE_LIBPFM" "OFF"
fi

if [[ -n "${LLVM_PARALLEL_LINK_JOBS:-}" ]]; then
	if ! [[ "$LLVM_PARALLEL_LINK_JOBS" =~ ^[1-9][0-9]*$ ]]; then
		echo "LLVM_PARALLEL_LINK_JOBS must be a positive integer, got: $LLVM_PARALLEL_LINK_JOBS" >&2
		exit 1
	fi
	export LLVM_PARALLEL_LINK_JOBS
	echo "LLVM_PARALLEL_LINK_JOBS: $LLVM_PARALLEL_LINK_JOBS"
	AddCMakeCacheValue "LLVM_PARALLEL_LINK_JOBS" "$LLVM_PARALLEL_LINK_JOBS"
else
	echo "LLVM_PARALLEL_LINK_JOBS: unset"
fi

if [[ "$MalterlibPlatform" == "macOS" ]]; then
	MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
	export MACOSX_DEPLOYMENT_TARGET
	echo "MACOSX_DEPLOYMENT_TARGET: $MACOSX_DEPLOYMENT_TARGET"
	AddCMakeCacheValue "CMAKE_OSX_DEPLOYMENT_TARGET" "$MACOSX_DEPLOYMENT_TARGET"
fi

ConfigureMacOSCommandLineToolsPython()
{
	local PythonFrameworkVersions="/Library/Developer/CommandLineTools/Library/Frameworks/Python3.framework/Versions"
	local PythonExecutable="/Library/Developer/CommandLineTools/usr/bin/python3"
	local PythonVersion
	local PythonRoot

	PythonVersion="$(
		find "$PythonFrameworkVersions" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; 2>/dev/null \
			| grep -E '^[0-9]+(\.[0-9]+)*$' \
			| sort -t . -k1,1n -k2,2n -k3,3n \
			| tail -1 \
		|| true
	)"

	if [[ -z "$PythonVersion" ]]; then
		echo "Unable to find Command Line Tools Python3.framework."
		echo "Install Apple's Command Line Tools so LLDB can link Python without depending on /Applications/Xcode.app."
		exit 1
	fi

	PythonRoot="$PythonFrameworkVersions/$PythonVersion"
	if [[ ! -x "$PythonExecutable" ]] || [[ ! -f "$PythonRoot/lib/libpython${PythonVersion}.dylib" ]] || [[ ! -d "$PythonRoot/include/python${PythonVersion}" ]]; then
		echo "Incomplete Command Line Tools Python installation at $PythonRoot."
		echo "Expected $PythonExecutable, lib/libpython${PythonVersion}.dylib, and include/python${PythonVersion}."
		exit 1
	fi

	echo "Using Command Line Tools Python: $PythonRoot"
	AddCMakeCacheValue "Python3_EXECUTABLE" "$PythonExecutable"
	AddCMakeCacheValue "Python3_ROOT_DIR" "$PythonRoot"
}

ConfigureDynamicPythonRuntime()
{
	local MinimumVersion="$1"
	local LimitedApiVersion="$2"

	AddCMakeCacheValue "LLDB_ENABLE_PYTHON_LIMITED_API" "ON"
	AddCMakeCacheValue "LLDB_DYNAMIC_PYTHON_RUNTIME" "ON"
	AddCMakeCacheValue "LLDB_PYTHON_MINIMUM_VERSION" "$MinimumVersion"
	AddCMakeCacheValue "LLDB_PYTHON_LIMITED_API_VERSION" "$LimitedApiVersion"
	AddCMakeCacheValue "LLDB_PYTHON_RELATIVE_PATH" "lib/lldb/python"
	AddCMakeCacheValue "LLDB_EMBED_PYTHON_HOME" "OFF"
	AddCMakeCacheValue "Python3_FIND_IMPLEMENTATIONS" "CPython"
	AddCMakeCacheValue "Python3_FIND_STRATEGY" "VERSION"
	AddCMakeCacheValue "Python3_FIND_UNVERSIONED_NAMES" "LAST"
}

ConfigureVendoredZstd()
{
	local ZstdSourceDir="$MalterlibRoot/External/zstd"

	if [[ ! -f "$ZstdSourceDir/build/cmake/CMakeLists.txt" ]]; then
		echo "Unable to find vendored zstd CMake project at: $ZstdSourceDir" >&2
		exit 1
	fi

	echo "Using vendored zstd source: $ZstdSourceDir"
	AddCMakeCacheValue "LLVM_USE_STATIC_ZSTD" "ON"
	AddCMakeCacheValue "LLVM_ZSTD_SOURCE_DIR" "$ZstdSourceDir"
}

ConfigureVendoredZlib()
{
	local ZlibSourceDir="$MalterlibRoot/External/zlib"

	if [[ ! -f "$ZlibSourceDir/zlib.h" ]] || [[ ! -f "$ZlibSourceDir/adler32.c" ]]; then
		echo "Unable to find vendored zlib source at: $ZlibSourceDir" >&2
		exit 1
	fi

	echo "Using static zlib source: $ZlibSourceDir"
	AddCMakeCacheValue "LLVM_ENABLE_ZLIB" "FORCE_ON"
	AddCMakeCacheValue "LLVM_ZLIB_SOURCE_DIR" "$ZlibSourceDir"
	AddCMakeCacheValue "ZLIB_USE_STATIC_LIBS" "ON"
	AddCMakeCacheUnset "ZLIB_INCLUDE_DIR"
	AddCMakeCacheUnset "ZLIB_LIBRARY"
}

ConfigureVendoredLibEdit()
{
	local LibEditSourceDir="$MalterlibRoot/External/libedit"

	if [[ ! -f "$LibEditSourceDir/src/histedit.h" ]] || [[ ! -f "$LibEditSourceDir/src/vis.c" ]]; then
		echo "Unable to find vendored libedit source at: $LibEditSourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi

	echo "Using static libedit source: $LibEditSourceDir"
	AddCMakeCacheValue "LLVM_ENABLE_LIBEDIT" "FORCE_ON"
	AddCMakeCacheValue "LLDB_ENABLE_LIBEDIT" "ON"
	AddCMakeCacheValue "LLVM_LIBEDIT_SOURCE_DIR" "$LibEditSourceDir"
	AddCMakeCacheUnset "LibEdit_INCLUDE_DIRS"
	AddCMakeCacheUnset "LibEdit_LIBRARIES"
	AddCMakeCacheUnset "LibEdit_TERMCAP_LIBRARIES"
}

ConfigureVendoredLua()
{
	local LuaSourceDir="$MalterlibRoot/External/lua"
	local LuaMajor
	local LuaMinor

	if [[ ! -f "$LuaSourceDir/lua.h" ]] || [[ ! -f "$LuaSourceDir/lauxlib.c" ]]; then
		echo "Unable to find vendored Lua source at: $LuaSourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi

	LuaMajor="$(awk '/#define LUA_VERSION_MAJOR_N/ {print $3; exit}' "$LuaSourceDir/lua.h")"
	LuaMinor="$(awk '/#define LUA_VERSION_MINOR_N/ {print $3; exit}' "$LuaSourceDir/lua.h")"

	if [[ -z "$LuaMajor" ]] || [[ -z "$LuaMinor" ]]; then
		LuaMajor="$(awk '/#define LUA_VERSION_MAJOR[[:space:]]/ {gsub(/"/, "", $3); print $3; exit}' "$LuaSourceDir/lua.h")"
		LuaMinor="$(awk '/#define LUA_VERSION_MINOR[[:space:]]/ {gsub(/"/, "", $3); print $3; exit}' "$LuaSourceDir/lua.h")"
	fi

	if [[ -z "$LuaMajor" ]] || [[ -z "$LuaMinor" ]]; then
		echo "Unable to determine Lua version from $LuaSourceDir/lua.h" >&2
		exit 1
	fi

	echo "Using static Lua source: $LuaSourceDir ($LuaMajor.$LuaMinor)"
	AddCMakeCacheValue "LLDB_ENABLE_LUA" "ON"
	AddCMakeCacheValue "LLVM_LUA_SOURCE_DIR" "$LuaSourceDir"
	AddCMakeCacheValue "LUA_VERSION_MAJOR" "$LuaMajor"
	AddCMakeCacheValue "LUA_VERSION_MINOR" "$LuaMinor"
	AddCMakeCacheValue "LLDB_LUA_RELATIVE_PATH" "lib/lua/$LuaMajor.$LuaMinor"
	AddCMakeCacheUnset "LUA_EXECUTABLE"
	AddCMakeCacheUnset "LUA_INCLUDE_DIR"
	AddCMakeCacheUnset "LUA_LIBRARIES"
}

FindLinuxStaticLibrary()
{
	local Name="$1"

	if FindLinuxStaticLibraryOptional "$Name"; then
		return 0
	fi

	echo "Unable to find static library lib$Name.a" >&2
	exit 1
}

FindLinuxStaticLibraryOptional()
{
	local Name="$1"
	local Multiarch=""
	local CandidateDirs=()
	local Dir

	if command -v gcc &> /dev/null; then
		Multiarch="$(gcc -print-multiarch 2> /dev/null || true)"
	fi

	if [[ -n "$Multiarch" ]]; then
		CandidateDirs+=("/usr/lib/$Multiarch" "/lib/$Multiarch")
	fi
	CandidateDirs+=("/usr/lib64" "/usr/lib" "/lib64" "/lib")

	for Dir in "${CandidateDirs[@]}"; do
		if [[ -f "$Dir/lib$Name.a" ]]; then
			printf "%s/lib%s.a" "$Dir" "$Name"
			return 0
		fi
	done

	return 1
}

FindLinuxIncludeDirectory()
{
	local Header="$1"
	shift

	local Dir
	for Dir in "$@"; do
		if [[ -f "$Dir/$Header" ]]; then
			printf "%s" "$Dir"
			return 0
		fi
	done

	echo "Unable to find include directory containing $Header" >&2
	exit 1
}

ConfigureLinuxStaticSystemLibraries()
{
	local IncludeRoot="/usr/include"
	local LibLzma
	local LibPfm

	LibLzma="$(FindLinuxStaticLibrary lzma)"
	LibPfm="$(FindLinuxStaticLibrary pfm)"

	echo "Using static liblzma: $LibLzma"
	AddCMakeCacheValue "LLDB_ENABLE_LZMA" "ON"
	AddCMakeCacheValue "LIBLZMA_INCLUDE_DIR" "$IncludeRoot"
	AddCMakeCacheValue "LIBLZMA_LIBRARY" "$LibLzma"

	echo "Using static libpfm: $LibPfm"
	AddCMakeCacheValue "LLVM_ENABLE_LIBPFM" "ON"
	AddCMakeCacheValue "LIBPFM_LIBRARY" "$LibPfm"
}

ConfigureVendoredLibXml2()
{
	local LibXml2SourceDir="$MalterlibRoot/External/libxml2"

	if [[ ! -f "$LibXml2SourceDir/CMakeLists.txt" ]]; then
		echo "Unable to find libxml2 source at: $LibXml2SourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi

	echo "Using static libxml2 source: $LibXml2SourceDir"
	AddCMakeCacheValue "LLVM_ENABLE_LIBXML2" "FORCE_ON"
	AddCMakeCacheValue "CLANG_ENABLE_LIBXML2" "ON"
	AddCMakeCacheValue "LLDB_ENABLE_LIBXML2" "ON"
	AddCMakeCacheValue "LLVM_LIBXML2_SOURCE_DIR" "$LibXml2SourceDir"
	AddCMakeCacheUnset "LIBXML2_INCLUDE_DIR"
	AddCMakeCacheUnset "LIBXML2_LIBRARY"
}

ConfigureVendoredCurl()
{
	local CurlSourceDir="$MalterlibRoot/External/curl"
	local BoringSSLSourceDir="$MalterlibRoot/External/boringssl"

	if [[ ! -f "$CurlSourceDir/CMakeLists.txt" ]]; then
		echo "Unable to find curl source at: $CurlSourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi
	if [[ ! -f "$BoringSSLSourceDir/CMakeLists.txt" ]]; then
		echo "Unable to find BoringSSL source at: $BoringSSLSourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi

	echo "Using static curl source: $CurlSourceDir"
	echo "Using static BoringSSL source: $BoringSSLSourceDir"
	AddCMakeCacheValue "LLVM_ENABLE_CURL" "FORCE_ON"
	AddCMakeCacheValue "LLVM_CURL_SOURCE_DIR" "$CurlSourceDir"
	AddCMakeCacheValue "LLVM_BORINGSSL_SOURCE_DIR" "$BoringSSLSourceDir"
	AddCMakeCacheUnset "CURL_INCLUDE_DIR"
	AddCMakeCacheUnset "CURL_LIBRARY"
	AddCMakeCacheUnset "CURL_LIBRARIES"
	AddCMakeCacheUnset "OPENSSL_CRYPTO_LIBRARY"
	AddCMakeCacheUnset "OPENSSL_INCLUDE_DIR"
	AddCMakeCacheUnset "OPENSSL_LIBRARIES"
	AddCMakeCacheUnset "OPENSSL_ROOT_DIR"
	AddCMakeCacheUnset "OPENSSL_SSL_LIBRARY"
	AddCMakeCacheUnset "OpenSSL_DIR"
}

ConfigureLinuxNcurses()
{
	local NcursesSourceDir="$MalterlibRoot/External/ncurses"

	if [[ ! -x "$NcursesSourceDir/configure" ]]; then
		echo "Unable to find ncurses source at: $NcursesSourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi

	echo "Using static ncurses source: $NcursesSourceDir"
	AddCMakeCacheValue "LLDB_ENABLE_CURSES" "ON"
	AddCMakeCacheUnset "LLVM_PDCURSES_SOURCE_DIR"
	AddCMakeCacheUnset "LLVM_PDCURSES_TARGET"
	AddCMakeCacheUnset "LLDB_PDCURSES_SOURCE_DIR"
	AddCMakeCacheUnset "LLDB_PDCURSES_TARGET"
	AddCMakeCacheUnset "LLVM_NCURSES_TERMCAP_SOURCE_DIR"
	AddCMakeCacheValue "LLVM_NCURSES_SOURCE_DIR" "$NcursesSourceDir"
	AddCMakeCacheUnset "LLVM_NCURSES_INCLUDE_DIR"
	AddCMakeCacheUnset "LLVM_NCURSES_LIBRARY"
	AddCMakeCacheUnset "LLVM_PANEL_LIBRARY"
	AddCMakeCacheUnset "LLVM_TINFO_LIBRARY"
	AddCMakeCacheUnset "CURSES_INCLUDE_DIRS"
	AddCMakeCacheUnset "CURSES_LIBRARIES"
	AddCMakeCacheUnset "PANEL_LIBRARIES"
	AddCMakeCacheUnset "TINFO_LIBRARIES"
	AddCMakeCacheUnset "HAS_TERMINFO_SYMBOLS"
	AddCMakeCacheUnset "CURSES_HAS_TINFO"
}

ConfigureVendoredPDCurses()
{
	local PDCursesSourceDir="$MalterlibRoot/External/PDCursesMod"

	if [[ ! -f "$PDCursesSourceDir/CMakeLists.txt" ]]; then
		echo "Unable to find PDCursesMod source at: $PDCursesSourceDir" >&2
		echo "Enable MalterlibEnableLLVMBuildRepositories and run ./mib update-repos." >&2
		exit 1
	fi

	echo "Using PDCursesMod source: $PDCursesSourceDir"
	AddCMakeCacheValue "LLDB_ENABLE_CURSES" "ON"
	AddCMakeCacheValue "LLVM_PDCURSES_SOURCE_DIR" "$PDCursesSourceDir"
	AddCMakeCacheValue "LLVM_PDCURSES_TARGET" "vt_pdcursesstatic"
	AddCMakeCacheValue "LLDB_PDCURSES_SOURCE_DIR" "$PDCursesSourceDir"
	AddCMakeCacheValue "LLDB_PDCURSES_TARGET" "vt_pdcursesstatic"
	AddCMakeCacheUnset "LLVM_NCURSES_TERMCAP_SOURCE_DIR"
	AddCMakeCacheUnset "LLVM_NCURSES_SOURCE_DIR"
	AddCMakeCacheUnset "LLVM_NCURSES_INCLUDE_DIR"
	AddCMakeCacheUnset "LLVM_NCURSES_LIBRARY"
	AddCMakeCacheUnset "LLVM_PANEL_LIBRARY"
	AddCMakeCacheUnset "LLVM_TINFO_LIBRARY"
	AddCMakeCacheUnset "CURSES_INCLUDE_DIRS"
	AddCMakeCacheUnset "CURSES_LIBRARIES"
	AddCMakeCacheUnset "PANEL_LIBRARIES"
	AddCMakeCacheUnset "TINFO_LIBRARIES"
	AddCMakeCacheUnset "HAS_TERMINFO_SYMBOLS"
	AddCMakeCacheUnset "CURSES_HAS_TINFO"
	AddCMakeCacheValue "PDC_BUILD_SHARED" "OFF"
	AddCMakeCacheValue "PDC_UTF8" "ON"
	AddCMakeCacheValue "PDC_NCURSES_BUILD" "OFF"
	AddCMakeCacheValue "PDC_SDL2_BUILD" "OFF"
	AddCMakeCacheValue "PDC_SDL2_DEPS_BUILD" "OFF"
	AddCMakeCacheValue "PDC_GL_BUILD" "OFF"
	AddCMakeCacheValue "PDC_VT_BUILD" "ON"
	AddCMakeCacheValue "PDC_WINCON_BUILD" "OFF"
	AddCMakeCacheValue "PDC_WINGUI_BUILD" "OFF"
}

ConfigureLinuxCurses()
{
	local LinuxCursesBackend="${LLVM_LINUX_CURSES_BACKEND:-pdcurses}"

	case "$LinuxCursesBackend" in
		pdcurses|PDCurses|PDCURSES)
			ConfigureVendoredPDCurses
			;;
		ncurses|NCurses|NCURSES)
			ConfigureLinuxNcurses
			;;
		*)
			echo "Unsupported LLVM_LINUX_CURSES_BACKEND: $LinuxCursesBackend" >&2
			echo "Expected one of: pdcurses, ncurses" >&2
			exit 1
			;;
	esac
}

ConfigureLinuxSwig()
{
	local SwigVersion="${LLVM_SWIG_VERSION:-4.2.1}"
	local SwigVenvDir="${LLVM_SWIG_VENV_DIR:-$RootDir/build/swig-$SwigVersion-venv}"
	local SwigExecutable="${LLVM_SWIG_EXECUTABLE:-$SwigVenvDir/bin/swig}"
	local SwigLibraryDir
	local SwigReportedVersion

	if [[ -n "${LLVM_SWIG_EXECUTABLE:-}" ]]; then
		if [[ ! -x "$SwigExecutable" ]]; then
			echo "LLVM_SWIG_EXECUTABLE is not executable: $SwigExecutable" >&2
			exit 1
		fi
	else
		if [[ ! -x "$SwigExecutable" ]] || ! "$SwigExecutable" -version | grep -q "SWIG Version $SwigVersion"; then
			rm -rf "$SwigVenvDir"
			python3 -m venv "$SwigVenvDir"
			"$SwigVenvDir/bin/python" -m pip install --disable-pip-version-check "swig==$SwigVersion"
		fi
	fi

	SwigLibraryDir="$("$SwigExecutable" -swiglib)"
	SwigReportedVersion="$("$SwigExecutable" -version | awk '/SWIG Version/ {print $3; exit}')"

	if [[ -z "$SwigLibraryDir" ]] || [[ -z "$SwigReportedVersion" ]]; then
		echo "Unable to determine SWIG version or library directory from: $SwigExecutable" >&2
		exit 1
	fi

	echo "Using SWIG: $SwigExecutable"
	AddCMakeCacheValue "SWIG_EXECUTABLE" "$SwigExecutable"
	AddCMakeCacheValue "SWIG_DIR" "$SwigLibraryDir"
	AddCMakeCacheValue "SWIG_VERSION" "$SwigReportedVersion"
}

if [[ "$MalterlibPlatform" == "Linux" ]] && [[ "$MalterlibArch" != "x86" ]]; then
	LLVMProjects="$LLVMProjects;bolt"
fi

if [[ "$MalterlibPlatform" == "Linux" ]]; then
	ConfigureLinuxSwig
	ConfigureDynamicPythonRuntime "3.10" "0x030a0000"
	ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_LINK_LOCAL_RUNTIMES=ON"
	ExtraCMake="$ExtraCMake -DLLVM_RELEASE_STATIC_LINK_CXX_STDLIB=OFF"
	AddCMakeCacheUnset "LLVM_STATIC_LINK_CXX_STDLIB"
	AddBootstrapCMakeCacheValue "LLVM_ENABLE_LIBCXX" "ON"
	AddBootstrapCMakeCacheValue "LLVM_STATIC_LINK_CXX_STDLIB" "OFF"
	# Release.cmake uses compiler-rt alone for the PGO instrumented stage, but the static sanitizer settings below require in-tree libc++ and libunwind targets.
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_ENABLE_RUNTIMES=$LLVMRuntimes"

	LinuxRuntimePassthroughVariables="LIBCXX_ENABLE_STATIC_ABI_LIBRARY"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXX_ENABLE_ABI_LINKER_SCRIPT"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXX_USE_COMPILER_RT"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXX_HAS_ATOMIC_LIB"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXXABI_ENABLE_STATIC_UNWINDER"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBCXXABI_USE_COMPILER_RT"
	LinuxRuntimePassthroughVariables="$LinuxRuntimePassthroughVariables;LIBUNWIND_USE_COMPILER_RT"
	AddCMakeCacheValue "LLVM_EXTERNAL_PROJECT_PASSTHROUGH" "$LinuxRuntimePassthroughVariables"
	AddCMakeCacheValue "LIBCXX_ENABLE_STATIC_ABI_LIBRARY" "ON"
	AddCMakeCacheValue "LIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY" "ON"
	AddCMakeCacheValue "LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY" "ON"
	AddCMakeCacheValue "LIBCXX_ENABLE_ABI_LINKER_SCRIPT" "OFF"
	AddCMakeCacheValue "LIBCXX_USE_COMPILER_RT" "ON"
	AddCMakeCacheValue "LIBCXX_HAS_ATOMIC_LIB" "OFF"
	AddCMakeCacheValue "LIBCXXABI_ENABLE_STATIC_UNWINDER" "ON"
	AddCMakeCacheValue "LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY" "ON"
	AddCMakeCacheValue "LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY" "ON"
	AddCMakeCacheValue "LIBCXXABI_USE_COMPILER_RT" "ON"
	AddCMakeCacheValue "LIBUNWIND_USE_COMPILER_RT" "ON"
	AddCMakeCacheValue "COMPILER_RT_CXX_LIBRARY" "libcxx"
	AddCMakeCacheValue "COMPILER_RT_STATIC_CXX_LIBRARY" "ON"
	AddCMakeCacheValue "COMPILER_RT_USE_BUILTINS_LIBRARY" "ON"
	AddCMakeCacheValue "COMPILER_RT_USE_LLVM_UNWINDER" "ON"
	AddCMakeCacheValue "COMPILER_RT_ENABLE_STATIC_UNWINDER" "ON"
	AddCMakeCacheValue "SANITIZER_CXX_ABI" "libc++"
	AddCMakeCacheValue "SANITIZER_CXX_ABI_INTREE" "ON"
	AddCMakeCacheValue "SANITIZER_USE_STATIC_CXX_ABI" "ON"
	AddCMakeCacheValue "SANITIZER_TEST_CXX" "libc++"
	AddCMakeCacheValue "SANITIZER_TEST_CXX_INTREE" "ON"
	AddCMakeCacheValue "SANITIZER_USE_STATIC_TEST_CXX" "ON"
	AddCMakeCacheValue "SANITIZER_USE_STATIC_LLVM_UNWINDER" "ON"
	ConfigureVendoredZlib
	ConfigureVendoredLibEdit
	ConfigureVendoredLua
	ConfigureLinuxStaticSystemLibraries
	ConfigureVendoredLibXml2
	ConfigureVendoredCurl
	ConfigureLinuxCurses
fi

AddCMakeCacheValue "LLVM_ENABLE_ZSTD" "FORCE_ON"

ExtraCMake="$ExtraCMake -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_INSTALL_TOOLCHAIN_ONLY=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLVM_INSTALL_TOOLCHAIN_ONLY=ON"

# Enable Python support for LLDB
ExtraCMake="$ExtraCMake -DLLDB_ENABLE_PYTHON=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLDB_ENABLE_PYTHON=ON"
ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_LLDB_ENABLE_PYTHON=ON"

if [[ "$MalterlibPlatform" == "macOS" ]]; then
	ConfigureMacOSCommandLineToolsPython
	ConfigureDynamicPythonRuntime "3.9" "0x03090000"
	ConfigureVendoredZlib
	ConfigureVendoredLibEdit
	ConfigureVendoredLua
	ConfigureVendoredLibXml2
	ConfigureVendoredCurl
	ConfigureVendoredPDCurses
fi

ConfigureVendoredZstd

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
	local RequestedStage="${1:-all}"
	mkdir -p "$RootDir/build/dist_temp"
	pushd "$RootDir/build"
		local BuildDir="$PWD"
	popd

	ConfigureLinuxBootstrapBuildRuntimePath "$BuildDir/dist_temp"

	ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_PGO=ON"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_LLVM_VP_COUNTERS_PER_SITE=3"
	ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_CMAKE_INSTALL_PREFIX=$DistributionDir"

	if [[ "$MalterlibPlatform" != "Linux" ]]; then
		ExtraCMake="$ExtraCMake -DCLANG_PGO_TRAINING_PROFILES_DIR=$BuildDir/profiles/"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_CLANG_PGO_TRAINING_PROFILES_DIR=$BuildDir/profiles/"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_BOOTSTRAP_CLANG_PGO_TRAINING_PROFILES_DIR=$BuildDir/profiles/"
	fi

	if [[ "$RequestedStage" == "all" || "$RequestedStage" == "stage1" ]]; then
		pushd "$BuildDir/dist_temp"
			echo Configuring LLVM bootstrap build

			cmake $ExtraCMake -C "$MalterlibRoot/External/llvm-project/clang/cmake/caches/Release.cmake" "$MalterlibRoot/External/llvm-project/llvm"
			RunNinja clang-bootstrap-deps
		popd
	fi

	if [[ "$RequestedStage" == "all" || "$RequestedStage" == "instrumented" ]]; then
		if [[ ! -f "$BuildDir/dist_temp/CMakeCache.txt" ]]; then
			echo "LLVM build is not configured: $BuildDir/dist_temp"
			echo "Run LLVMBuildStage=stage1 first."
			exit 1
		fi

		pushd "$BuildDir/dist_temp"
			RunNinja stage2-instrumented
		popd
	fi

	if [[ "$RequestedStage" == "all" || "$RequestedStage" == "training" ]]; then
		if [[ ! -x "$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins/bin/clang" ]]; then
			echo "Instrumented clang is missing: $BuildDir/dist_temp/tools/clang/stage2-instrumented-bins/bin/clang"
			echo "Run LLVMBuildStage=instrumented first."
			exit 1
		fi

		# Generate profiling data
		rm -rf "$BuildDir/profiles"
		mkdir -p "$BuildDir/profiles"
		if [[ "$MalterlibPlatform" == "macOS" ]]; then
			export LLVM_PROFILE_FILE="$BuildDir/profiles/malterlib-%${NCPUS}m%c.profraw"
		else
			export LLVM_PROFILE_FILE="$BuildDir/profiles/malterlib-%${NCPUS}m.profraw"
		fi
		pushd "$MalterlibRoot"

			export MalterlibDisableBuildSystemGeneration=true
			export PlatformToolsetCompiler="$BuildDir/dist_temp/tools/clang/stage2-instrumented-bins/bin/clang"
			export MalterlibImportUpdateCache=false

			./mib generate --no-use-user-settings Tests --reconcile-removed=*:leave

			(source "$MalterlibRoot/BuildSystem/SharedBuildSettings.sh"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Int"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Out")

			./mib build Tests || true

			(source "$MalterlibRoot/BuildSystem/SharedBuildSettings.sh"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Int"; rm -rf "$MalterlibCompiledFilesSourceBase/Tests/Out")
			rm -rf /opt/Deploy/Tests /Deploy/Tests || true
			df -h "$BuildDir" || true
		popd
	fi

	if [[ "$RequestedStage" == "all" || "$RequestedStage" == "final" ]]; then
		if [[ ! -f "$BuildDir/dist_temp/CMakeCache.txt" ]]; then
			echo "LLVM build is not configured: $BuildDir/dist_temp"
			echo "Run LLVMBuildStage=stage1 first."
			exit 1
		fi

		pushd "$BuildDir/dist_temp"
			cmake $ExtraCMake -C "$MalterlibRoot/External/llvm-project/clang/cmake/caches/Release.cmake" "$MalterlibRoot/External/llvm-project/llvm"
			CleanDistributionDir

			if [[ "$BuildIncremental" == "true" ]]; then
				rm -f "tools/clang/stage2-stamps/stage2-really-install"
			fi

			InstallLinuxRuntimeLibraryLinksInDir "$BuildDir/dist_temp"
			InstallLinuxRuntimeLibraryLinksInDir "$BuildDir/dist_temp/tools/clang/stage2-bins"
			RunNinja stage2-install
			InstallLinuxRuntimeLibraryLinks
		popd
	fi
}

BuildCompiler()
{
	local RequestedStage="${1:-all}"
	export StandaloneBuild=true
	mkdir -p "$RootDir/build/dist_temp2"
	pushd "$RootDir/build"
		local BuildDir="$PWD"
	popd
	ConfigureLinuxBootstrapBuildRuntimePath "$BuildDir/dist_temp2"

	pushd "$RootDir/build/dist_temp2"
		ExtraCMake="$ExtraCMake -DBOOTSTRAP_CMAKE_INSTALL_PREFIX=$DistributionDir"
		ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_PGO=OFF"
		ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_LTO=OFF"
		ExtraCMake="$ExtraCMake -DLLVM_RELEASE_ENABLE_BOLTOPT=OFF"

		if [[ "$RequestedStage" == "all" || "$RequestedStage" == "stage1" ]]; then
			cmake $ExtraCMake -C "$MalterlibRoot/External/llvm-project/clang/cmake/caches/Release.cmake" "$MalterlibRoot/External/llvm-project/llvm"
			RunNinja clang-bootstrap-deps
		fi

		if [[ "$RequestedStage" == "all" || "$RequestedStage" == "final" ]]; then
			if [[ ! -f CMakeCache.txt ]]; then
				echo "LLVM build is not configured: $PWD"
				echo "Run LLVMBuildStage=stage1 first."
				exit 1
			fi

			cmake $ExtraCMake -C "$MalterlibRoot/External/llvm-project/clang/cmake/caches/Release.cmake" "$MalterlibRoot/External/llvm-project/llvm"
			CleanDistributionDir

			if [[ "$BuildIncremental" == "true" ]]; then
				rm -f "tools/clang/stage2-stamps/stage2-really-install"
			fi

			InstallLinuxRuntimeLibraryLinksInDir "$BuildDir/dist_temp2"
			InstallLinuxRuntimeLibraryLinksInDir "$BuildDir/dist_temp2/tools/clang/stage2-bins"
			RunNinja stage2-install
			InstallLinuxRuntimeLibraryLinks
		fi
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

		CleanDistributionDir

		RunNinja install
		InstallLinuxRuntimeLibraryLinks
	popd
}

case "$LLVMBuildStage" in
	all|stage1|instrumented|training|final)
		;;
	*)
		echo "Unsupported LLVMBuildStage: $LLVMBuildStage"
		exit 1
		;;
esac

if [[ "$1" == "dev" ]]; then
	BuildDevCompiler
elif [[ "$MalterlibPlatform/$MalterlibArch" == "Linux/x86" ]] || [[ "$1" == "debug" ]]; then
	case "$LLVMBuildStage" in
		all|stage1|final)
			BuildCompiler "$LLVMBuildStage"
			;;
		*)
			echo "LLVMBuildStage=$LLVMBuildStage is not used for $MalterlibPlatform/$MalterlibArch"
			;;
	esac
else
	BuildCompilerLTO "$LLVMBuildStage"
fi
