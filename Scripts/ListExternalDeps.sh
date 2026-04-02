#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#
# Lists External repositories whose files are used to build given ninja targets.
#
# Usage: list-external-deps.sh <build-dir> <target> [<target>...]
#
# Example:
#   list-external-deps.sh "BuildSystem/Default/MTool/macOS arm64 Release Testing" Com_MTool_MTool
#   list-external-deps.sh "BuildSystem/Default/Apps_Malterlib_Cloud/macOS arm64 Release Testing" \
#       Com_Malterlib_Cloud_AppManager Com_Malterlib_Cloud_KeyManager

set -eo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

source "$DIR/../../Core/BuildScripts/DetectSystem.sh"

set -u

NINJA="$MToolDirectory/ninja"

if [ $# -lt 2 ]; then
	echo "Usage: $0 <build-dir> <target> [<target>...]" >&2
	exit 1
fi

BUILDDIR="$1"
shift

# Get the list of External repository paths (sorted longest first for matching)
REPO_PATHS=$(./mib repo-run --no-color -n "External/*" -- echo 'repo' 2>/dev/null | grep -oE 'External/[^ ]+' | awk '{ print length, $0 }' | sort -rn | cut -d' ' -f2- | sort -u)

# Get all input files for all targets in one query
ALL_INPUTS=$("$NINJA" -C "$BUILDDIR" -t inputs "$@" 2>/dev/null | grep "/External/" | sort -u)

# Map input files to the longest matching repository path
echo "$ALL_INPUTS" | while read -r src; do
	[ -z "$src" ] && continue
	while IFS= read -r repo; do
		case "$src" in
			*"/$repo/"*)
				echo "$repo"
				break
				;;
		esac
	done <<< "$REPO_PATHS"
done | sort -u
