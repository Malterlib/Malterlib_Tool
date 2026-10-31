#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e

Clang=../../../Binaries/MalterlibLLVM/macOS/arm64/bin/clang++
Flags=(
	-target arm64-apple-macos10.7 -g
	-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
	-std=c++2b
)

echo
echo "-O0"
"$Clang" "${Flags[@]}" Tests/TestCoroutine.cpp
./a.out
echo

echo "-O3"
"$Clang" -O3 "${Flags[@]}" Tests/TestCoroutine.cpp
./a.out
echo
