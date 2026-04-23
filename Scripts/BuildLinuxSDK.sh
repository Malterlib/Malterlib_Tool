#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -eo pipefail

DestinationDir=${1:-/LocalSource/SDKs}
SourcePackagesDir=${2:-"$DestinationDir/Linux.sdk-sources"}
SourcePackagesURLBase=${3:-https://github.com/Malterlib/MalterlibBinariesSDK_Linux_DebianBased_Source_rLFS.git}
SourcePackagesBranch=${4:-${SOURCE_PACKAGES_BRANCH:-master}}
GeneratorScriptPath="$(realpath "${BASH_SOURCE[0]}")"
Architecture=`uname -m`
LibcxxDevVersion="${LIBCXX_DEV_VERSION:-20}"

if [[ "$Architecture" == "i686" ]] || [[ "$Architecture" == "i586" ]] || [[ "$Architecture" == "i486" ]] || [[ "$Architecture" == "i386" ]] ; then
	Architecture=i386
elif [[ "$Architecture" == "x86_64" ]]; then
	if [[ `getconf LONG_BIT` == "32" ]] ; then
		Architecture=i386
	fi
fi

export MalterlibImportUpdateCache=false

echo "DestinationDir: $DestinationDir"
echo "SourcePackagesDir: $SourcePackagesDir"
if [ -n "$SourcePackagesURLBase" ]; then
	echo "SourcePackagesURLBase: $SourcePackagesURLBase"
	echo "SourcePackagesBranch: $SourcePackagesBranch"
fi
echo "Architecture: $Architecture"

# If the driver bind-mounted a TLS-MITM proxy CA, install it into the system
# trust store BEFORE any apt call so HTTPS fetches through the proxy validate
# correctly. The mount point is /etc/malterlib-apt-ca.crt; the canonical
# install location for locally-trusted CAs on Debian/Ubuntu is
# /usr/local/share/ca-certificates/. update-ca-certificates regenerates the
# bundles apt uses.
if [ -f /etc/malterlib-apt-ca.crt ]; then
	echo "Installing APT_PROXY_CA into system trust store..."
	sudo install -m 0644 /etc/malterlib-apt-ca.crt \
		/usr/local/share/ca-certificates/malterlib-apt-cache.crt
	sudo update-ca-certificates >/dev/null
fi

# If an apt proxy was provided, wire it in before any apt-get invocation so
# bootstrap fetches hit the cache too. Harmless when unset.
if [ -n "${APT_PROXY:-}" ]; then
	echo "Routing apt through proxy: $APT_PROXY"
	printf 'Acquire::http::Proxy "%s";\nAcquire::https::Proxy "%s";\n' \
		"$APT_PROXY" "$APT_PROXY" \
		| sudo tee /etc/apt/apt.conf.d/50-malterlib-proxy > /dev/null
fi

# Prerequisites for repo management (keyring tooling + codename detection).
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
	ca-certificates gnupg lsb-release wget binutils

apply_apt_snapshot() {
	local Date="$1"
	[ -n "$Date" ] || return 0

	local DistroID
	DistroID=$(lsb_release -is 2>/dev/null || echo unknown)

	echo "Pinning apt to snapshot date: $Date ($DistroID)"

	case "$DistroID" in
		Ubuntu)
			# Inject snapshot pinning into every apt source. See
			# https://snapshot.ubuntu.com for Canonical's docs.
			#
			# Keeps sources pointing at the standard archive.ubuntu.com /
			# security.ubuntu.com / ports.ubuntu.com URLs and lets apt itself
			# resolve historical indexes. Works for both primary (amd64, i386)
			# and ports (arm64, etc.) architectures without any form of
			# authentication.
			#
			# Two source formats need handling:
			#   * Classic /etc/apt/sources.list{,.d/*.list} (Ubuntu 22.04):
			#     per-line "[snapshot=<date>]" option on each deb / deb-src
			#     entry. The global APT::Snapshot apt.conf option is
			#     advertised but NOT honored at fetch time for ports /
			#     -updates / -security / -backports on jammy apt 2.4.14, so
			#     the per-line form is required there.
			#   * DEB822 /etc/apt/sources.list.d/*.sources (Ubuntu 24.04+):
			#     append "Snapshot: <date>" field to each record.
			#
			# Both formats are handled regardless of distro version so the
			# script works whether the container happens to have one, the
			# other, or both.
			local AptVersion
			AptVersion=$(dpkg-query -W -f='${Version}' apt 2>/dev/null || echo 0)
			if ! dpkg --compare-versions "$AptVersion" ge 2.4.11; then
				echo "ERROR: apt $AptVersion lacks snapshot support." >&2
				echo "       Expected apt >= 2.4.11. Refresh the base image pin via" >&2
				echo "       Scripts/UpdateLinuxSDKBaseImages.sh, or unset APT_SNAPSHOT_DATE" >&2
				echo "       to disable snapshot pinning." >&2
				exit 1
			fi

			# Classic format: inject [snapshot=<date>] into each deb / deb-src
			# line. Two cases handled per line:
			#   1. Existing options:  "deb [arch=amd64] http://..."
			#      -> append "snapshot=..." inside the same bracket block.
			#   2. No options:        "deb http://..."
			#      -> insert a fresh "[snapshot=...]" bracket block.
			# Pattern #1 runs first so pattern #2 doesn't re-match rewritten
			# lines. Third-party sources (LLVM etc.) aren't added yet at this
			# point in the script, so no filtering is needed.
			local SourceFile
			for SourceFile in /etc/apt/sources.list /etc/apt/sources.list.d/*.list; do
				[ -f "$SourceFile" ] || continue
				sudo sed -i -E \
					-e "s/^(deb(-src)?[[:space:]]+\[)([^]]*)\][[:space:]]+/\1\3 snapshot=$Date] /" \
					-e "s/^(deb(-src)?[[:space:]]+)(http)/\1[snapshot=$Date] \3/" \
					"$SourceFile"
			done

			# DEB822 format: append "Snapshot: <date>" to each record that
			# has a URIs: field and doesn't already declare Snapshot:.
			# Records are separated by blank lines.
			for SourceFile in /etc/apt/sources.list.d/*.sources; do
				[ -f "$SourceFile" ] || continue
				local Tmp
				Tmp=$(mktemp)
				awk -v date="$Date" '
					function flush() {
						if (record != "") {
							if (has_uris && !has_snapshot) {
								sub(/\n$/, "", record)
								record = record "\nSnapshot: " date "\n"
							}
							printf "%s", record
						}
						record = ""
						has_uris = 0
						has_snapshot = 0
					}
					/^[[:space:]]*$/ { flush(); print; next }
					/^URIs:/ { has_uris = 1 }
					/^Snapshot:/ { has_snapshot = 1 }
					{ record = record $0 "\n" }
					END { flush() }
				' "$SourceFile" > "$Tmp"
				sudo install -m 644 "$Tmp" "$SourceFile"
				rm -f "$Tmp"
			done

			# Clean up any apt.conf.d file this script may have written in a
			# previous iteration using the APT::Snapshot global option.
			sudo rm -f /etc/apt/apt.conf.d/50-malterlib-snapshot
			;;
		Debian)
			# Debian's snapshot service uses URL rewriting to snapshot.debian.org
			# and is publicly accessible without authentication.
			local SnapshotURL="http://snapshot.debian.org/archive/debian/$Date"
			local SnapshotSecurityURL="http://snapshot.debian.org/archive/debian-security/$Date"
			local SourceFile
			for SourceFile in /etc/apt/sources.list.d/debian.sources /etc/apt/sources.list; do
				[ -f "$SourceFile" ] || continue
				sudo sed -i -E "s#http[s]?://[a-zA-Z0-9.-]+/debian-security#$SnapshotSecurityURL#g" "$SourceFile"
				# The (/|[[:space:]]|\$) alternation matches a `/`, whitespace, OR
				# end-of-line after `/debian` so the main archive URI is rewritten
				# whether it ends at EOL (DEB822 `URIs:` form) or is followed by
				# distribution/component fields (classic `deb` form).
				#
				# IMPORTANT when re-testing this regex by hand: the `\$` is a
				# bash-double-quote escape, so sed actually receives `$` (the
				# end-of-line anchor). Reproducing the test through nested shell
				# layers (e.g. `bash -lc "...sed -E '...'..."`) can quietly mangle
				# the quoting and turn the anchor into a literal `$`, making the
				# regex appear broken when it isn't. Run the substitution against a
				# real /etc/apt/sources.list.d/debian.sources to verify.
				sudo sed -i -E "s#http[s]?://deb\.debian\.org/debian(/|[[:space:]]|\$)#$SnapshotURL\1#g" "$SourceFile"
			done
			;;
		*)
			echo "WARNING: APT_SNAPSHOT_DATE is set but distro '$DistroID' has no snapshot mapping" >&2
			return 0
			;;
	esac

	# Snapshot archives ship frozen Release files that trip the Valid-Until check.
	echo 'Acquire::Check-Valid-Until "false";' | sudo tee /etc/apt/apt.conf.d/99-malterlib-snapshot > /dev/null
}

verify_apt_snapshot() {
	local ExpectedDate="$1"
	[ -n "$ExpectedDate" ] || return 0

	# Canonicalise the hardcoded compact form (e.g. 20260415T000000Z) into the
	# ISO-8601 form GNU date -d accepts, then resolve to epoch seconds.
	local Iso
	Iso=$(sed -E 's/^([0-9]{4})([0-9]{2})([0-9]{2})T([0-9]{2})([0-9]{2})([0-9]{2})Z$/\1-\2-\3T\4:\5:\6Z/' <<< "$ExpectedDate")
	local ExpectedEpoch
	ExpectedEpoch=$(date -u -d "$Iso" +%s 2>/dev/null || echo 0)
	if [ "$ExpectedEpoch" -eq 0 ]; then
		echo "ERROR: Cannot parse APT_SNAPSHOT_DATE=$ExpectedDate (expected YYYYMMDDTHHMMSSZ)." >&2
		exit 1
	fi

	echo
	echo "Snapshot pinning verification:"
	echo "  APT_SNAPSHOT_DATE: $ExpectedDate"

	# Only examine Release files that apt actually fetched from a snapshot
	# service. The live-archive files (archive/security/ports.ubuntu.com_*)
	# may linger in the cache from the bootstrap apt-get update that ran
	# before snapshot pinning was configured; they don't reflect what apt
	# would fetch now, so skip them. If no snapshot-served Release files
	# exist at all, apt never hit the snapshot service -- that's a failure.
	local AnyFailed=0 AnyChecked=0
	local ReleaseFile
	for ReleaseFile in /var/lib/apt/lists/*_InRelease /var/lib/apt/lists/*_Release; do
		[ -f "$ReleaseFile" ] || continue
		local Base
		Base=$(basename "$ReleaseFile")
		case "$Base" in
			snapshot.ubuntu.com_* | snapshot.debian.org_*)
				;;
			*)
				continue
				;;
		esac

		local DateLine
		DateLine=$(grep -m1 '^Date:' "$ReleaseFile" 2>/dev/null | sed 's/^Date:[[:space:]]*//')
		[ -n "$DateLine" ] || continue

		local ReleaseEpoch
		ReleaseEpoch=$(date -u -d "$DateLine" +%s 2>/dev/null || echo 0)
		[ "$ReleaseEpoch" -gt 0 ] || continue

		AnyChecked=$((AnyChecked + 1))
		if [ "$ReleaseEpoch" -gt "$ExpectedEpoch" ]; then
			printf '  FAIL %s: %s (newer than snapshot date)\n' "$Base" "$DateLine"
			AnyFailed=1
		else
			printf '  OK   %s: %s\n' "$Base" "$DateLine"
		fi
	done

	if [ "$AnyChecked" -eq 0 ]; then
		echo "ERROR: no snapshot-served Release files found in /var/lib/apt/lists/." >&2
		echo "       apt did not redirect to the snapshot service; pinning is inactive." >&2
		echo "       Investigate before continuing; rebuilds will not be reproducible." >&2
		exit 1
	fi

	if [ "$AnyFailed" -ne 0 ]; then
		echo "ERROR: at least one Release file is newer than APT_SNAPSHOT_DATE=$ExpectedDate." >&2
		echo "       Snapshot pinning is NOT in effect -- apt is serving live indexes." >&2
		echo "       Investigate before continuing; rebuilds will not be reproducible." >&2
		exit 1
	fi
}

