#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Builds deterministic Malterlib Linux SDKs (x64, arm64, x86) using Docker.
#
# Each architecture is built inside a pinned Docker image and the results
# are written into the per-architecture
# Binaries/MalterlibSDK/Linux/<Distro>/<Arch> repository checkouts. Source
# packages are shared across all three builds and written into
# Binaries/MalterlibSDK/Linux/DebianBased/Source.
#
# Requires:
#   - Docker with buildx, accessed either via membership in the `docker`
#     group OR via passwordless sudo (auto-detected: docker_cmd transparently
#     prefixes `sudo` when the user isn't in the group). The script itself
#     runs as the invoking user; output files in the bind mounts come out
#     owned by that user.
#   - For cross-arch builds: a Docker daemon that allows
#     `docker run --privileged` (used by ensure_binfmt to register QEMU
#     handlers via tonistiigi/binfmt). Rootless Docker and Docker Desktop /
#     CI configurations that forbid `--privileged` are NOT supported for
#     cross-arch builds. Native-only invocations
#     (e.g. `Ubuntu:amd64` on an amd64 host, `Debian:386` on amd64 via the
#     IA32 compat path) skip ensure_binfmt and work without privileged.
#   - Kernel-level binfmt_misc (installed on first cross-arch run via
#     tonistiigi/binfmt; not needed for native-only invocations)
#
# Usage:
#   BuildLinuxSDKContainers.sh [distro_platform ...]
#
# Without arguments, all configured SDKs are built. Pass one or more tokens
# ("Ubuntu:amd64", "Debian:386", ...) to build a subset.

set -eo pipefail

ScriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Resolve this driver script's absolute path. ${BASH_SOURCE[0]} can be relative
# when the script is launched via a relative path (e.g. ./BuildLinuxSDKContainers.sh),
# and Docker -v does not accept a relative bind-mount source.
DriverScriptPath="$ScriptDir/$(basename "${BASH_SOURCE[0]}")"
BuildScriptPath="$ScriptDir/BuildLinuxSDK.sh"

# Locate the per-distro Dockerfile tree and output root. Two layouts are
# supported:
#   1. In-repo developer checkout:
#        Malterlib/Tool/Scripts/BuildLinuxSDKContainers.sh  (this file)
#        Malterlib/Tool/Docker/LinuxSDK/<Distro>/Dockerfile
#        Binaries/MalterlibSDK/Linux/...                    (output)
#   2. Shipped SDK bundle:
#        SourceMetadata/BuildLinuxSDKContainers.sh          (this file)
#        SourceMetadata/Docker/<Distro>/Dockerfile
#        <user-chosen output directory>
# Prefer the adjacent SourceMetadata layout so that a recipient running the
# bundled harness from a standalone SDK distribution picks up the Dockerfiles
# packaged next to this script rather than the non-existent repo path.
if [ -d "$ScriptDir/Docker" ]; then
	DockerRoot="$ScriptDir/Docker"
	# Standalone bundle: default the output under the script directory so a
	# blind invocation produces something inspectable instead of writing into
	# a synthesised repo path. Override by editing BinariesRoot here.
	BinariesRoot="${BINARIES_ROOT:-$ScriptDir/RebuiltSDK}"
else
	MalterlibRoot="$(cd "$ScriptDir/../../.." 2>/dev/null && pwd || true)"
	if [ -n "$MalterlibRoot" ] && [ -d "$MalterlibRoot/Malterlib/Tool/Docker/LinuxSDK" ]; then
		DockerRoot="$MalterlibRoot/Malterlib/Tool/Docker/LinuxSDK"
		BinariesRoot="${BINARIES_ROOT:-$MalterlibRoot/Binaries/MalterlibSDK/Linux}"
	else
		echo "ERROR: Could not locate the Docker/<Distro> tree alongside this script or at the" >&2
		echo "       expected repo-relative path. Run this script from its original location" >&2
		echo "       or adjust DockerRoot at the top of the file." >&2
		exit 1
	fi
fi

