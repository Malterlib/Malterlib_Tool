#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Refreshes every base-image digest pin under Malterlib/Tool/Docker/LinuxSDK
# to the latest date-stamped image whose build date is at or before the apt
# snapshot date used by BuildLinuxSDKContainers.sh. Pinning the base image
# to a "before the snapshot" date keeps the pre-installed apt state (and
# pre-installed packages in the image) consistent with the snapshot-pinned
# package universe the inner build script will draw from, avoiding mismatches
# where a freshly published base image has newer preinstalled packages than
# the snapshot archive can satisfy.
#
# Each Dockerfile in the tree is expected to have a single `FROM` line of
# the form:
#   FROM <image>:<tag>@sha256:<digest>
# The tag stays as the major-version label (ubuntu:22.04, debian:12, ...);
# only the digest is rewritten, to point at a date-stamped variant (e.g.
# ubuntu:jammy-20260410) whose build date is <= APT_SNAPSHOT_DATE.
#
# The effective snapshot date is determined by:
#   1. $APT_SNAPSHOT_DATE from the environment (if set, including empty).
#   2. DefaultAptSnapshotDate extracted from BuildLinuxSDKContainers.sh.
# So updating the date in the driver automatically propagates here.
#
# Requires:
#   - Docker with buildx (for `docker buildx imagetools inspect`).
#   - Network access to hub.docker.com for tag listing.
#   - python3 for JSON/HTTP handling.

set -eo pipefail

ScriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MalterlibRoot="$(cd "$ScriptDir/../../.." && pwd)"
DockerRoot="$MalterlibRoot/Malterlib/Tool/Docker/LinuxSDK"
DriverScript="$ScriptDir/BuildLinuxSDKContainers.sh"

if [ ! -d "$DockerRoot" ]; then
	echo "ERROR: Docker root not found at $DockerRoot" >&2
	exit 1
fi
if [ ! -f "$DriverScript" ]; then
	echo "ERROR: Driver script not found at $DriverScript" >&2
	exit 1
fi

# Extract DefaultAptSnapshotDate from the driver so the two files can't
# drift. Accept env override (including explicit empty).
DefaultAptSnapshotDate=$(
	grep -E '^DefaultAptSnapshotDate="[^"]*"' "$DriverScript" \
		| head -n1 \
		| sed -E 's/^DefaultAptSnapshotDate="([^"]*)"$/\1/'
)
if [ -z "$DefaultAptSnapshotDate" ]; then
	echo "ERROR: Could not extract DefaultAptSnapshotDate from $DriverScript" >&2
	exit 1
fi
SnapshotDate="${APT_SNAPSHOT_DATE-$DefaultAptSnapshotDate}"

# Validate the full YYYYMMDDTHHMMSSZ form. The time-of-day component is
# significant: tag selection treats a midnight snapshot more strictly
# (excludes same-day tags) than a non-midnight one, since a same-day base
# image built later than midnight would carry packages that don't yet
# exist in the snapshot's apt indexes.
if [ -n "$SnapshotDate" ]; then
	if ! [[ "$SnapshotDate" =~ ^[0-9]{8}T[0-9]{6}Z$ ]]; then
		echo "ERROR: APT_SNAPSHOT_DATE=$SnapshotDate is not in YYYYMMDDTHHMMSSZ form" >&2
		exit 1
	fi
fi

if docker info >/dev/null 2>&1; then
	DockerPrefix=()
else
	if ! command -v sudo >/dev/null 2>&1; then
		echo "ERROR: Docker daemon is not accessible and sudo is not available." >&2
		exit 1
	fi
	DockerPrefix=(sudo)
fi

docker_cmd() {
	"${DockerPrefix[@]}" docker "$@"
}

# Map <image>:<tag> to the codename used by date-stamped tags on the base
# image's Docker Hub repo. Extend as new distros are added.
image_to_codename() {
	case "$1" in
		ubuntu:22.04) echo jammy ;;
		ubuntu:24.04) echo noble ;;
		debian:12)    echo bookworm ;;
		debian:13)    echo trixie ;;
		*)
			return 1
			;;
	esac
}