# Enable deb-src BEFORE apply_apt_snapshot so the single snapshot-injection
# sweep covers both binary and source index lines. apt-get build-dep later in
# the script relies on the deb-src indexes to resolve source-package versions;
# if those lines were enabled afterwards they would be fetched live and break
# reproducibility of the source manifests.
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
	if ! grep -q 'Types:.*deb-src' /etc/apt/sources.list.d/ubuntu.sources; then
		sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources
	fi
elif [ -f /etc/apt/sources.list.d/debian.sources ]; then
	if ! grep -q 'Types:.*deb-src' /etc/apt/sources.list.d/debian.sources; then
		sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
	fi
elif [ -f /etc/apt/sources.list ]; then
	if ! grep -q '^deb-src' /etc/apt/sources.list; then
		# Uncomment commented deb-src lines if present; otherwise synthesize
		# parallel deb-src entries from the existing deb lines. Do NOT replace
		# the deb lines in place -- that would remove all binary repositories.
		if grep -q '^# *deb-src ' /etc/apt/sources.list; then
			sudo sed -i -E 's/^# *deb-src /deb-src /' /etc/apt/sources.list
		else
			sudo sed -n -E 's/^deb /deb-src /p' /etc/apt/sources.list \
				| sudo tee -a /etc/apt/sources.list > /dev/null
		fi
	fi
fi

apply_apt_snapshot "${APT_SNAPSHOT_DATE:-}"
sudo apt-get update
verify_apt_snapshot "${APT_SNAPSHOT_DATE:-}"

# Realign every installed package with the snapshot-pinned versions before
# anything else runs. This catches two distinct cases:
#
#   1. Packages baked into the base image (e.g. libcap2) that ship at
#      whatever version the image was built with -- often older than the
#      snapshot's current version and absent from the snapshot-pinned
#      deb-src indexes, which would later break apt-get source for them.
#
#   2. Bootstrap packages (ca-certificates, gnupg, lsb-release, wget,
#      binutils) installed BEFORE apply_apt_snapshot ran. Their first
#      install fetched from the live archive (chicken-and-egg with
#      lsb_release used by apply_apt_snapshot), so they may now be NEWER
#      than the snapshot. binutils in particular MUST match: `objcopy`
#      rewrites every shared library later in the script, so two builds
#      against the same APT_SNAPSHOT_DATE would otherwise produce
#      bit-different SDKs depending on when each ran.
#
# `--allow-downgrades` is what makes case (2) work: by default
# dist-upgrade keeps a package back when installed > candidate, even when
# the candidate (snapshot) is the only version in the configured sources.
#
# Done before the LLVM repo is added so this sweep only touches
# Ubuntu/Debian snapshot-pinned packages.
sudo DEBIAN_FRONTEND=noninteractive apt-get dist-upgrade -y --allow-downgrades
# Remove auto-installed packages that were only needed by the live bootstrap
# versions above, so they cannot leak into the SDK after snapshot realignment.
sudo DEBIAN_FRONTEND=noninteractive apt-get autoremove --purge -y

# Add LLVM apt repository to pull libc++-${LibcxxDevVersion}-dev and friends.
Codename=$(lsb_release -cs)
sudo install -m 0755 -d /etc/apt/keyrings
# This wget bypasses APT_PROXY by design: the proxy exists to cache apt
# package fetches across rebuilds, not to be a general HTTPS egress
# gateway. The LLVM signing key is a single tiny one-shot HTTPS request
# (and apt traffic for packages from apt.llvm.org *does* still flow
# through the proxy because that's wired into apt.conf).
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo gpg --dearmor --yes -o /etc/apt/keyrings/llvm.gpg
echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] https://apt.llvm.org/$Codename/ llvm-toolchain-$Codename-$LibcxxDevVersion main" \
	| sudo tee /etc/apt/sources.list.d/llvm.list > /dev/null