# Apt snapshot date used for reproducibility. Hardcoded on purpose so the
# committed scripts alone describe a reproducible build against a fixed set
# of Ubuntu / Debian package indexes. Override by exporting
# APT_SNAPSHOT_DATE=<YYYYMMDDTHHMMSSZ> before invoking the driver, or set
# APT_SNAPSHOT_DATE="" (empty string) to disable snapshot pinning entirely.
#
# Inside each container:
#   * Ubuntu builds inject a per-line [snapshot=<date>] option on each deb /
#     deb-src entry (the syntax Canonical documents at
#     https://snapshot.ubuntu.com for "Ubuntu 23.10 and earlier"). apt
#     redirects the fetches to snapshot.ubuntu.com/ubuntu/<date>/ for both
#     primary (amd64, i386) and ports (arm64, etc.) architectures. Publicly
#     accessible; no authentication needed.
#   * Debian builds rewrite sources to snapshot.debian.org/archive/debian/,
#     which is likewise publicly accessible.
DefaultAptSnapshotDate="20260422T000000Z"
APT_SNAPSHOT_DATE="${APT_SNAPSHOT_DATE-$DefaultAptSnapshotDate}"

# Optional HTTP proxy for apt. When set, forwarded to every build container
# and wired into /etc/apt/apt.conf.d/ so all apt fetches go through the
# proxy. Example:
#   APT_PROXY=http://host.docker.internal:3128
# Default is unset, which means direct internet fetches.
APT_PROXY="${APT_PROXY:-}"

# Optional CA certificate for a TLS-MITM caching proxy (e.g. squid with
# ssl_bump, provisioned by Scripts/SetupLinuxSDKAptCache.sh). When set, the
# file is bind-mounted into every container at /etc/malterlib-apt-ca.crt and
# BuildLinuxSDK.sh installs it into the container's system trust store so apt
# accepts the proxy's per-domain certificates. Required when APT_PROXY points
# at a TLS-MITM proxy; not needed for plain HTTP proxies.
APT_PROXY_CA="${APT_PROXY_CA:-}"
if [ -n "$APT_PROXY_CA" ] && [ ! -f "$APT_PROXY_CA" ]; then
	echo "ERROR: APT_PROXY_CA points at non-existent file: $APT_PROXY_CA" >&2
	exit 1
fi

# Source repository URL and branch embedded in the SDK source-link manifests.
# REQUIRED: URL must be a non-empty git URL pointing at a GitHub repository.
# This script is internal to Malterlib and only knows how to translate
# GitHub clone URLs into browseable blob/tree URLs (see
# build_source_repo_url in BuildLinuxSDK.sh). Other forges (GitLab,
# BitBucket, etc.) are not supported. Empty overrides are silently restored
# to the defaults below.
SourcePackagesURL="${SOURCE_PACKAGES_URL:-https://github.com/Malterlib/MalterlibBinariesSDK_Linux_DebianBased_Source_rLFS.git}"
SourcePackagesBranch="${SOURCE_PACKAGES_BRANCH:-master}"

# LLVM libc++ major version installed in each SDK. Override per architecture
# below if a given distro/arch does not publish that version.
DefaultLibcxxDevVersion=20

# Configured distro builds: "<Dockerfile subdir>:<docker platform>:<dest subdir under $BinariesRoot>[:<libc++ version>]"
# Extend this list to support additional distros/arches. Each Dockerfile lives
# at $DockerRoot/<Dockerfile subdir>/Dockerfile.
Distros=(
	"Ubuntu:amd64:Ubuntu/x64"
	"Ubuntu:arm64:Ubuntu/arm64"
	"Debian:386:Debian/x86"
)

SourceDir="$BinariesRoot/DebianBased/Source"
BuilderName="malterlib-sdk-builder"

# Docker access check: if the invoking user is in the docker group we run
# docker commands directly; otherwise we prepend sudo only to docker calls so
# the rest of the script (file creation, etc.) stays unprivileged.
if docker info >/dev/null 2>&1; then
	DockerPrefix=()
else
	if ! command -v sudo >/dev/null 2>&1; then
		echo "ERROR: Docker daemon is not accessible and sudo is not available." >&2
		echo "Add your user to the docker group or install sudo." >&2
		exit 1
	fi
	echo "Docker requires elevation; docker commands will be prefixed with sudo."
	DockerPrefix=(sudo)
fi

docker_cmd() {
	"${DockerPrefix[@]}" docker "$@"
}

# Require Docker >= 20.10. The driver passes
# `--add-host=host.docker.internal:host-gateway` to every container so the
# inner build script can reach an APT_PROXY running on the host. The
# `host-gateway` magic value was introduced in Docker 20.10; older daemons
# parse it as a literal IP, fail to validate, and refuse to start the
# container -- making this a hard prerequisite rather than a best-effort
# convenience. The comparison is pure-bash major.minor parsing rather
# than `dpkg --compare-versions` so this script works on Fedora/Arch/
# macOS hosts where dpkg isn't available.
DockerServerVersion=$(docker_cmd version --format '{{.Server.Version}}' 2>/dev/null || true)
if [ -z "$DockerServerVersion" ]; then
	echo "ERROR: Could not read Docker server version via 'docker version'." >&2
	exit 1
