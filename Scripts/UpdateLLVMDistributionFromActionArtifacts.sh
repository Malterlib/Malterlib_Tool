#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e
set -o pipefail

cd "$( dirname "${BASH_SOURCE[0]}" )"
echo Directory: $PWD
ScriptDir="$PWD"

pushd ../../.. >/dev/null
	MalterlibRoot="$PWD"
popd >/dev/null

Repo="${GitHubRepository:-${GITHUB_REPOSITORY:-Malterlib/Malterlib}}"
Workflow="${MalterlibLLVMWorkflow:-Build_LLVM_Distribution.yml}"
RunSearchLimit="${MalterlibLLVMRunSearchLimit:-25}"
RunId="${MalterlibLLVMRunId:-}"

export GH_PAGER=cat
export GH_PROMPT_DISABLED=1
export GH_NO_UPDATE_NOTIFIER=1

# Run gh without letting it take over the controlling terminal. gh's prompt
# library can switch the tty into raw mode, which suppresses Ctrl+C and leaves
# the console in an altered state. Redirecting stdin from /dev/null together with
# GH_PROMPT_DISABLED keeps gh fully non-interactive so signals keep working.
fg_gh()
{
	gh "$@" </dev/null
}

TempDir=""
TerminalState=""
if [[ -r /dev/tty ]]; then
	TerminalState="`{ stty -g < /dev/tty; } 2>/dev/null || true`"
fi

fg_Cleanup()
{
	local Result="$?"
	local DownloadPid

	if declare -p DownloadPids >/dev/null 2>&1; then
		for DownloadPid in "${DownloadPids[@]}"
		do
			kill "$DownloadPid" >/dev/null 2>&1 || true
		done

		for DownloadPid in "${DownloadPids[@]}"
		do
			wait "$DownloadPid" >/dev/null 2>&1 || true
		done
	fi

	if [[ -n "$TempDir" ]]; then
		rm -rf "$TempDir"
	fi

	if [[ -n "$TerminalState" ]]; then
		{ stty "$TerminalState" < /dev/tty; } 2>/dev/null || { stty echo < /dev/tty; } 2>/dev/null || true
	fi

	return "$Result"
}

trap fg_Cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

fg_Usage()
{
	echo "Usage: $0 [--dry-run] [--run-id <id>] [--repo <owner/repo>] [--workflow <file-or-name>] [--host|all|host|Target...]"
	echo
	echo "Downloads MalterlibLLVM-* GitHub Actions artifacts and deploys them to Binaries/MalterlibLLVM."
	echo "Targets: Linux/x64, Linux/arm64, Linux/x86, macOS/arm64, macOS/x64, Windows/x64, Windows/arm64"
	echo
	echo "If --run-id is omitted, the newest $Workflow run with a successful artifact for each enabled target is used."
	echo "--host or host downloads only the current host platform/architecture."
	echo "--dry-run prints the selected runs and artifacts without downloading or updating files."
}

fg_NormalizeTarget()
{
	local Target="${1//-//}"
	Target="${Target//_//}"

	case "$Target" in
		Linux/x64|linux/x64)
			echo "Linux/x64"
			;;
		Linux/arm64|linux/arm64)
			echo "Linux/arm64"
			;;
		Linux/x86|linux/x86)
			echo "Linux/x86"
			;;
		macOS/arm64|macos/arm64|MacOS/arm64)
			echo "macOS/arm64"
			;;
		macOS/x64|macos/x64|MacOS/x64)
			echo "macOS/x64"
			;;
		Windows/x64|windows/x64)
			echo "Windows/x64"
			;;
		Windows/arm64|windows/arm64)
			echo "Windows/arm64"
			;;
		*)
			return 1
			;;
	esac
}

fg_AddTarget()
{
	local Target="$1"
	local ExistingTarget

	for ExistingTarget in "${Targets[@]}"
	do
		if [[ "$ExistingTarget" == "$Target" ]]; then
			return
		fi
	done

	Targets+=("$Target")
}