sudo apt-get update

Packages="xcb build-essential cmake ninja-build libacl1-dev \
	libext2fs-dev libudev-dev libssl-dev uuid-dev libdbus-1-dev libsecret-1-dev libxcb-xinerama0-dev \
	libunity-dev libxkbcommon-x11-dev libxkbcommon-dev libxcb-cursor0 libxcb-cursor-dev libxcb-util-dev \
	libxcb1-dev libx11-dev libc++-${LibcxxDevVersion}-dev"

# python3-distutils was removed in Ubuntu 24.04 (merged into Python 3.12 stdlib)
if apt-get install --simulate python3-distutils &>/dev/null; then
	Packages="$Packages python3-distutils"
fi

sudo apt install -y $Packages

# deb-src is enabled earlier (before apply_apt_snapshot) so the snapshot sweep
# covers source indexes too.
sudo apt-get build-dep qtbase-opensource-src qtchooser -y

mkdir -p "$DestinationDir"
mkdir -p "$SourcePackagesDir"

# Maintain a marker-delimited block inside a .gitattributes file. Every
# invocation rewrites the lines between the BEGIN and END markers so pattern
# updates in this script propagate to previously-initialized repositories.
# Content outside the markers is preserved so user-added rules survive.
ensure_gitattributes_block() {
	local Path="$1"
	local Title="$2"
	shift 2
	local BeginMarker="# BEGIN: $Title"
	local EndMarker="# END: $Title"

	local BlockFile
	BlockFile=$(mktemp)
	{
		printf '%s\n' "$BeginMarker"
		printf '%s\n' "$@"
		printf '%s\n' "$EndMarker"
	} > "$BlockFile"

	if [ -f "$Path" ] && grep -qxF -- "$BeginMarker" "$Path" && grep -qxF -- "$EndMarker" "$Path"; then
		local Tmp
		Tmp=$(mktemp)
		awk -v begin="$BeginMarker" -v end="$EndMarker" -v blockfile="$BlockFile" '
			BEGIN {
				while ((getline line < blockfile) > 0) {
					block = block (block == "" ? "" : "\n") line
				}
				close(blockfile)
			}
			$0 == begin {
				print block
				in_block = 1
				next
			}
			in_block && $0 == end {
				in_block = 0
				next
			}
			!in_block { print }
		' "$Path" > "$Tmp"
		mv "$Tmp" "$Path"
	else
		local NeedSeparator=false
		[ -f "$Path" ] && [ -s "$Path" ] && NeedSeparator=true
		{
			[ "$NeedSeparator" = true ] && echo ""
			cat "$BlockFile"
		} >> "$Path"
	fi

	rm -f "$BlockFile"
}

ensure_gitattributes_block "$DestinationDir/.gitattributes" \
	"Malterlib Linux SDK - binary artifacts tracked via Git LFS" \
	"*.so filter=lfs diff=lfs merge=lfs -text" \
	"*.so.* filter=lfs diff=lfs merge=lfs -text" \
	"*.a filter=lfs diff=lfs merge=lfs -text" \
	"*.o filter=lfs diff=lfs merge=lfs -text"

ensure_gitattributes_block "$SourcePackagesDir/.gitattributes" \
	"Malterlib Linux SDK - Debian source tarballs tracked via Git LFS" \
	"*.tar.gz filter=lfs diff=lfs merge=lfs -text" \
	"*.tar.xz filter=lfs diff=lfs merge=lfs -text" \
	"*.tar.bz2 filter=lfs diff=lfs merge=lfs -text" \
	"*.diff.gz filter=lfs diff=lfs merge=lfs -text"

pushd "$DestinationDir"

mkdir -p Linux.sdk
pushd Linux.sdk

SDKDir="$PWD"
DistributionRootDir="$(dirname "$SDKDir")"
SourceManifestDir="$DistributionRootDir/SourceMetadata"
ModificationsPath="$DistributionRootDir/MODIFICATIONS.txt"
LegalNoticePath="$DistributionRootDir/LEGAL-NOTICE.txt"
SourceAccessPath="$DistributionRootDir/SOURCE-ACCESS.txt"

build_source_repo_url() {
	local Kind="$1"
	local RelativePath="$2"
	if [ -z "$SourcePackagesURLBase" ]; then
		return 0
	fi

	# GitHub-only by design: this is an internal Malterlib script, not a
	# general-purpose tool. The output URL uses the GitHub
	# /<blob|tree>/<branch>/<path> layout and assumes $SourcePackagesURLBase
	# is a GitHub repo URL on $SourcePackagesBranch. Pointing the source
	# mirror at GitLab, BitBucket, etc. is not supported -- the generated
	# `repo_url` entries in SourcePackageLinks.tsv would be invalid. If you
	# need to mirror elsewhere, regenerate the manifests with a different helper.
	#
	# GitHub distinguishes /tree/<ref>/<path> (directory listing) from
	# /blob/<ref>/<path> (single-file view). The source repository is served
	# through Git LFS with a custom transfer agent, so per-file blob pages
	# render the LFS pointer (version / sha256 oid / size) rather than the
	# artifact itself -- which is still the most useful anchor for a recipient
	# because it proves the artifact exists at that path and pins its content
	# hash. Use blob for files and tree for directories accordingly.
	case "$Kind" in
		tree|blob)
			;;
		*)
			echo "ERROR: build_source_repo_url: unknown kind '$Kind' (expected tree|blob)" >&2
			return 1
			;;
	esac

	# Strip trailing .git so GitHub tree/blob links resolve in a browser.
	local Base="${SourcePackagesURLBase%.git}"
	# download_source_package stores epoch-bearing versions under literal
	# `...%3a...` directory names (colon -> %3a so the path is valid on
	# filesystems that reject `:`). Without re-encoding the `%` here, the
	# `%3a` in the URL would be URL-decoded to `:` by the server/client,
	# and the fetched path would not match the actual on-disk filename.
	# Double-encoding `%` -> `%25` makes the URL decode back to the
	# original `%3a` path component.
	local RelativePathEscaped="${RelativePath//%/%25}"
	printf '%s/%s/%s/%s\n' "$Base" "$Kind" "$SourcePackagesBranch" "$RelativePathEscaped"
}

is_source_download_exempt() {
	local SourcePackage="$1"

	# LLVM is under Apache-2.0 with the LLVM exception. The upstream LLVM
	# source download artifacts we mirror for this package currently do not
	# carry separate bundled notice/license files in the package payload, so
	# this package is handled outside the Debian source-package mirror flow.
	# The major version tracks $LibcxxDevVersion (set from $LIBCXX_DEV_VERSION)
	# so overriding the libc++ version per build keeps the exemption valid.
	case "$SourcePackage" in
		"llvm-toolchain-$LibcxxDevVersion")
			return 0
			;;
	esac

	return 1
}