fi
if ! [[ "$DockerServerVersion" =~ ^([0-9]+)\.([0-9]+) ]]; then
	echo "ERROR: Could not parse Docker server version '$DockerServerVersion'." >&2
	exit 1
fi
DockerMajor="${BASH_REMATCH[1]}"
DockerMinor="${BASH_REMATCH[2]}"
if [ "$DockerMajor" -lt 20 ] \
	|| { [ "$DockerMajor" -eq 20 ] && [ "$DockerMinor" -lt 10 ]; }; then
	echo "ERROR: Docker $DockerServerVersion is too old; need >= 20.10 for" >&2
	echo "       --add-host=host.docker.internal:host-gateway support." >&2
	echo "       Upgrade Docker before re-running the driver." >&2
	exit 1
fi

# UID/GID/username that the in-container build user will run as. The
# script itself always runs as the invoking user (only docker commands
# get sudo-prefixed via docker_cmd, when the user isn't in the docker
# group), so id -u is always the user we want files in the bind mounts
# to be owned by. The destination tree is part of that user's git
# repository -- we never chown anything, since a chown would mutate
# files the user already owns and tracks in git.
BuildUid=$(id -u)
BuildGid=$(id -g)
if [ "$BuildUid" = 0 ]; then
	BuildUser=root
else
	BuildUser=builder
fi

ensure_binfmt() {
	# Register QEMU binfmt handlers on every run. Kernel binfmt_misc
	# registrations don't persist across reboots or Docker daemon restarts,
	# and a missing handler shows up only at `docker run` time as
	# "exec /usr/bin/bash: exec format error" once a cross-arch build has
	# started -- too late and confusing. tonistiigi/binfmt --install is
	# idempotent and fast when the handlers are already present.
	docker_cmd run --privileged --rm tonistiigi/binfmt:latest --install all >/dev/null

	if ! docker_cmd buildx inspect "$BuilderName" >/dev/null 2>&1; then
		echo "Creating buildx builder $BuilderName..."
		docker_cmd buildx create --name "$BuilderName" --use >/dev/null
	fi
	docker_cmd buildx use "$BuilderName" >/dev/null
}

image_tag() {
	local DistroDir="$1"
	local Platform="$2"
	local Lower
	Lower=$(echo "$DistroDir" | tr '[:upper:]' '[:lower:]')
	echo "malterlib-sdk-${Lower}-${Platform}:latest"
}

build_image() {
	local DistroDir="$1"
	local Platform="$2"
	local Tag
	Tag=$(image_tag "$DistroDir" "$Platform")

	echo "[$DistroDir/$Platform] Building builder image $Tag..."
	docker_cmd buildx build \
		--platform "linux/$Platform" \
		--load \
		--build-arg "BUILD_UID=$BuildUid" \
		--build-arg "BUILD_GID=$BuildGid" \
		--build-arg "BUILD_USER=$BuildUser" \
		-t "$Tag" \
		"$DockerRoot/$DistroDir"
}

run_build() {
	local DistroDir="$1"
	local Platform="$2"
	local DestSubdir="$3"
	local LibcxxVersion="$4"
	local Tag
	Tag=$(image_tag "$DistroDir" "$Platform")

	local DestDir="$BinariesRoot/$DestSubdir"
	mkdir -p "$DestDir" "$SourceDir"

	echo
	echo "=============================================="
	echo "[$DistroDir/$Platform] Building SDK -> $DestDir"
	echo "=============================================="

	local CaMount=()
	if [ -n "$APT_PROXY_CA" ]; then
		CaMount=(-v "$APT_PROXY_CA:/etc/malterlib-apt-ca.crt:ro")
	fi

	docker_cmd run --rm \
		--platform "linux/$Platform" \
		--add-host=host.docker.internal:host-gateway \
		-v "$BuildScriptPath:/work/BuildLinuxSDK.sh:ro" \
		-v "$DriverScriptPath:/work/BuildLinuxSDKContainers.sh:ro" \
		-v "$DockerRoot:/work/Docker:ro" \
		-v "$DestDir:/output/sdk" \
		-v "$SourceDir:/output/sources" \
		"${CaMount[@]}" \
		-e "APT_SNAPSHOT_DATE=$APT_SNAPSHOT_DATE" \
		-e "APT_PROXY=$APT_PROXY" \
		-e "LIBCXX_DEV_VERSION=$LibcxxVersion" \
		-e "DRIVER_SCRIPT_PATH=/work/BuildLinuxSDKContainers.sh" \
		-e "DOCKER_ROOT=/work/Docker" \
		"$Tag" \
		bash /work/BuildLinuxSDK.sh /output/sdk /output/sources "$SourcePackagesURL" "$SourcePackagesBranch"
}