# Find the latest date-stamped tag on Docker Hub for <repo>/<codename>
# eligible for the apt snapshot moment. The registry's tag_last_pushed is
# intentionally NOT used as a cutoff: Docker Hub republishes (e.g.
# multi-arch manifest refresh) move that timestamp without changing the
# underlying YYYYMMDD-build content, so push-time filtering would
# unfairly skip correct tags.
#
# Eligibility rule:
#   * Tags with YYYYMMDD <  snapshot day are always eligible.
#   * Tags with YYYYMMDD == snapshot day are eligible iff the snapshot
#     time-of-day is non-zero. A midnight snapshot (T000000Z) precedes
#     any same-day build, so a same-day base image would carry packages
#     that don't yet exist in the snapshot's apt indexes; non-midnight
#     snapshots can't be verified against a YYYYMMDD-only tag, so we
#     trust the operator's chosen timestamp rather than reject the only
#     candidate available for that day.
pick_tag_for_snapshot() {
	local Repo="$1"         # e.g. library/ubuntu
	local Codename="$2"     # e.g. jammy
	local SnapshotDate="$3" # YYYYMMDDTHHMMSSZ
	python3 - "$Repo" "$Codename" "$SnapshotDate" <<'PY'
import sys, json, re, urllib.request
repo, codename, snapshot_date = sys.argv[1], sys.argv[2], sys.argv[3]
m = re.fullmatch(r'(\d{8})T(\d{6})Z', snapshot_date)
if not m:
	sys.stderr.write(f"Invalid snapshot date: {snapshot_date} (expected YYYYMMDDTHHMMSSZ)\n")
	sys.exit(1)
snapshot_ymd = m.group(1)
exclude_same_day = m.group(2) == '000000'
pattern = re.compile(rf'^{re.escape(codename)}-(\d{{8}})(?:\.(\d+))?$')
url = f"https://hub.docker.com/v2/repositories/{repo}/tags?name={codename}-&page_size=100"
best = None  # (ymd, suffix, name)
while url:
	with urllib.request.urlopen(url, timeout=30) as f:
		data = json.load(f)
	for r in data.get("results", []):
		name = r.get("name", "")
		m = pattern.match(name)
		if not m:
			continue
		ymd = m.group(1)
		suffix = int(m.group(2) or 0)
		if ymd > snapshot_ymd:
			continue
		if exclude_same_day and ymd == snapshot_ymd:
			continue
		key = (ymd, suffix)
		if best is None or key > best[:2]:
			best = (ymd, suffix, name)
	url = data.get("next")
if best is None:
	bound = "<" if exclude_same_day else "<="
	sys.stderr.write(
		f"No {codename}-YYYYMMDD tag on Docker Hub with date {bound} {snapshot_ymd}\n"
	)
	sys.exit(1)
print(best[2])
PY
}

# Resolve the multi-arch index digest for a tag. We pick the top-level index
# digest rather than a per-platform manifest so a single pin covers every
# architecture built from that Dockerfile.
resolve_index_digest() {
	local Reference="$1"
	local Output
	if ! Output=$(docker_cmd buildx imagetools inspect "$Reference" 2>/dev/null); then
		return 1
	fi
	awk '
		/^Digest:[[:space:]]+sha256:/ {
			print $2
			exit
		}
	' <<< "$Output"
}

if [ -z "$SnapshotDate" ]; then
	echo "APT_SNAPSHOT_DATE is empty; resolving current digests for the plain <image>:<tag> refs."
else
	echo "Snapshot date: $SnapshotDate (eligible base-image tags resolved per snapshot moment)."
fi

UpdatedAny=0
while IFS= read -r -d '' Dockerfile; do
	FromLine=$(grep -E '^FROM[[:space:]]+[^[:space:]]+:[^[:space:]@]+@sha256:[0-9a-f]{64}' "$Dockerfile" || true)
	if [ -z "$FromLine" ]; then
		echo "Skipping $Dockerfile: no pinned FROM line found." >&2
		continue
	fi

	ImageAndTag=$(sed -E 's/^FROM[[:space:]]+([^[:space:]]+:[^[:space:]@]+)@sha256:[0-9a-f]{64}.*$/\1/' <<< "$FromLine")
	OldDigest=$(sed -E 's/^FROM[[:space:]]+[^[:space:]]+:[^[:space:]@]+@(sha256:[0-9a-f]{64}).*$/\1/' <<< "$FromLine")
	ImagePart="${ImageAndTag%%:*}"

	echo "[$Dockerfile]"
	echo "  image:    $ImageAndTag"
	echo "  current:  $OldDigest"

	Reference="$ImageAndTag"
	if [ -n "$SnapshotDate" ]; then
		Codename=$(image_to_codename "$ImageAndTag") || {
			echo "  ERROR: no codename mapping for $ImageAndTag; add it to image_to_codename()" >&2
			exit 1
		}
		RepoPath="library/$ImagePart"
		PickedTag=$(pick_tag_for_snapshot "$RepoPath" "$Codename" "$SnapshotDate") || exit 1
		echo "  picked:   $ImagePart:$PickedTag"
		Reference="$ImagePart:$PickedTag"
	fi

	if ! NewDigest=$(resolve_index_digest "$Reference"); then
		echo "  ERROR: failed to resolve digest for $Reference" >&2
		exit 1
	fi
	if [ -z "$NewDigest" ]; then
		echo "  ERROR: could not parse digest from imagetools output for $Reference" >&2
		exit 1
	fi

	echo "  new:      $NewDigest"

	if [ "$NewDigest" = "$OldDigest" ]; then
		echo "  -> already up to date"
		continue
	fi

	# In-place rewrite of the digest. Anchor on the old digest so we only
	# touch the single FROM line even if the Dockerfile mentions sha256:
	# elsewhere (e.g. in comments or copy-pasted snippets).
	python3 - "$Dockerfile" "$OldDigest" "$NewDigest" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "r", encoding="utf-8") as f:
	data = f.read()
if old not in data:
	sys.stderr.write(f"ERROR: {old} not present in {path}\n")
	sys.exit(1)
# Replace only the first occurrence so comment-pasted copies are untouched.
data = data.replace(old, new, 1)
with open(path, "w", encoding="utf-8") as f:
	f.write(data)
PY

	echo "  -> updated"
	UpdatedAny=1
done < <(find "$DockerRoot" -type f -name Dockerfile -print0)

if [ "$UpdatedAny" -eq 0 ]; then
	echo
	echo "All base-image pins are already up to date."
else
	echo
	echo "Base-image pins updated. Review the diff and commit the changes."
fi