download_source_package() {
	local SourcePackage="$1"
	local SourceVersion="$2"
	local SafeSourceVersion="${SourceVersion//:/%3a}"
	local PackageDir="$SourcePackagesDir/$SourcePackage/$SafeSourceVersion"
	local DownloadLog="$PackageDir/download.log"

	mkdir -p "$PackageDir"

	# Cache check: only short-circuit when every component file listed in
	# the .dsc is present. An interrupted apt-get source can leave just
	# the .dsc (or .dsc + a subset of tarballs) on disk; treating that as
	# complete would publish an incomplete corresponding-source mirror.
	local DscFile
	DscFile=$(find "$PackageDir" -maxdepth 1 -type f -name '*.dsc' | head -n1)
	if [ -n "$DscFile" ]; then
		local Expected=0
		local Missing=0
		local ComponentFile
		while read -r ComponentFile; do
			[ -n "$ComponentFile" ] || continue
			Expected=$((Expected + 1))
			if [ ! -f "$PackageDir/$ComponentFile" ]; then
				Missing=$((Missing + 1))
			fi
		done < <(
			awk '
				/^[Ff]iles:[[:space:]]*$/ { in_files = 1; next }
				/^[^[:space:]]/ { in_files = 0 }
				in_files && NF >= 3 { print $NF }
			' "$DscFile"
		)
		if [ "$Expected" -gt 0 ] && [ "$Missing" -eq 0 ]; then
			return 0
		fi
		echo "Cached source package at $PackageDir is incomplete (${Missing} of ${Expected} file(s) missing); refetching..." >&2
	fi

	echo "Downloading source package $SourcePackage=$SourceVersion into $PackageDir" >&2
	if (
		cd "$PackageDir"
		apt-get source --only-source --download-only "${SourcePackage}=${SourceVersion}"
	) >"$DownloadLog" 2>&1; then
		return 0
	else
		local ExitCode=$?
		if grep -Fq "Unable to find a source package for $SourcePackage" "$DownloadLog" || \
			grep -Fq "Can not find version '$SourceVersion' of package '$SourcePackage'" "$DownloadLog"; then
			{
				echo
				echo "NOTE: The exact source package version is not currently available from the configured deb-src indexes."
				echo "If you still need to ship files from this package version, pre-seed the exact source artifacts"
				echo "(.dsc, orig tarball, debian tarball, and signatures if available) into:"
				echo "  $PackageDir"
				echo "The build will reuse those files on the next run and publish links from SourceMetadata/."
			} >>"$DownloadLog"
		fi
		return $ExitCode
	fi
}

report_source_package_coverage() {
	local SourcePackage="$1"
	local SourceVersion="$2"
	local CoverageFile="$3"

	echo "SDK files covered by $SourcePackage=$SourceVersion:" >&2
	awk -F '\t' -v pkg="$SourcePackage" -v ver="$SourceVersion" '
		NR > 1 && $5 == pkg && $6 == ver { print $1 }
	' "$CoverageFile" | sort -u | sed -n '1,100p' >&2
}

is_allowed_sdk_generated_file() {
	local SDKFile="$1"

	case "$SDKFile" in
		/SDKSettings.plist)
			return 0
			;;
	esac

	return 1
}

cat <<EndOfText > SDKSettings.plist
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CanonicalName</key>
	<string>linux</string>
	<key>DefaultDeploymentTarget</key>
	<string>15.0</string>
	<key>DefaultProperties</key>
	<dict>
		<key>AD_HOC_CODE_SIGNING_ALLOWED</key>
		<string>YES</string>
		<key>CODE_SIGNING_REQUIRED</key>
		<string>YES</string>
		<key>CODE_SIGN_ENTITLEMENTS</key>
		<string></string>
		<key>CODE_SIGN_IDENTITY</key>
		<string>Apple Development</string>
		<key>DEFAULT_COMPILER</key>
		<string>com.apple.compilers.llvm.clang.1_0</string>
		<key>DEPLOYMENT_TARGET_SUGGESTED_VALUES</key>
		<array>
			<string>10.13</string>
			<string>10.14</string>
			<string>10.15</string>
			<string>11.0</string>
			<string>11.1</string>
			<string>11.2</string>
			<string>11.3</string>
			<string>11.4</string>
			<string>11.5</string>
			<string>12.0</string>
			<string>12.2</string>
			<string>12.3</string>
			<string>12.4</string>
			<string>13.0</string>
			<string>13.1</string>
			<string>13.2</string>
			<string>13.3</string>
			<string>13.4</string>
			<string>13.5</string>
			<string>14.0</string>
			<string>14.1</string>
			<string>14.2</string>
			<string>14.3</string>
			<string>14.4</string>
			<string>14.5</string>
			<string>14.6</string>
			<string>15.0</string>
		</array>
		<key>ENTITLEMENTS_DESTINATION</key>
		<string>Signature</string>
		<key>ENTITLEMENTS_REQUIRED</key>
		<string>YES</string>
		<key>IOS_UNZIPPERED_TWIN_PREFIX_PATH</key>
		<string>/System/iOSSupport</string>
		<key>KASAN_CFLAGS_CLASSIC</key>
		<string>-DKASAN=1 -DKASAN_CLASSIC=1 -fsanitize=address -mllvm -asan-globals-live-support -mllvm -asan-force-dynamic-shadow</string>
		<key>KASAN_CFLAGS_TBI</key>
		<string>-DKASAN=1 -DKASAN_TBI=1 -fsanitize=kernel-hwaddress -mllvm -hwasan-recover=0 -mllvm -hwasan-instrument-atomics=0 -mllvm -hwasan-instrument-stack=1 -mllvm -hwasan-generate-tags-with-calls=1 -mllvm -hwasan-instrument-with-calls=1 -mllvm -hwasan-use-short-granules=0 -mllvm -hwasan-memory-access-callback-prefix=__asan_</string>
		<key>KASAN_DEFAULT_CFLAGS</key>
		<string>\$(KASAN_CFLAGS_CLASSIC)</string>
		<key>KASAN_DEFAULT_CFLAGS[arch=arm64]</key>
		<string>\$(KASAN_CFLAGS_TBI)</string>
		<key>KASAN_DEFAULT_CFLAGS[arch=arm64e]</key>
		<string>\$(KASAN_CFLAGS_TBI)</string>
		<key>MACOSX_DEPLOYMENT_TARGET</key>
		<string>15.0</string>
		<key>PLATFORM_NAME</key>
		<string>macosx</string>
		<key>TAPI_USE_SRCROOT</key>
		<string>YES</string>
		<key>TAPI_VERIFY_MODE</key>
		<string>Pedantic</string>
		<key>TEST_FRAMEWORK_SEARCH_PATHS</key>
		<string>\$(inherited) \$(PLATFORM_DIR)/Developer/Library/Frameworks</string>
		<key>TEST_LIBRARY_SEARCH_PATHS</key>
		<string>\$(inherited) \$(PLATFORM_DIR)/Developer/usr/lib</string>
	</dict>
	<key>DefaultVariant</key>
	<string>macos</string>
	<key>DisplayName</key>
	<string>Linux</string>
	<key>IsBaseSDK</key>
	<string>YES</string>
	<key>MaximumDeploymentTarget</key>
	<string>15.0.99</string>
	<key>MinimalDisplayName</key>
	<string>15.0</string>
	<key>PropertyConditionFallbackNames</key>
	<array/>
	<key>SupportedTargets</key>
	<dict>
		<key>macosx</key>
		<dict>
			<key>Archs</key>
			<array>
				<string>x86_64</string>
				<string>x86_64h</string>
				<string>arm64</string>
				<string>arm64e</string>
			</array>
			<key>BuildVersionPlatformID</key>
			<string>1</string>
			<key>ClangRuntimeLibraryPlatformName</key>
			<string>osx</string>
			<key>DefaultDeploymentTarget</key>
			<string>15.0</string>
			<key>DeploymentTargetSettingName</key>
			<string>MACOSX_DEPLOYMENT_TARGET</string>
			<key>DeviceFamilies</key>
			<array>
				<dict>
					<key>DisplayName</key>
					<string>Mac</string>
					<key>Name</key>
					<string>mac</string>
				</dict>
			</array>
			<key>LLVMTargetTripleEnvironment</key>
			<string></string>
			<key>LLVMTargetTripleSys</key>
			<string>macos</string>
			<key>LLVMTargetTripleVendor</key>
			<string>apple</string>
			<key>MaximumDeploymentTarget</key>
			<string>15.0.99</string>
			<key>MinimumDeploymentTarget</key>
			<string>10.13</string>
			<key>PlatformFamilyDisplayName</key>
			<string>macOS</string>
			<key>PlatformFamilyName</key>
			<string>macOS</string>
			<key>RecommendedDeploymentTarget</key>
			<string>11.0</string>
			<key>SwiftConcurrencyMinimumDeploymentTarget</key>
			<string>12.0</string>
			<key>SwiftOSRuntimeMinimumDeploymentTarget</key>
			<string>10.14.4</string>
			<key>SystemPrefix</key>
			<string></string>
			<key>ValidDeploymentTargets</key>
			<array>
				<string>10.13</string>
				<string>10.14</string>
				<string>10.15</string>
				<string>11.0</string>
				<string>11.1</string>
				<string>11.2</string>
				<string>11.3</string>
				<string>11.4</string>
				<string>11.5</string>
				<string>12.0</string>
				<string>12.2</string>
				<string>12.3</string>
				<string>12.4</string>
				<string>13.0</string>
				<string>13.1</string>
				<string>13.2</string>
				<string>13.3</string>
				<string>13.4</string>
				<string>13.5</string>
				<string>14.0</string>
				<string>14.1</string>
				<string>14.2</string>
				<string>14.3</string>
				<string>14.4</string>
				<string>14.5</string>
				<string>14.6</string>
				<string>15.0</string>
			</array>
		</dict>
	</dict>
	<key>Variants</key>
	<array>
		<dict>
			<key>BuildSettings</key>
			<dict>
				<key>CODE_SIGN_IDENTITY</key>
				<string>\$(CODE_SIGN_IDENTITY_\$(_DEVELOPMENT_TEAM_IS_EMPTY))</string>
				<key>CODE_SIGN_IDENTITY_NO</key>
				<string>Apple Development</string>
				<key>CODE_SIGN_IDENTITY_YES</key>
				<string>-</string>
				<key>IPHONEOS_DEPLOYMENT_TARGET</key>
				<string>18.0</string>
				<key>LLVM_TARGET_TRIPLE_OS_VERSION</key>
				<string>\$(LLVM_TARGET_TRIPLE_OS_VERSION_\$(_MACOSX_DEPLOYMENT_TARGET_IS_EMPTY))</string>
				<key>LLVM_TARGET_TRIPLE_OS_VERSION_NO</key>
				<string>macos\$(MACOSX_DEPLOYMENT_TARGET)</string>
				<key>LLVM_TARGET_TRIPLE_OS_VERSION_YES</key>
				<string>macos15.0</string>
				<key>LLVM_TARGET_TRIPLE_SUFFIX</key>
				<string></string>
				<key>_BOOL_</key>
				<string>NO</string>
				<key>_BOOL_NO</key>
				<string>NO</string>
				<key>_BOOL_YES</key>
				<string>YES</string>
				<key>_DEVELOPMENT_TEAM_IS_EMPTY</key>
				<string>\$(_BOOL_\$(_IS_EMPTY_\$(DEVELOPMENT_TEAM)))</string>
				<key>_IS_EMPTY_</key>
				<string>YES</string>
				<key>_MACOSX_DEPLOYMENT_TARGET_IS_EMPTY</key>
				<string>\$(_BOOL_\$(_IS_EMPTY_\$(MACOSX_DEPLOYMENT_TARGET)))</string>
			</dict>
			<key>Name</key>
			<string>macos</string>
		</dict>
	</array>
	<key>Version</key>
	<string>15.0</string>
