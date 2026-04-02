#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e

ScriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

pushd "$ScriptDir/../../.."
source BuildSystem/SharedBuildSettings.sh

echo Copying files

PackageDirectory="$TempDirectory/Package"

MTool BuildServerGet "Source=BuildResults/*" "DestinationDir=$PackageDirectory"

pushd "$PackageDirectory"

tar -czf "$TempDirectory/MTool.tar.gz" .

UploadToVersionManagerWithInfo "$TempDirectory/MTool.tar.gz" MTool All

popd
