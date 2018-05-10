#!/bin/bash

# Copyright © 2015 Hansoft AB
# Distributed under the MIT license, see license text in LICENSE.Malterlib

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