</dict>
</plist>
EndOfText

rm -rf lib
rm -rf usr/lib
rm -rf usr/llvm-*
mkdir -p usr/lib

if ! [ -h /lib ] ; then
	cp -r "/lib/${Architecture}-linux-gnu/"* usr/lib/
fi

#mkdir -p "usr/lib/clang/"
#cp -r "../llvm-malterlib/build/main/lib/clang/"* "usr/lib/clang/"
ln -sfnT usr/lib lib
ln -sfnT usr/lib lib64
ln -sfnT usr/lib lib32
ln -sfnT usr/lib libx32

cp -r "/usr/lib/${Architecture}-linux-gnu/"* usr/lib/
# apt.llvm.org libunwind development packages install linker inputs in the
# triplet directory as relative symlinks to ../llvm-<version>/lib. Preserve
# that symlink topology by copying only the referenced LLVM target files into
# the path those flattened symlinks resolve to inside the SDK.
copy_llvm_lib_path() {
	local SourcePath="$1"
	local SDKPath="$2"

	mkdir -p "$(dirname "$SDKPath")"
	cp -a "$SourcePath" "$SDKPath"
}

copy_llvm_lib_path_and_targets() {
	local SourcePath="$1"
	local SDKPath="$2"
	local nLinks=0

	while [ -L "$SourcePath" ]; do
		copy_llvm_lib_path "$SourcePath" "$SDKPath"

		local LinkTarget
		LinkTarget="$(readlink "$SourcePath")" || return 0
		case "$LinkTarget" in
			/*)
				SourcePath="$LinkTarget"
				case "$SourcePath" in
					"/usr/lib/llvm-$LibcxxDevVersion/"*)
						SDKPath="$SDKDir/usr/${SourcePath#/usr/lib/}"
						;;
					*)
						echo "ERROR: LLVM lib symlink target escapes expected tree: $LinkTarget" >&2
						exit 1
						;;
				esac
				;;
			*)
				SourcePath="$(realpath -m "$(dirname "$SourcePath")/$LinkTarget")"
				SDKPath="$(realpath -m "$(dirname "$SDKPath")/$LinkTarget")"
				;;
		esac

		nLinks=$((nLinks + 1))
		if [ "$nLinks" -gt 16 ]; then
			echo "ERROR: symlink loop while copying LLVM lib target: $SDKPath" >&2
			exit 1
		fi
	done

	if [ ! -e "$SourcePath" ]; then
		echo "ERROR: LLVM lib target is missing on the build host: $SourcePath" >&2
		exit 1
	fi
	copy_llvm_lib_path "$SourcePath" "$SDKPath"
}

for SDKLink in usr/lib/libunwind*; do
	[ -L "$SDKLink" ] || continue
	LinkTarget="$(readlink "$SDKLink")" || continue
	case "$LinkTarget" in
		"../llvm-$LibcxxDevVersion/lib/"*)
			SDKTarget="$(realpath -m "$(dirname "$SDKLink")/$LinkTarget")"
			case "$SDKTarget" in
				"$SDKDir/usr/llvm-$LibcxxDevVersion/lib/"*)
					HostTarget="/usr/lib/${SDKTarget#"$SDKDir/usr/"}"
					copy_llvm_lib_path_and_targets "$HostTarget" "$SDKTarget"
					;;
				*)
					echo "ERROR: SDK libunwind symlink target escapes expected tree: $SDKLink -> $LinkTarget" >&2
					exit 1
					;;
			esac
			;;
	esac
done
cp -r /usr/lib/gcc usr/lib/
# Clean up stale links that older script versions could create inside usr/lib
# when refreshing top-level lib* symlinks on repeat builds.
rm -f usr/lib/lib usr/lib/lib64 usr/lib/lib32 usr/lib/libx32

rm -fr usr/share/pkgconfig
mkdir -p usr/share/pkgconfig
cp -r /usr/share/pkgconfig usr/share/

rm -rf usr/share/common-licenses
mkdir -p usr/share
cp -r /usr/share/common-licenses usr/share/

rm -rf "usr/lib/libc++"*
rm -rf "usr/lib/libcunwind"*

#cp -r "../llvm-malterlib/build/main/lib/libc++"* "usr/lib/"
#cp -r "../llvm-malterlib/build/main/lib/libunwind"* "usr/lib/"

# Remove some big stuff
rm -rf usr/lib/guile
rm -rf usr/lib/aisleriot
rm -f usr/lib/gcc/*-linux-gnu/*/lto*
rm -f usr/lib/gcc/*-linux-gnu/*/cc*
rm -rf usr/lib/girepository-1.0
rm -rf usr/lib/qt5/mkspecs/common/winrt_winphone
rm -f usr/lib/firebird/*/firebird.msg
rm -f usr/lib/gconv/gconv-modules.cache
rm -rf usr/lib/espeak-ng-data
rm -rf usr/lib/gedit/plugins
rm -rf usr/lib/rhythmbox/plugins
rm -rf usr/lib/gnome-software
rm -f usr/lib/avahi/service-types.db
rm -rf usr/lib/eog
rm -rf usr/lib/evince
rm -rf usr/lib/deja-dup
rm -rf usr/lib/elisa
rm -rf usr/lib/gio/modules
rm -rf usr/lib/gdk-pixbuf-2.0/*/loaders
rm -rf usr/lib/gstreamer-1.0
rm -rf usr/lib/gstreamer1.0
rm -rf usr/lib/gtk-2.0/*/engines
rm -rf usr/lib/gtk-2.0/*/immodules
rm -rf usr/lib/gtk-2.0/*/printbackends
rm -rf usr/lib/gtk-2.0/modules
rm -f usr/lib/gtk-2.0/*/immodules.cache
rm -rf usr/lib/gtk-3.0/*/immodules
rm -rf usr/lib/gtk-3.0/*/printbackends
rm -rf usr/lib/gtk-3.0/modules
rm -f usr/lib/gtk-3.0/*/immodules.cache
rm -rf usr/lib/gtk-4.0/*/immodules
rm -rf usr/lib/gtk-4.0/*/media
rm -rf usr/lib/gtk-4.0/*/printbackends
rm -rf usr/lib/libdecor/plugins-1
rm -rf usr/lib/libpeas-1.0
rm -rf usr/lib/nautilus
rm -rf usr/lib/gnome-terminal
rm -rf usr/lib/gvfs
rm -rf usr/lib/open-vm-tools
rm -rf usr/lib/openvpn/plugins
rm -rf usr/lib/packagekit-backend
rm -rf usr/lib/perl
rm -rf usr/lib/perl5
rm -rf usr/lib/perl-base
rm -rf usr/lib/nodejs
rm -f usr/lib/gdk-pixbuf-2.0/*/loaders.cache
rm -rf usr/lib/libexec/__pycache__
rm -f usr/lib/vlc/plugins/plugins.dat

# Remove executables
find . -type f -executable | grep -v '.*\.so\($\|\.\)' | xargs rm -f
rm -rf usr/lib/qt5/bin usr/lib/qtchooser usr/lib/qt-default

# Remove cmake files
find . -name '*.cmake' | xargs rm -f

rm -rf usr/include
cp -r /usr/include usr/
rm -rf "usr/include/c++"
mkdir -p "usr/include/c++"

#cp -r "../llvm-malterlib/build/main/include/c++/"* "usr/include/c++/"

for FirebirdInclude in usr/lib/firebird/*/include; do
	[ -L "$FirebirdInclude" ] || continue
	ln -sfnT ../../../include "$FirebirdInclude"
done

ln -sfnT . usr/lib/${Architecture}-linux-gnu

find . -type l | while read l; do
	link="$(readlink "$l")"
	if [[ "$link" =~ ^/ ]]; then
	if [ -e "${SDKDir}$link" ] ; then
		ln -fs "$(realpath --relative-to="$(dirname "$(realpath -s "$l")")" "${SDKDir}$link")" "$l"
		fi
	fi
done

# Remove files and directories that only differ by case
find . | sort -f | uniq -di | xargs rm -r

# Strip elf files to only needed for linking (skip linker scripts)
nStripped=0
nSkipped=0
while IFS= read -r -d '' File; do
	# Skip files that are not ELF binaries (e.g. GNU linker scripts)
	if head -c4 "$File" | grep -q $'\x7fELF'; then
		objcopy -j .dynamic -j .dynsym -j .dynstr -j .symtab -j .strtab -j .shstrtab -j .gnu.version -j .gnu.version_d -j .gnu.version_r "$File" 2>&1 | sed '/^objcopy: .*warning: empty loadable segment detected at/d'
		nStripped=$((nStripped + 1))
	else
		nSkipped=$((nSkipped + 1))
	fi
done < <(find . -type f \( -name '*.so' -o -name '*.so.*' \) -print0)
echo "Stripped $nStripped ELF shared libraries, skipped $nSkipped non-ELF files (linker scripts etc.)"

BrokenLinks=$(mktemp)
while IFS= read -r -d '' Link; do
	if [ ! -e "$Link" ]; then
		printf '%s -> %s\n' "${Link#$SDKDir/}" "$(readlink "$Link")"
	fi
done < <(find "$SDKDir" -type l -print0) > "$BrokenLinks"

if [ -s "$BrokenLinks" ]; then
	echo "ERROR: Broken symlinks remain in SDK:" >&2
	sed -n '1,100p' "$BrokenLinks" >&2
	rm -f "$BrokenLinks"
	exit 1
fi
rm -f "$BrokenLinks"

# Collect license notices for all files included in the SDK
echo "Collecting license notices..."
LicenseDir="$DistributionRootDir/Licenses"
RootLicensePath="$DistributionRootDir/LICENSE"
rm -rf "$LicenseDir"
mkdir -p "$LicenseDir"
rm -rf "$SourceManifestDir"
mkdir -p "$SourceManifestDir"

# Include the exact generator script next to the SDK so recipients get the local
# build/modification logic that produced the redistributed binaries.
cp "$GeneratorScriptPath" "$SourceManifestDir/BuildLinuxSDK.sh"

# Also bundle the container-based reproduction harness (driver script +
# Dockerfiles) so a recipient can rebuild the SDK from scratch against the
# same base images. Paths are provided by the driver via env vars when
# running inside Docker; otherwise fall back to siblings on a developer
# checkout.
GeneratorScriptDir="$(dirname "$GeneratorScriptPath")"
DriverScriptPath="${DRIVER_SCRIPT_PATH:-$GeneratorScriptDir/BuildLinuxSDKContainers.sh}"
# Two layouts:
#   * Shipped SDK bundle:    SourceMetadata/Docker/<Distro>/Dockerfile
#                            (alongside this script)
#   * Developer checkout:    Tool/Docker/LinuxSDK/<Distro>/Dockerfile
#                            (one level up from Tool/Scripts/)
# Mirror the driver's own bundle-detection logic (BuildLinuxSDKContainers.sh
# checks $ScriptDir/Docker first) so a direct-from-bundle invocation finds
# the Dockerfiles instead of looking at the non-existent <bundle>/Docker.
if [ -z "${DOCKER_ROOT:-}" ] && [ -d "$GeneratorScriptDir/Docker" ]; then
	DockerRoot="$GeneratorScriptDir/Docker"
else
	DockerRoot="${DOCKER_ROOT:-$(dirname "$GeneratorScriptDir")/Docker/LinuxSDK}"
fi

if [ -f "$DriverScriptPath" ]; then
	cp "$DriverScriptPath" "$SourceManifestDir/BuildLinuxSDKContainers.sh"
	chmod +x "$SourceManifestDir/BuildLinuxSDKContainers.sh"
fi
if [ -d "$DockerRoot" ]; then
	cp -r "$DockerRoot" "$SourceManifestDir/Docker"
fi

TmpPaths=$(mktemp)
TmpFileSystemPaths=$(mktemp)
TmpFilePackageMap=$(mktemp)
TmpPackages=$(mktemp)
TmpPackageVersions=$(mktemp)
TmpSourceLinks=$(mktemp)
TmpSourceLinksUnsorted=$(mktemp)
TmpFileMappings=$(mktemp)
TmpAllSDKFiles=$(mktemp)
TmpPackageVersionMap=$(mktemp)
TmpSdkSystemPathMap=$(mktemp)
TmpJoinedSystemPathPackageMap=$(mktemp)
TmpUnresolvedSDKFiles=$(mktemp)

# Map SDK usr/lib files back to possible system paths
find "$SDKDir/usr/lib" -type f -not -path "$SDKDir/usr/lib/gcc/*" | while read -r File; do
	RelPath="${File#$SDKDir/usr/lib/}"
	echo "${File#$SDKDir}"$'\t'"/usr/lib/${Architecture}-linux-gnu/$RelPath"
	echo "${File#$SDKDir}"$'\t'"/lib/${Architecture}-linux-gnu/$RelPath"
done >> "$TmpPaths"

# Map preserved LLVM support files back to their original system paths.
if [ -d "$SDKDir/usr/llvm-$LibcxxDevVersion" ]; then
	find "$SDKDir/usr/llvm-$LibcxxDevVersion" -type f | while read -r File; do
		RelPath="${File#$SDKDir/usr/llvm-$LibcxxDevVersion/}"
		echo "${File#$SDKDir}"$'\t'"/usr/lib/llvm-$LibcxxDevVersion/$RelPath"
	done >> "$TmpPaths"
fi

# Map gcc files back to system paths
find "$SDKDir/usr/lib/gcc" -type f 2>/dev/null | while read -r File; do
	echo "${File#$SDKDir}"$'\t'"${File#$SDKDir}"
done >> "$TmpPaths"

# Map header files back to system paths
find "$SDKDir/usr/include" -type f 2>/dev/null | while read -r File; do
	echo "${File#$SDKDir}"$'\t'"${File#$SDKDir}"
done >> "$TmpPaths"

# Map pkg-config files back to system paths
find "$SDKDir/usr/share/pkgconfig" -type f 2>/dev/null | while read -r File; do
	echo "${File#$SDKDir}"$'\t'"${File#$SDKDir}"
done >> "$TmpPaths"

# Map common license files back to system paths
find "$SDKDir/usr/share/common-licenses" -type f 2>/dev/null | while read -r File; do
	echo "${File#$SDKDir}"$'\t'"${File#$SDKDir}"
done >> "$TmpPaths"

# Resolve system path ownership for every SDK file we copied from the distribution
sort -u "$TmpPaths" > "$TmpSdkSystemPathMap"
cut -f2 "$TmpSdkSystemPathMap" | while read -r SystemPath; do
	[ -e "$SystemPath" ] && printf '%s\n' "$SystemPath"
done | sort -u > "$TmpFileSystemPaths"
(
	xargs -r dpkg -S < "$TmpFileSystemPaths" \
		2> >(grep -vE 'dpkg-query: no path found matching pattern /(usr/)?lib/[^/]+-linux-gnu/' >&2) || true
) \
	| while read -r OwnershipLine; do
		PackageList="${OwnershipLine%: *}"
		SystemPath="${OwnershipLine##*: }"

		case "$PackageList" in
			diversion\ by\ *|diverted\ by\ *)
				continue
				;;
		esac
		# Split PackageList on commas using pure-bash IFS; avoids forking
		# echo | tr | sed | while once per ownership line (was 3 external
		# processes per line * thousands of lines).
		IFS=',' read -ra _PkgParts <<< "$PackageList"
		for _PkgPart in "${_PkgParts[@]}"; do
			Package="${_PkgPart# }"
			[ -n "$Package" ] || continue
			case "$Package" in
				*:*)
					Package="${Package%%:*}"
					;;
			esac
			printf '%s\t%s\n' "$SystemPath" "$Package"
		done
	done | sort -u > "$TmpFilePackageMap"