matches_filter() {
	local Token="$1"
	shift
	[ $# -eq 0 ] && return 0
	local Filter
	for Filter in "$@"; do
		[ "$Token" = "$Filter" ] && return 0
	done
	return 1
}

# Map uname -m to the docker platform string used in the Distros table so we
# can run the native arch first -- its build downloads the bulk of the shared
# source packages at native speed, and the remaining emulated builds then
# reuse those downloads instead of re-fetching them under QEMU.
detect_native_platform() {
	case "$(uname -m)" in
		aarch64|arm64) echo "arm64" ;;
		x86_64|amd64) echo "amd64" ;;
		i[3-6]86|i386) echo "386" ;;
		*) echo "" ;;
	esac
}

NativePlatform=$(detect_native_platform)

# A configured target runs natively (no QEMU / binfmt required) when
# either:
#   * Its platform string matches $NativePlatform exactly, or
#   * It's a linux/386 target on an amd64 host -- same x86 ISA, the
#     kernel's IA32 compat path runs 32-bit userland without any
#     emulation.
is_native_platform() {
	local P="$1"
	[ -z "$NativePlatform" ] && return 1
	[ "$P" = "$NativePlatform" ] && return 0
	[ "$NativePlatform" = "amd64" ] && [ "$P" = "386" ] && return 0
	return 1
}

NativeRows=()
OtherRows=()
for Row in "${Distros[@]}"; do
	IFS=':' read -r _ RowPlatform _ _ <<< "$Row"
	if [ -n "$NativePlatform" ] && [ "$RowPlatform" = "$NativePlatform" ]; then
		NativeRows+=("$Row")
	else
		OtherRows+=("$Row")
	fi
done
OrderedDistros=("${NativeRows[@]}" "${OtherRows[@]}")

# ensure_binfmt registers QEMU handlers via a privileged container, which
# is unavailable (and unnecessary) on rootless Docker / restricted CI.
# Skip it unless the filtered target list actually contains a platform
# that would need emulation (see is_native_platform -- linux/386 on an
# amd64 host runs natively via IA32 compat and does not count).
NeedsBinfmt=0
for Row in "${OrderedDistros[@]}"; do
	IFS=':' read -r DistroDir Platform _ _ <<< "$Row"
	if ! matches_filter "$DistroDir:$Platform" "$@"; then
		continue
	fi
	if ! is_native_platform "$Platform"; then
		NeedsBinfmt=1
		break
	fi
done
if [ "$NeedsBinfmt" -eq 1 ]; then
	ensure_binfmt
fi

BuildsRun=0
for Row in "${OrderedDistros[@]}"; do
	IFS=':' read -r DistroDir Platform DestSubdir LibcxxVersion <<< "$Row"
	LibcxxVersion="${LibcxxVersion:-$DefaultLibcxxDevVersion}"
	if ! matches_filter "$DistroDir:$Platform" "$@"; then
		continue
	fi
	build_image "$DistroDir" "$Platform"
	run_build "$DistroDir" "$Platform" "$DestSubdir" "$LibcxxVersion"
	BuildsRun=$((BuildsRun + 1))
done

# Fail loudly when target filters were given but matched nothing -- a
# typo like `Ubuntu:x64` (vs `Ubuntu:amd64`) would otherwise silently
# build zero SDKs and report success. With no filter args the loop is
# guaranteed to run every configured distro, so BuildsRun > 0 there.
if [ "$BuildsRun" -eq 0 ] && [ "$#" -gt 0 ]; then
	echo "ERROR: None of '$*' matched any configured SDK target." >&2
	echo "       Available targets:" >&2
	for Row in "${Distros[@]}"; do
		IFS=':' read -r DistroDir Platform _ _ <<< "$Row"
		echo "         $DistroDir:$Platform" >&2
	done
	exit 1
fi

echo
echo "All selected SDKs built successfully."