fg_DetectHostTarget()
{
	local HostPlatform="${MalterlibPlatform-}"
	local HostArch="${MalterlibArch-}"
	local SysName
	local ProcessorArch

	if [[ ( -z "$HostPlatform" || -z "$HostArch" ) && -f "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh" ]]; then
		source "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh"
		HostPlatform="$MalterlibPlatform"
		HostArch="$MalterlibArch"
	fi

	if [[ -z "$HostPlatform" || -z "$HostArch" ]]; then
		SysName="$(uname -s)"
		ProcessorArch="$(uname -m)"

		if [[ $SysName == MSYS* ]] || [[ $SysName == MINGW* ]] || [[ $SysName == CYGWIN* ]] || [[ $SysName == windows* ]]; then
			HostPlatform=Windows
			if [[ "${PROCESSOR_IDENTIFIER-}" == ARMv8\ \(64-bit\)* ]]; then
				HostArch=arm64
			elif [[ $ProcessorArch == i*86 ]]; then
				HostArch=x86
			elif [[ $ProcessorArch == x86_64 ]]; then
				HostArch=x64
			fi
		elif [[ $SysName == Darwin* ]]; then
			HostPlatform=macOS
			if [[ $ProcessorArch == x86_64 ]]; then
				HostArch=x64
			elif [[ $ProcessorArch == arm64 ]]; then
				HostArch=arm64
			fi
		elif [[ $SysName == Linux* ]]; then
			HostPlatform=Linux
			if [[ $ProcessorArch == i*86 ]]; then
				HostArch=x86
			elif [[ $ProcessorArch == aarch64 ]]; then
				HostArch=arm64
			elif [[ $ProcessorArch == x86_64 ]]; then
				if [[ "$(getconf LONG_BIT)" == "32" ]]; then
					HostArch=x86
				else
					HostArch=x64
				fi
			fi
		fi
	fi

	fg_NormalizeTarget "$HostPlatform/$HostArch"
}

fg_TargetToDisplay()
{
	local Target="$1"
	local Platform="${Target%/*}"
	local Arch="${Target#*/}"

	echo "$Platform $Arch"
}

fg_TargetToArtifactName()
{
	local Target="$1"
	local Platform="${Target%/*}"
	local Arch="${Target#*/}"

	echo "MalterlibLLVM-$Platform-$Arch.tar.zst"
}

fg_TargetToFinalStatusJobName()
{
	local Target="$1"
	local Display

	Display="$(fg_TargetToDisplay "$Target")"

	echo "$Display / $Display final / $Display final status"
}

fg_FindArtifactId()
{
	local WantedArtifact="$1"
	local Artifacts="$2"
	local ArtifactName
	local ArtifactId

	while IFS=$'\t' read -r ArtifactName ArtifactId
	do
		if [[ "$ArtifactName" == "$WantedArtifact" ]]; then
			echo "$ArtifactId"
			return 0
		fi
	done <<< "$Artifacts"

	return 1
}

fg_ListRunArtifacts()
{
	local CandidateRunId="$1"

	fg_gh api \
		"repos/$Repo/actions/runs/$CandidateRunId/artifacts" \
		--paginate \
		--jq '.artifacts[]? | select((.expired // false) | not) | "\(.name)\t\(.id)"'
}

fg_ListRunJobs()
{
	local CandidateRunId="$1"

	fg_gh run view "$CandidateRunId" \
		--repo "$Repo" \
		--json jobs \
		--jq '.jobs[] | "\(.name)\t\(.conclusion // "")"'
}

fg_HasEnabledTarget()
{
	local Target="$1"
	local Jobs="$2"
	local Display
	local JobName
	local Conclusion

	Display="$(fg_TargetToDisplay "$Target")"

	while IFS=$'\t' read -r JobName Conclusion
	do
		if [[ "$JobName" == "$Display" || "$JobName" == "$Display / "* ]]; then
			return 0
		fi
	done <<< "$Jobs"

	return 1
}