# Collect package lists and copyright files
cut -f2 "$TmpFilePackageMap" | sort -u > "$TmpPackages"
while read -r Package; do
		if [ -f "/usr/share/doc/$Package/copyright" ]; then
			cp "/usr/share/doc/$Package/copyright" "$LicenseDir/$Package.copyright"
		fi
done < "$TmpPackages"

# Resolve exact package versions in bulk
xargs -r dpkg-query -W -f='${Package}\t${Version}\t${source:Package}\t${source:Version}\n' < "$TmpPackages" \
	| sort -u > "$TmpPackageVersionMap"

# Join system-path ownership to SDK files, then join package versions
join -t $'\t' -1 2 -2 1 \
	<(sort -t $'\t' -k2,2 "$TmpSdkSystemPathMap") \
	<(sort -t $'\t' -k1,1 "$TmpFilePackageMap") \
	| sort -t $'\t' -k3,3 > "$TmpJoinedSystemPathPackageMap"

join -t $'\t' -1 3 -2 1 \
	"$TmpJoinedSystemPathPackageMap" \
	<(sort -t $'\t' -k1,1 "$TmpPackageVersionMap") \
	| while IFS=$'\t' read -r Package SystemPath SDKFile BinaryVersion SourcePackage SourceVersion; do
		printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
			"$SDKFile" "$SystemPath" "$Package" "$BinaryVersion" "$SourcePackage" "$SourceVersion"
	done | sort -u > "$TmpFileMappings"

