#!/bin/bash
set -e

DestinationDir=${1:-/LocalSource/SDKs}
Architecture=`uname -m`

if [[ "$Architecture" == "i686" ]] || [[ "$Architecture" == "i586" ]] || [[ "$Architecture" == "i486" ]] || [[ "$Architecture" == "i386" ]] ; then
	Architecture=i386
elif [[ "$Architecture" == "x86_64" ]]; then
	if [[ `getconf LONG_BIT` == "32" ]] ; then
		Architecture=i386
	fi
fi

export MalterlibImportUpdateCache=false

echo "DestinationDir: $DestinationDir"
echo "Architecture: $Architecture"

sudo apt install -y build-essential cmake ninja-build python3-distutils libacl1-dev \
	libext2fs-dev libudev-dev libssl-dev uuid-dev libdbus-1-dev libsecret-1-dev libxcb-xinerama0-dev \
	libunity-dev libxkbcommon-x11-dev libxkbcommon-dev libxcb-cursor0 libxcb-cursor-dev libxcb-util-dev \
	libxcb1-dev libx11-dev libc++-20-dev

sudo apt-get build-dep qtbase-opensource-src qtchooser -y

mkdir -p "$DestinationDir"
pushd "$DestinationDir"

mkdir -p Linux.sdk
pushd Linux.sdk

SDKDir="$PWD"

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
mkdir -p usr/lib

if ! [ -h /lib ] ; then
	cp -r "/lib/${Architecture}-linux-gnu/"* usr/lib/
fi

#mkdir -p "usr/lib/clang/"
#cp -r "../llvm-malterlib/build/main/lib/clang/"* "usr/lib/clang/"
ln -s usr/lib lib
ln -s usr/lib lib64
ln -s usr/lib lib32
ln -s usr/lib libx32

cp -r "/usr/lib/${Architecture}-linux-gnu/"* usr/lib/
cp -r /usr/lib/gcc usr/lib/

rm -fr usr/share/pkgconfig
mkdir -p usr/share/pkgconfig
cp -r /usr/share/pkgconfig usr/share/

rm -rf "usr/lib/libc++"*
rm -rf "usr/lib/libcunwind"*

#cp -r "../llvm-malterlib/build/main/lib/libc++"* "usr/lib/"
#cp -r "../llvm-malterlib/build/main/lib/libunwind"* "usr/lib/"

# Remove some big stuff
rm -rf usr/lib/guile
rm -rf usr/lib/aisleriot
rm -f usr/lib/gcc/x86_64-linux-gnu/*/lto*
rm -f usr/lib/gcc/x86_64-linux-gnu/*/cc*
rm -rf usr/lib/espeak-ng-data
rm -rf usr/lib/gedit/plugins
rm -rf usr/lib/rhythmbox/plugins
rm -rf usr/lib/gnome-software

# Remove executables
find . -type f -executable | grep -v '.*\.so\($\|\.\)' | xargs rm -f

# Remove cmake files
find . -name '*.cmake' | xargs rm -f

rm -rf usr/include
cp -r /usr/include usr/
rm -rf "usr/include/c++"
mkdir -p "usr/include/c++"
#cp -r "../llvm-malterlib/build/main/include/c++/"* "usr/include/c++/"

ln -s . usr/lib/${Architecture}-linux-gnu

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

# Strip elf files to only needed for linking
find . -type f -name '*.so' -exec objcopy -j .dynamic -j .dynsym -j .dynstr -j .symtab -j .strtab -j .shstrtab -j .gnu.version -j .gnu.version_d -j .gnu.version_r {} \; 2>&1 | sed '/^objcopy: .*warning: empty loadable segment detected at/d'
find . -type f -name '*.so.*' -exec objcopy -j .dynamic  -j .dynsym -j .dynstr -j .symtab -j .strtab -j .shstrtab -j .gnu.version -j .gnu.version_d -j .gnu.version_r {} \; 2>&1 | sed '/^objcopy: .*warning: empty loadable segment detected at/d'