fg_HasSuccessfulFinalStatusTarget()
{
	local Target="$1"
	local Jobs="$2"
	local WantedJobName
	local JobName
	local Conclusion

	WantedJobName="$(fg_TargetToFinalStatusJobName "$Target")"

	while IFS=$'\t' read -r JobName Conclusion
	do
		if [[ "$Conclusion" == "success" && "$JobName" == "$WantedJobName" ]]; then
			return 0
		fi
	done <<< "$Jobs"

	return 1
}

fg_IsTargetSelected()
{
	local Target
	local SelectedTarget

	Target="$1"

	for SelectedTarget in "${SelectedTargets[@]}"
	do
		if [[ "$SelectedTarget" == "$Target" ]]; then
			return 0
		fi
	done

	return 1
}

fg_HaveSelectedAllSearchTargets()
{
	local Target

	for Target in "${SearchTargets[@]}"
	do
		if ! fg_IsTargetSelected "$Target"; then
			return 1
		fi
	done

	return 0
}

fg_SelectTargetFromRun()
{
	local Target="$1"
	local CandidateRunId
	local Artifacts
	local Jobs
	local WantedArtifact
	local ArtifactId
	local bReportFailure

	CandidateRunId="$2"
	Artifacts="$3"
	Jobs="$4"
	bReportFailure="${5:-true}"
	WantedArtifact="$(fg_TargetToArtifactName "$Target")"

	ArtifactId="$(fg_FindArtifactId "$WantedArtifact" "$Artifacts")" || {
		if [[ "$bReportFailure" == "true" ]]; then
			echo "Run $CandidateRunId has $Target enabled, but it does not contain non-expired artifact $WantedArtifact." >&2
		fi
		return 1
	}

	if ! fg_HasSuccessfulFinalStatusTarget "$Target" "$Jobs"; then
		if [[ "$bReportFailure" == "true" ]]; then
			echo "Run $CandidateRunId has $Target enabled, but the final status job for that target did not succeed." >&2
		fi
		return 1
	fi

	SelectedTargets+=("$Target")
	SelectedArtifacts+=("$WantedArtifact")
	SelectedArtifactIds+=("$ArtifactId")
	SelectedRunIds+=("$CandidateRunId")

	return 0
}

fg_SelectFromPinnedRun()
{
	local Artifacts
	local Jobs
	local Target
	local nSelectedBefore
	local bSawEnabledTarget

	Artifacts="$(fg_ListRunArtifacts "$RunId")"
	Jobs="$(fg_ListRunJobs "$RunId")"
	nSelectedBefore="${#SelectedTargets[@]}"
	bSawEnabledTarget=false

	for Target in "${SearchTargets[@]}"
	do
		if ! fg_HasEnabledTarget "$Target" "$Jobs"; then
			if [[ "$AllTargets" == "true" ]]; then
				continue
			fi

			echo "Run $RunId was not started with $Target enabled." >&2
			exit 1
		fi

		bSawEnabledTarget=true
		if [[ "$AllTargets" == "true" ]]; then
			fg_SelectTargetFromRun "$Target" "$RunId" "$Artifacts" "$Jobs" false || true
		else
			fg_SelectTargetFromRun "$Target" "$RunId" "$Artifacts" "$Jobs" || exit 1
		fi
	done

	if [[ "${#SelectedTargets[@]}" == "$nSelectedBefore" ]]; then
		if [[ "$bSawEnabledTarget" == "true" ]]; then
			echo "Run $RunId did not contain any requested enabled targets with successful non-expired artifacts." >&2
		else
			echo "Run $RunId was not started with any requested targets enabled." >&2
		fi
		exit 1
	fi
}