{
	echo -e "sdk_file\tsystem_path\tbinary_package\tbinary_version\tsource_package\tsource_version"
	cat "$TmpFileMappings"
} > "$SourceManifestDir/FilePackageVersions.tsv"

FileCoverageManifest="$SourceManifestDir/FilePackageVersions.tsv"

cut -f3-6 "$TmpFileMappings" | sort -u > "$TmpPackageVersions"

# Download exact source packages and record where they live in the separate
# public source repository. This mirror is intended to be append-only and
# immutable for already-published paths, even though the generated URLs may
# point at tree/<branch>/... locations. The compliance model here relies on
# those exact published paths remaining durably available and unchanged, so a
# separate tagged release per SDK build is not required as long as that
# stability guarantee is upheld operationally.
while IFS=$'\t' read -r BinaryPackage BinaryVersion SourcePackage SourceVersion; do
	[ -n "$SourcePackage" ] || continue
	[ -n "$SourceVersion" ] || continue

	if is_source_download_exempt "$SourcePackage"; then
		printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
			"$BinaryPackage" "$BinaryVersion" "$SourcePackage" "$SourceVersion" "-" "-" "EXEMPT"
		continue
	fi

	if ! download_source_package "$SourcePackage" "$SourceVersion"; then
		echo "ERROR: Failed to download source package $SourcePackage=$SourceVersion" >&2
		echo "See log: $SourcePackagesDir/$SourcePackage/${SourceVersion//:/%3a}/download.log" >&2
		if grep -Fq "NOTE: The exact source package version is not currently available from the configured deb-src indexes." \
			"$SourcePackagesDir/$SourcePackage/${SourceVersion//:/%3a}/download.log"; then
			echo "The installed binary version is not present in the current deb-src indexes." >&2
			echo "Pre-seed the exact source files into $SourcePackagesDir/$SourcePackage/${SourceVersion//:/%3a}/ and rerun." >&2
		fi
		report_source_package_coverage "$SourcePackage" "$SourceVersion" "$FileCoverageManifest"
		exit 1
	fi

	SafeSourceVersion="${SourceVersion//:/%3a}"
	SourceRelativeDir="$SourcePackage/$SafeSourceVersion"

	find "$SourcePackagesDir/$SourceRelativeDir" -maxdepth 1 -type f ! -name 'download.log' | sort | while read -r SourceFile; do
		RepoRelativePath="${SourceFile#$SourcePackagesDir/}"
		FileURL="$(build_source_repo_url blob "$RepoRelativePath")"
		printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
			"$BinaryPackage" "$BinaryVersion" "$SourcePackage" "$SourceVersion" "$RepoRelativePath" "$SourceRelativeDir" "$FileURL"
	done
done < "$TmpPackageVersions" > "$TmpSourceLinksUnsorted"

sort -u "$TmpSourceLinksUnsorted" > "$TmpSourceLinks"

{
	echo -e "binary_package\tbinary_version\tsource_package\tsource_version\trepo_relative_path\tsource_directory\trepo_url"
	cat "$TmpSourceLinks"
} > "$SourceManifestDir/SourcePackageLinks.tsv"

{
	echo -e "binary_package\tbinary_version\tsource_package\tsource_version"
	cat "$TmpPackageVersions"
} > "$SourceManifestDir/PackageVersions.tsv"

cat <<EOF > "$SourceAccessPath"
Linux SDK - Source Access
=========================

Corresponding source package metadata for this SDK is recorded in:

  SourceMetadata/PackageVersions.tsv
  SourceMetadata/SourcePackageLinks.tsv
  SourceMetadata/FilePackageVersions.tsv

Source packages are mirrored in a separate source repository rooted at:

  ${SourcePackagesURLBase:-<source repository URL not configured>}

Source manifest URLs target branch:

  $SourcePackagesBranch

