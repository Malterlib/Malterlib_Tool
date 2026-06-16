#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -euo pipefail

ScriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

pushd "$ScriptDir/../../.." >/dev/null
	MalterlibRoot="$PWD"
popd >/dev/null

Dockerfile="$MalterlibRoot/Malterlib/Tool/Docker/LLVMDistribution/Ubuntu22.04/Dockerfile"
DockerContext="$(dirname "$Dockerfile")"
ImageName="${LLVMDockerImage:-malterlib/llvm-distribution:ubuntu22.04}"
SourceRoot="$(dirname "$MalterlibRoot")"

if [[ ! -f "$Dockerfile" ]]; then
	echo "Unable to find LLVM distribution Dockerfile: $Dockerfile" >&2
	exit 1
fi

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

docker_cmd()
{
	"${DockerPrefix[@]}" docker "$@"
}

DockerBuildArgs=()
DockerRunArgs=()
DockerVolumeArgs=()

if [[ "$SourceRoot" != "/" ]]; then
	DockerVolumeArgs+=(-v "$SourceRoot:$SourceRoot")
else
	DockerVolumeArgs+=(-v "$MalterlibRoot:$MalterlibRoot")
fi

AddVolumeIfPresent()
{
	local Path="$1"

	if [[ -d "$Path" ]]; then
		DockerVolumeArgs+=(-v "$Path:$Path")
	fi
}

AddVolumeIfPresent /CompiledFiles
AddVolumeIfPresent /Deploy

DockerPlatform="${LLVMDockerPlatform:-}"
case "${MalterlibLLVMArch:-}" in
	x64)
		DockerPlatform="${DockerPlatform:-linux/amd64}"
		;;
	arm64)
		DockerPlatform="${DockerPlatform:-linux/arm64}"
		;;
esac

if [[ -n "$DockerPlatform" ]]; then
	DockerBuildArgs+=(--platform "$DockerPlatform")
	DockerRunArgs+=(--platform "$DockerPlatform")
fi

if [[ "${LLVMDockerPull:-false}" == "true" ]]; then
	DockerBuildArgs+=(--pull)
fi

if [[ -n "${APT_PROXY:-}" ]]; then
	DockerBuildArgs+=(--build-arg "APT_PROXY=$APT_PROXY")
fi

if [[ "${LLVMDockerSkipImageBuild:-false}" != "true" ]]; then
	echo "Building Docker image: $ImageName"
	docker_cmd build "${DockerBuildArgs[@]}" -t "$ImageName" "$DockerContext"
else
	echo "Skipping Docker image build: $ImageName"
fi

if [[ "${LLVMDockerAddHostGateway:-true}" == "true" ]]; then
	DockerRunArgs+=(--add-host=host.docker.internal:host-gateway)
fi

DockerRunUser="${LLVMDockerUser:-host}"
case "$DockerRunUser" in
	host|current)
		DockerRunArgs+=(--user "$(id -u):$(id -g)" -e "HOME=/tmp" -e "USER=$(id -un)" -e "LOGNAME=$(id -un)")
		;;
	root)
		DockerRunArgs+=(-e "HOME=/root")
		;;
	*)
		DockerRunArgs+=(--user "$DockerRunUser")
		;;
esac

DockerEnvArgs=(
	-e "BuildIncremental=${BuildIncremental:-true}"
	-e "InstallDependencies=${InstallDependencies:-false}"
	-e "MalterlibLLVMPlatform=${MalterlibLLVMPlatform:-Linux}"
	-e "MALTERLIB_ROOT=$MalterlibRoot"
	-e "MALTERLIB_SCRIPT_DIR=$ScriptDir"
)

AddEnvIfSet()
{
	local Name="$1"

	if [[ -n "${!Name:-}" ]]; then
		DockerEnvArgs+=(-e "$Name=${!Name}")
	fi
}

AddEnvIfSet APT_PROXY
AddEnvIfSet LLVMBuildStage
AddEnvIfSet LLVM_LINUX_CURSES_BACKEND
AddEnvIfSet LLVMNinjaTimeoutSeconds
AddEnvIfSet LLVMNinjaTimeoutDeadlineSeconds
AddEnvIfSet LLVM_PARALLEL_LINK_JOBS
AddEnvIfSet LLVM_SWIG_EXECUTABLE
AddEnvIfSet LLVM_SWIG_VENV_DIR
AddEnvIfSet LLVM_SWIG_VERSION
AddEnvIfSet MalterlibLLVMArch
AddEnvIfSet MalterlibLLVMBuildRoot

echo "Running LLVM distribution build in Docker image: $ImageName"
echo "Mounted source root: $SourceRoot"
echo "Docker user: $DockerRunUser"
echo "BuildIncremental: ${BuildIncremental:-true}"

docker_cmd run --rm \
	"${DockerRunArgs[@]}" \
	"${DockerVolumeArgs[@]}" \
	-w "$ScriptDir" \
	"${DockerEnvArgs[@]}" \
	"$ImageName" \
	bash -lc 'set -euo pipefail; git config --global --add safe.directory "$MALTERLIB_ROOT" 2>/dev/null || true; cd "$MALTERLIB_SCRIPT_DIR"; exec ./BuildLLVMDistribution.sh "$@"' \
	bash "$@"
