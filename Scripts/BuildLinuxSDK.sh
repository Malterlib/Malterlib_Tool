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

sudo apt install -y build-essential cmake ninja-build python3-distutils libacl1-dev libext2fs-dev libudev-dev libssl-dev uuid-dev libdbus-1-dev libsecret-1-dev libxcb-xinerama0-dev libunity-dev libxkbcommon-x11-dev libxkbcommon-dev
sudo apt-get build-dep qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools qt5dxcb-plugin -y

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
	<key>CustomProperties</key>
	<dict>
		<key>KERNEL_EXTENSION_HEADER_SEARCH_PATHS</key>
		<string>\$(KERNEL_FRAMEWORK)/PrivateHeaders \$(KERNEL_FRAMEWORK_HEADERS)</string>
	</dict>
	<key>DefaultProperties</key>
	<dict>
		<key>MACOSX_DEPLOYMENT_TARGET</key>
		<string>2.6</string>
		<key>PLATFORM_NAME</key>
		<string>macosx</string>
	</dict>
	<key>DisplayName</key>
	<string>Linux</string>
	<key>MinimalDisplayName</key>
	<string>2.6</string>
	<key>MinimumSupportedToolsVersion</key>
	<string>3.2</string>
	<key>SupportedBuildToolComponents</key>
	<array>
		<string>com.apple.compilers.gcc.headers.4_2</string>
	</array>
	<key>Version</key>
	<string>2.6</string>
	<key>isBaseSDK</key>
	<string>YES</string>
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

