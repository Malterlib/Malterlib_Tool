#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Checks that an installed LLVM distribution holds what the build made.
#
# Usage: VerifyLLVMDistribution.sh <distribution directory> [<second install of the same build>]
#
# Fails when a file that isn't empty is all zero bytes, which no file of a distribution is, and, given a second install
# of the same build, when the two don't hold the same files and symbolic links with the same content.
#
# The distributions the macOS runners built on 2026-11-15 had files of the right size that were all zero bytes: seven
# Python files of lldb and llvm-mca, all written by the build moments before the install copied them.

set -e
set -o pipefail

if [[ $# -lt 1 || $# -gt 2 || ! -d "$1" || ( $# -eq 2 && ! -d "$2" ) ]]; then
	echo "Usage: $0 <distribution directory> [<second install of the same build>]" >&2
	exit 2
fi

DistributionDir="$1"
SecondDir="${2:-}"
Failures=0

ReportFailure()
{
	Failures=$((Failures + 1))
	if (( Failures <= 50 )); then
		echo "$1" >&2
	fi
}

# The files and symbolic links of a directory, without what starts with a dot in the directory itself, like the
# repository a distribution directory is a checkout of, which cleaning the directory before an install keeps.
ListFiles()
{
	(cd "$1" && find . -path './.*' -prune -o \( -type f -o -type l \) -print | LC_ALL=C sort)
}

TempDir="$(mktemp -d)"
trap 'rm -rf "$TempDir"' EXIT

ListFiles "$DistributionDir" > "$TempDir/Files"

if [[ ! -s "$TempDir/Files" ]]; then
	echo "The distribution has no files: $DistributionDir" >&2
	exit 1
fi

while IFS= read -r File; do
	Path="$DistributionDir/$File"
	if [[ -L "$Path" || ! -s "$Path" ]]; then
		continue
	fi

	Size="$(wc -c < "$Path" | tr -d '[:space:]')"
	if cmp -s -n "$Size" "$Path" /dev/zero; then
		ReportFailure "All zero bytes: $File ($Size bytes)"
	fi
done < "$TempDir/Files"

if [[ -n "$SecondDir" ]]; then
	ListFiles "$SecondDir" > "$TempDir/SecondFiles"

	while IFS= read -r File; do
		ReportFailure "Only in one of the installs: $File"
	done < <(LC_ALL=C comm -3 "$TempDir/Files" "$TempDir/SecondFiles" | tr -d '\t')

	while IFS= read -r File; do
		Path="$DistributionDir/$File"
		SecondPath="$SecondDir/$File"

		if [[ -L "$Path" || -L "$SecondPath" ]]; then
			if [[ ! -L "$Path" || ! -L "$SecondPath" || "$(readlink "$Path")" != "$(readlink "$SecondPath")" ]]; then
				ReportFailure "The symbolic links differ: $File"
			fi
		elif ! cmp -s "$Path" "$SecondPath"; then
			ReportFailure "The installs differ: $File"
		fi
	done < <(LC_ALL=C comm -12 "$TempDir/Files" "$TempDir/SecondFiles")
fi

FileCount="$(wc -l < "$TempDir/Files" | tr -d '[:space:]')"

if (( Failures > 0 )); then
	if (( Failures > 50 )); then
		echo "... and $((Failures - 50)) more" >&2
	fi
	echo "The LLVM distribution is not what the build made: $Failures of $FileCount files failed in $DistributionDir" >&2
	exit 1
fi

if [[ -n "$SecondDir" ]]; then
	echo "Verified the LLVM distribution: $FileCount files, none all zero bytes, the same as the second install"
else
	echo "Verified the LLVM distribution: $FileCount files, none all zero bytes"
fi