The TSV manifests record exact source package names, versions, and repository
paths. If the source repository uses pointer files or a bootstrap workflow to
materialize large artifacts, that retrieval method is part of the repository's
normal access path and applies uniformly to all mirrored source packages.

For source access instructions, start at the source repository root and follow
its bootstrap/README documentation.

If the automated source access path is unavailable, contact:

  info@malterlib.org
EOF

cat <<EOF > "$ModificationsPath"
This SDK contains unmodified files and mechanically transformed shared libraries.

Shared libraries matching *.so and *.so.* were copied from the Ubuntu/Debian
distribution installed on the build machine and then rewritten with:

  SourceMetadata/BuildLinuxSDK.sh

The transformation keeps ELF metadata needed for linking and removes most other
sections using objcopy.

Build snapshot: ${APT_SNAPSHOT_DATE:-(none -- snapshot pinning disabled)}
Architecture: $Architecture

Distribution info:
$(lsb_release -a 2>/dev/null)

Reproducing this SDK
--------------------

The full container-based reproduction harness is bundled next to the
generator script:

  SourceMetadata/BuildLinuxSDK.sh              - build script (runs inside a
                                                 matching Linux container)
  SourceMetadata/BuildLinuxSDKContainers.sh    - multi-arch Docker driver
                                                 (registers QEMU binfmt, builds
                                                 each arch sequentially)
  SourceMetadata/Docker/<Distro>/Dockerfile    - per-distro base images

Typical flow on a Linux host with Docker + buildx:

  cd SourceMetadata
  ./BuildLinuxSDKContainers.sh                    # build all configured arches
  ./BuildLinuxSDKContainers.sh Ubuntu:arm64       # or build a single arch

When run from a standalone SDK distribution the driver detects the adjacent
Docker/ directory and defaults the output to SourceMetadata/RebuiltSDK; set
BINARIES_ROOT=/path/to/output to redirect it elsewhere, or edit Distros /
BinariesRoot at the top of the script for more involved customisation.

The Dockerfiles under Docker/<Distro>/ pin their base images by digest, so a
rebuild from this bundle reproduces against the same Ubuntu / Debian base
that produced these binaries regardless of later registry retagging.

Use BuildLinuxSDKContainers.sh to reproduce. BuildLinuxSDK.sh is the
inner build script and is intended to be invoked by the driver inside
a fresh container. Running it directly on a developer host is NOT
supported: the script mutates apt state (snapshot pinning, proxy
configuration, third-party repos) and does not unwind those changes
on rerun, so the host's apt configuration drifts and subsequent
operations on that host can break in non-obvious ways. The
container-based path keeps every mutation inside a one-shot
container and is the only path that produces reproducible SDK
output.

Reproducible apt package versions
---------------------------------

BuildLinuxSDK.sh pins apt to a specific snapshot date so every rebuild
installs the same Ubuntu and Debian package versions that produced the
shipped SDK. The date is hardcoded in BuildLinuxSDKContainers.sh.

Override the date by exporting APT_SNAPSHOT_DATE=<YYYYMMDDTHHMMSSZ> before
running the driver. Set APT_SNAPSHOT_DATE="" (empty) to disable snapshot
pinning and build against the current live archives.

LLVM toolchain
--------------

The LLVM runtime packages (libc++, libunwind) are installed from
apt.llvm.org and pinned at the major-version level. The exact point
release may vary between rebuilds as upstream ships updates. LLVM is
licensed under Apache-2.0 with the LLVM exception, which does not require
reproducible or corresponding source for redistributed binaries.

EOF

cat <<'EOF' > "$LegalNoticePath"
Linux SDK - Redistribution Boundary Notice
==========================================

This SDK redistributes headers, libraries, object files, startup files, and
other materials from Ubuntu and Debian packages. The distributor of this SDK is
responsible for complying with the licenses that apply to the SDK artifact
itself, including providing the included license notices, modification notices,
and corresponding source/package metadata shipped with this SDK.

Developers who use this SDK may have separate legal obligations when they
distribute applications, libraries, or other binaries built with it. Those
obligations depend on what they distribute and how they link it.

In particular, this SDK includes static libraries (*.a) and object/startup
files (*.o). That is intentional: this artifact is a general Linux sysroot, not
a curated "copyleft-safe" subset. Using those files in distributed binaries can
create additional license obligations for the downstream distributor under the
applicable component licenses, including copyleft licenses such as the LGPL or
GPL.

This notice is informational only and does not replace the actual license terms
for any component. See LICENSE, Licenses/, MODIFICATIONS.txt, and
SourceMetadata/ for the governing notices and source-package information. See
SOURCE-ACCESS.txt for the general source retrieval path.
EOF

find "$SDKDir" -type f | sed "s#^$SDKDir##" | sort > "$TmpAllSDKFiles"

# Compute the set of SDK files that the package-ownership join did NOT
# resolve, as one sort+comm pair. The previous implementation ran
# `grep -Fq "$SDKFile"$'\t' "$TmpFileMappings"` once per SDK file, which is
# O(files * mapping_lines) in both fork count and bytes scanned -- several
# seconds on a 20k-file SDK. With comm it's one linear pass.
TmpMappedSDKFiles=$(mktemp)
TmpUnmappedSDKFiles=$(mktemp)
cut -f1 "$TmpFileMappings" | sort -u > "$TmpMappedSDKFiles"
comm -23 "$TmpAllSDKFiles" "$TmpMappedSDKFiles" > "$TmpUnmappedSDKFiles"

while read -r SDKFile; do
	if ! is_allowed_sdk_generated_file "$SDKFile"; then
		echo "$SDKFile" >> "$TmpUnresolvedSDKFiles"
		continue
	fi

	printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$SDKFile" "-" "sdk-generated" "-" "-" "-"
done < "$TmpUnmappedSDKFiles" >> "$SourceManifestDir/FilePackageVersions.tsv"

rm -f "$TmpMappedSDKFiles" "$TmpUnmappedSDKFiles"

if [ -s "$TmpUnresolvedSDKFiles" ]; then
	echo "ERROR: Unresolved SDK files found that are not in the generated-file whitelist:" >&2
	sed -n '1,100p' "$TmpUnresolvedSDKFiles" >&2
	exit 1
fi

rm -f "$TmpPaths" "$TmpFileSystemPaths" "$TmpFilePackageMap" "$TmpPackages" \
	"$TmpPackageVersions" "$TmpSourceLinks" "$TmpFileMappings" "$TmpAllSDKFiles" \
	"$TmpPackageVersionMap" "$TmpSdkSystemPathMap" "$TmpJoinedSystemPathPackageMap" \
	"$TmpUnresolvedSDKFiles" "$TmpSourceLinksUnsorted"

NumLicenses=$(ls "$LicenseDir" | wc -l)
echo "Collected $NumLicenses license files into $LicenseDir"

# Generate root LICENSE file next to the SDK rather than inside the sysroot.
cat <<'LICEOF' > "$RootLicensePath"
Linux SDK - Third-Party License Notices
========================================

This SDK contains headers, libraries, and other files redistributed from
packages provided by Ubuntu and Debian. These files are subject to their
respective licenses as documented by each upstream package.

Individual license notices for each package are located in the Licenses/
directory. Each file is named <package>.copyright and follows the Debian
machine-readable copyright format (many in DEP-5 format).

Package/source manifests are located in the SourceMetadata/ directory.
Modification notes for this SDK are located in MODIFICATIONS.txt.
General source retrieval instructions are located in SOURCE-ACCESS.txt.
The SDK build/modification script used to generate this artifact is included at
SourceMetadata/BuildLinuxSDK.sh.

The following packages are included:

LICEOF

ls "$LicenseDir" | sed 's/\.copyright$//' | sed 's/^/  - /' >> "$RootLicensePath"

cat <<'LICEOF' >> "$RootLicensePath"

For the full license terms of any component, refer to the corresponding
file in the Licenses/ directory or Linux.sdk/usr/share/common-licenses/.
LICEOF

echo "Generated $RootLicensePath"