fg_FindRuns()
{
	local CandidateRunId
	local Artifacts
	local Jobs
	local Target
	local EnabledTargets
	local bFoundEnabledTarget

	if [[ -n "$RunId" ]]; then
		fg_SelectFromPinnedRun
		return
	fi

	while IFS= read -r CandidateRunId
	do
		Jobs="$(fg_ListRunJobs "$CandidateRunId")"
		EnabledTargets=()

		for Target in "${SearchTargets[@]}"
		do
			if fg_IsTargetSelected "$Target"; then
				continue
			fi

			if fg_HasEnabledTarget "$Target" "$Jobs"; then
				EnabledTargets+=("$Target")
			fi
		done

		if [[ "${#EnabledTargets[@]}" == "0" ]]; then
			continue
		fi

		Artifacts="$(fg_ListRunArtifacts "$CandidateRunId")"
		for Target in "${EnabledTargets[@]}"
		do
			fg_SelectTargetFromRun "$Target" "$CandidateRunId" "$Artifacts" "$Jobs" false || true
		done

		if fg_HaveSelectedAllSearchTargets; then
			return
		fi
	done < <(
		fg_gh run list \
			--repo "$Repo" \
			--workflow "$Workflow" \
			--limit "$RunSearchLimit" \
			--json databaseId \
			--jq '.[].databaseId'
	)

	if [[ "$AllTargets" != "true" ]]; then
		bFoundEnabledTarget=true
		for Target in "${SearchTargets[@]}"
		do
			if ! fg_IsTargetSelected "$Target"; then
				echo "No $Workflow run in $Repo had $Target enabled with a successful non-expired artifact within the newest $RunSearchLimit runs." >&2
				bFoundEnabledTarget=false
			fi
		done

		if [[ "$bFoundEnabledTarget" != "true" ]]; then
			exit 1
		fi
	elif [[ "${#SelectedTargets[@]}" == "0" ]]; then
		echo "No $Workflow run in $Repo had any requested targets enabled with successful non-expired artifacts within the newest $RunSearchLimit runs." >&2
		exit 1
	fi
}

SupportedTargets=(Linux/x64 Linux/arm64 Linux/x86 macOS/arm64 macOS/x64 Windows/x64 Windows/arm64)
AllTargets=true
DryRun=false
SawAll=false
SawTarget=false
SawHost=false
Targets=()
SearchTargets=()
SelectedTargets=()
SelectedArtifacts=()
SelectedArtifactIds=()
SelectedRunIds=()

