#!/bin/bash
set -e

echo
echo "-O0"
../../../Binaries/MalterlibLLVM/macOS/arm64/bin/clang++ -target arm64-apple-macos10.7 -g -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk -std=c++2b Tests/TestCoroutine.cpp
./a.out
echo

echo "-O3"
../../../Binaries/MalterlibLLVM/macOS/arm64/bin/clang++ -O3 -target arm64-apple-macos10.7 -g -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk -std=c++2b Tests/TestCoroutine.cpp
./a.out
echo
