#!/bin/bash
set -e

echo
echo "-O0"
../../../Binaries/MalterlibLLVM/OSX/arm64/bin/clang++ -target arm64-apple-macos10.7 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.3.sdk -std=c++20 -finstrument-non-coro-functions Tests/TestCoroutine.cpp
./a.out
echo

echo "-O3"
../../../Binaries/MalterlibLLVM/OSX/arm64/bin/clang++ -O3 -target arm64-apple-macos10.7 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.3.sdk -std=c++20 -finstrument-non-coro-functions Tests/TestCoroutine.cpp
./a.out
echo