while [[ $# -gt 0 ]]
do
	case "$1" in
		--help|-h)
			fg_Usage
			exit 0
			;;
		--dry-run)
			DryRun=true
			shift
			;;
		--repo)
			if [[ $# -lt 2 ]]; then
				echo "--repo requires a value." >&2
				exit 1
			fi
			Repo="$2"
			shift 2
			;;
		--workflow)
			if [[ $# -lt 2 ]]; then
				echo "--workflow requires a value." >&2
				exit 1
			fi
			Workflow="$2"
			shift 2
			;;
		--run-id)
			if [[ $# -lt 2 ]]; then
				echo "--run-id requires a value." >&2
				exit 1
			fi
			RunId="$2"
			shift 2
			;;
		--limit)
			if [[ $# -lt 2 ]]; then
				echo "--limit requires a value." >&2
				exit 1
			fi
			RunSearchLimit="$2"
			shift 2
			;;
		--host|host)
			SawHost=true
			AllTargets=false
			Target="$(fg_DetectHostTarget)" || {
				echo "Unable to detect a supported host target." >&2
				exit 1
			}
			fg_AddTarget "$Target"
			shift
			;;
		all)
			SawAll=true
			shift
			;;
		*)
			Target="`fg_NormalizeTarget "$1"`" || {
				echo "Unsupported target: $1" >&2
				echo "Use all, host, Linux/x64, Linux/arm64, Linux/x86, macOS/arm64, macOS/x64, Windows/x64, or Windows/arm64." >&2
				exit 1
			}
			SawTarget=true
			AllTargets=false
			fg_AddTarget "$Target"
			shift
			;;
	esac
done

if [[ "$SawAll" == "true" && ( "$SawTarget" == "true" || "$SawHost" == "true" ) ]]; then
	echo "Use either all, host, or specific targets, not more than one mode." >&2
	exit 1
fi

if [[ "$SawHost" == "true" && "$SawTarget" == "true" ]]; then
	echo "Use host by itself when requesting only the host platform/architecture." >&2
	exit 1
fi

if ! [[ "$RunSearchLimit" =~ ^[1-9][0-9]*$ ]]; then
	echo "--limit must be a positive integer, got: $RunSearchLimit" >&2
	exit 1
fi

if [[ "$AllTargets" == "true" ]]; then
	SearchTargets=("${SupportedTargets[@]}")
else
	SearchTargets=("${Targets[@]}")
fi

if ! command -v gh >/dev/null 2>&1; then
	echo "gh is required and must be authenticated for $Repo."
	exit 1
fi

fg_FindRuns

echo "Using workflow run(s):"
PrintedRunIds=()
for RunIdForSummary in "${SelectedRunIds[@]}"
do
	bAlreadyPrinted=false
	for PrintedRunId in "${PrintedRunIds[@]}"
	do
		if [[ "$PrintedRunId" == "$RunIdForSummary" ]]; then
			bAlreadyPrinted=true
			break
		fi
	done

	if [[ "$bAlreadyPrinted" == "true" ]]; then
		continue
	fi

	PrintedRunIds+=("$RunIdForSummary")
	fg_gh run view "$RunIdForSummary" \
		--repo "$Repo" \
		--json displayTitle,headSha,url,createdAt \
		--jq '"  \(.displayTitle)\n  Run: \(.url)\n  Commit: \(.headSha)\n  Created: \(.createdAt)"'
done

if [[ "$DryRun" == "true" ]]; then
	echo "Dry run: selected artifact update(s):"
	for ((i = 0; i < ${#SelectedArtifacts[@]}; ++i))
	do
		Target="${SelectedTargets[$i]}"
		ArtifactName="${SelectedArtifacts[$i]}"
		ArtifactRunId="${SelectedRunIds[$i]}"
		Platform="${Target%/*}"
		Arch="${Target#*/}"
		Destination="$MalterlibRoot/Binaries/MalterlibLLVM/$Platform/$Arch"

		echo "  $Target: $ArtifactName from run $ArtifactRunId -> $Destination"
	done
	echo "Dry run: no artifacts downloaded, no files updated, and LICENSE regeneration skipped."
	exit 0
fi

TempDir="$(mktemp -d)"

TarTool=""
if [[ -f "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh" ]]; then
	source "$MalterlibRoot/Malterlib/Core/Scripts/Detect.sh"
	TarTool="$MToolDirectory/bsdtar"
	if [[ ! -f "$TarTool" && -f "$TarTool.exe" ]]; then
		TarTool="$TarTool.exe"
	fi
	if [[ ! -f "$TarTool" ]]; then
		TarTool=""
	fi
fi

ExtractArchive()
{
	local Archive="$1"
	local Destination="$2"

	if [[ -n "$TarTool" ]]; then
		"$TarTool" -xf "$Archive" -C "$Destination"
	elif tar -tf "$Archive" >/dev/null 2>&1; then
		tar -xf "$Archive" -C "$Destination"
	elif command -v zstd >/dev/null 2>&1; then
		zstd -dc "$Archive" | tar -xf - -C "$Destination"
	else
		echo "Unable to extract $Archive. Install zstd or make sure Malterlib bsdtar is available."
		exit 1
	fi
}

nUpdated=0
DownloadArtifacts=()
DownloadRunIds=()
DownloadPids=()
LicenseRepoFilters=()

fg_DownloadArtifact()
{
	local ArtifactName="$1"
	local ArtifactId="$2"
	local DownloadDir="$TempDir/$ArtifactName"
	local LogFile="$DownloadDir.log"
	local Archive="$DownloadDir/$ArtifactName"
	local Magic

	echo "Downloading $ArtifactName (artifact $ArtifactId)"
	mkdir -p "$DownloadDir"

	# The distribution artifacts are uploaded with actions/upload-artifact's
	# archive: false, so GitHub serves them as the raw .tar.zst blob rather than a
	# zip. "gh run download" always assumes a zip and fails with "not a valid zip
	# file", so fetch the blob straight from the artifacts API instead. gh's
	# stderr goes to a log that is only surfaced on failure; sending the blob to a
	# file (not the terminal) also keeps gh from drawing its progress spinner.
	if ! fg_gh api "repos/$Repo/actions/artifacts/$ArtifactId/zip" \
		>"$Archive" 2>"$LogFile"
	then
		cat "$LogFile" >&2
		return 1
	fi

	# Fail loudly if the blob is not a zstd archive (e.g. if the artifacts are
	# ever uploaded zipped again), instead of feeding the wrong thing to the
	# extractor and silently deploying garbage.
	Magic="$(od -An -N4 -tx1 "$Archive" | tr -d ' ')"
	if [[ "$Magic" != "28b52ffd" ]]; then
		echo "Downloaded $ArtifactName is not a zstd archive (magic: $Magic); the artifact format may have changed." >&2
		return 1
	fi
}

for ((i = 0; i < ${#SelectedArtifacts[@]}; ++i))
do
	ArtifactName="${SelectedArtifacts[$i]}"
	ArtifactId="${SelectedArtifactIds[$i]}"
	ArtifactRunId="${SelectedRunIds[$i]}"

	if [[ ! "$ArtifactName" =~ ^MalterlibLLVM-(Linux|macOS|Windows)-(x86|x64|arm64)\.tar\.zst$ ]]; then
		continue
	fi

	DownloadArtifacts+=("$ArtifactName")
	DownloadRunIds+=("$ArtifactRunId")
	fg_DownloadArtifact "$ArtifactName" "$ArtifactId" &
	DownloadPids+=("$!")
done

if [[ "${#DownloadPids[@]}" == "0" ]]; then
	echo "No matching LLVM distribution artifacts were selected."
	exit 1
fi

DownloadFailed=false
for ((i = 0; i < ${#DownloadPids[@]}; ++i))
do
	if ! wait "${DownloadPids[$i]}"; then
		echo "Download failed for ${DownloadArtifacts[$i]} from run ${DownloadRunIds[$i]}." >&2
		DownloadFailed=true
	fi
done
DownloadPids=()

if [[ "$DownloadFailed" == "true" ]]; then
	exit 1
fi

for ArtifactName in "${DownloadArtifacts[@]}"
do
	[[ "$ArtifactName" =~ ^MalterlibLLVM-(Linux|macOS|Windows)-(x86|x64|arm64)\.tar\.zst$ ]]
	Platform="${BASH_REMATCH[1]}"
	Arch="${BASH_REMATCH[2]}"
	DownloadDir="$TempDir/$ArtifactName"
	Archive=""
	Destination="$MalterlibRoot/Binaries/MalterlibLLVM/$Platform/$Arch"
	LicenseRepoFilter="Binaries/MalterlibLLVM/$Platform/$Arch"

	Archive="`find "$DownloadDir" -name '*.tar.zst' -type f -print -quit`"
	if [[ -z "$Archive" ]]; then
		echo "Downloaded artifact $ArtifactName did not contain a .tar.zst archive." >&2
		exit 1
	fi

	echo "Updating $Destination"
	mkdir -p "$Destination"
	find "$Destination" -mindepth 1 -maxdepth 1 ! -name '.*' -exec rm -rf {} +
	ExtractArchive "$Archive" "$Destination"

	((++nUpdated))
	LicenseRepoFilters+=("$LicenseRepoFilter")
done

if [[ "$nUpdated" == "0" ]]; then
	echo "No matching LLVM distribution artifacts were downloaded."
	exit 1
fi

echo "Updated $nUpdated LLVM distribution(s)."

# Regenerate the LICENSE files in the updated binary repositories so they reflect
# the current bundled components (Malterlib, LLVM, the bundled Python runtime, ...).
(
	cd "$MalterlibRoot"
	for LicenseRepoFilter in "${LicenseRepoFilters[@]}"
	do
		echo "Regenerating LICENSE files for $LicenseRepoFilter (mib check-license --fix)..."
		./mib check-license -n "$LicenseRepoFilter" --fix
	done
)
