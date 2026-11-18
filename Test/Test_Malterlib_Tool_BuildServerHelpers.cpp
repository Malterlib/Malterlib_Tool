// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Test/Test>

#include "../Apps/MTool/Source/Malterlib_Tool_App_MTool_BuildServerHelpers.h"

namespace NMib::NTool
{
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NTest;

	struct CBuildServerHelpers_Tests : CTest
	{
		void f_DoTests();
	};

	void CBuildServerHelpers_Tests::f_DoTests()
	{
		DMibTestSuite("ProtectedDirs")
		{
			TCVector<CStr> ProtectedDirectories = {"/Build/Shared", "/Build/Tools/"};

			auto fCheck = [&](CStr const &_Case, CStr const &_Path, bool _bExpected)
				{
					DMibTestPath(_Case);
					DMibExpect(fg_IsInProtectedDirectory(_Path, ProtectedDirectories), ==, _bExpected);
				}
			;

			fCheck("Directory", "/Build/Shared", true);
			fCheck("Below", "/Build/Shared/Libraries/Lib", true);
			fCheck("OtherCase", "/build/SHARED/Libraries", true);
			fCheck("EntryWithSeparator", "/Build/Tools", true);
			fCheck("BelowEntryWithSeparator", "/Build/Tools/Compiler", true);
			fCheck("SharedNamePrefix", "/Build/SharedOther", false);
			fCheck("SharedNamePrefixBelow", "/Build/SharedOther/Libraries", false);
			fCheck("Parent", "/Build", false);
			fCheck("Unrelated", "/Source/Shared", false);

			DMibTestCategory("Win32PathNames")
			{
				TCVector<CStr> WindowsDirectories = {"C:/Build/Shared", "C:/Build/Tools./"};

				auto fCheckWin32 = [&](CStr const &_Case, CStr const &_Path, bool _bWin32PathNames, bool _bExpected)
					{
						DMibTestPath(_Case);
						DMibExpect(fg_IsInProtectedDirectory(_Path, WindowsDirectories, _bWin32PathNames), ==, _bExpected);
					}
				;

				fCheckWin32("TrailingPeriod", "C:/Build/Shared.", true, true);
				fCheckWin32("TrailingPeriodsAndSpaces", "C:/Build/Shared. .", true, true);
				fCheckWin32("EntryWithTrailingPeriod", "C:/Build/Tools", true, true);
				fCheckWin32("SharedNamePrefix", "C:/Build/Shared.Other", true, false);
				fCheckWin32("IntermediateTrailingPeriod", "C:/Build./Shared/Libraries", true, true);
				fCheckWin32("IntermediateTwoTrailingPeriods", "C:/Build../Shared", true, false);
				fCheckWin32("IntermediateThreeTrailingPeriods", "C:/Build.../Shared", true, false);
				fCheckWin32("IntermediateOnlyPeriods", "C:/.../Build/Shared", true, false);
				fCheckWin32("IntermediateTrailingSpace", "C:/Build /Shared", true, false);
				fCheckWin32("LastNameTwoTrailingPeriods", "C:/Build/Shared..", true, true);
				fCheckWin32("LastNamePeriodAndSpace", "C:/Build/Shared. ", true, true);
				fCheckWin32("LastNameOnlyPeriodAndSpace", "C:/Build/Shared/. ", true, true);
				fCheckWin32("LastNameOnlyPeriodsIsParent", "C:/Build/...", true, false);
				fCheckWin32("ExpandedTrailingSeparatorKeepsSpace", "C:/Build/Shared //", true, false);
				fCheckWin32("SingleTrailingSeparatorIsExpandedAway", "C:/Build/Shared /", true, true);
				fCheckWin32("TrailingSeparatorSinglePeriod", "C:/Build/Shared./", true, true);
				fCheckWin32("PeriodIsNameOnPosix", "C:/Build/Shared.", false, false);
				fCheckWin32("IntermediatePeriodIsNameOnPosix", "C:/Build./Shared", false, false);
				fCheckWin32("ExtendedLengthKeepsPeriods", "C:/Build/Tools./Bin", false, true);
			};

			DMibTestCategory("ExtendedLengthPaths")
			{
				CStr LongName;
				LongName.f_AddChars('L', 260);

				auto fCheckLong = [&](CStr const &_Case, CStr const &_Path, CStr const &_ProtectedDirectory, bool _bExpected)
					{
						DMibTestPath(_Case);
						DMibExpect(fg_IsInProtectedDirectory(_Path, {_ProtectedDirectory}, true), ==, _bExpected);
					}
				;

				fCheckLong("LongPathBelowWin32Directory", "C:/Build/Shared/" + LongName, "C:/Build/Shared.", true);
				fCheckLong("LongPathPeriodIsName", "C:/Build/Shared./" + LongName, "C:/Build/Shared", false);
				fCheckLong("LongDirectoryKeepsPeriods", "C:/Build/" + LongName + "../Bin", "C:/Build/" + LongName + "..", true);
				fCheckLong("LongDirectoryPeriodsAreName", "C:/Build/" + LongName + "/Bin", "C:/Build/" + LongName + "..", false);

				// The file API measures the expanded path, which keeps one trailing separator
				CStr Base = "C:/Build/";
				Base.f_AddChars('S', 259 - 8 - Base.f_GetLen());
				Base += "Symbols.";
				DMibAssert(Base.f_GetLen(), ==, 259);
				fCheckLong("TrailingSeparatorsMakeLong", Base.f_Left(258), Base + "//", false);
				fCheckLong("ExpandedBeforeMeasuring", "C:/Build/Shared", "C:/Build/./Shared.", true);
			};

			DMibTestCategory("ExtendedLengthPrefix")
			{
				CStr Name;
				Name.f_AddChars('N', 243);
				CStr Directory = "C:/Build/" + Name;

				auto fCheckPrefixed = [&](CStr const &_Case, CStr const &_Path, CStr const &_ProtectedDirectory, bool _bExpected)
					{
						DMibTestPath(_Case);
						DMibExpect(fg_IsInProtectedDirectory(_Path, {_ProtectedDirectory}, true), ==, _bExpected);
					}
				;

				fCheckPrefixed("ShortAfterPrefixTrimsPeriod", "//?/" + Directory + "./Bin", "//?/" + Directory, true);
				fCheckPrefixed("PrefixMatchesUnprefixed", "//?/" + Directory + "/Bin", Directory, true);
				fCheckPrefixed("PrefixedUncMatchesUnc", "//?/UNC/server/Shared/Cache./Job", "//server/Shared/Cache", true);
				fCheckPrefixed("LongAfterPrefixKeepsPeriod", "//?/" + Directory + Name + "./Bin", "//?/" + Directory + Name, false);
				fCheckPrefixed("ExpandedAgainAfterPrefix", "//?/C:/Build/Shared //", "//?/C:/Build/Shared", true);
				fCheckPrefixed("UncExpandedAgainAfterPrefix", "//?/UNC/server/Shared/Cache //", "//server/Shared/Cache", true);
			};

			DMibTestCategory("UncRoot")
			{
				auto fCheckUnc = [&](CStr const &_Case, CStr const &_Path, bool _bExpected)
					{
						DMibTestPath(_Case);
						DMibExpect(fg_IsInProtectedDirectory(_Path, {"//server/Shared/Cache"}, true), ==, _bExpected);
					}
				;

				fCheckUnc("Below", "//server/Shared/Cache/Job", true);
				fCheckUnc("DirectoryTrailingPeriod", "//server/Shared/Cache./Job", true);
				fCheckUnc("SharePeriodIsName", "//server/Shared./Cache/Job", false);
				fCheckUnc("ServerPeriodIsName", "//server./Shared/Cache/Job", false);
				fCheckUnc("ShareOnlyPeriodIsName", "//server/Shared.", false);
				DMibExpectTrue(fg_IsInProtectedDirectory("//server/Shared./Job", {"//server/Shared."}, true));
			};

			{
				DMibTestPath("Root");
				DMibExpectTrue(fg_IsInProtectedDirectory("/Source", {"/"}));
			}

			{
				DMibTestPath("RelativeEntry");
				CStr CurrentDirectory = NFile::CFile::fs_GetCurrentDirectory();
				DMibExpectFalse(fg_IsInProtectedDirectory(CurrentDirectory / "Symbols", {"Symbols"}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory(CurrentDirectory / "Symbols", {"Symbols"}, false));
				DMibExpectFalse(fg_IsInProtectedDirectory(CurrentDirectory / "Symbols", {"./Symbols"}, true));
			}

			{
				DMibTestPath("ArgumentExpandedOnce");
				DMibExpectFalse(fg_IsInProtectedDirectory("C:/Build/Shared //", {"C:/Build/Shared"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:/Build/Shared //", {"C:/Build/Shared //"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("Symbols", {NFile::CFile::fs_GetCurrentDirectory() / "Symbols/"}, false));
				DMibExpectTrue(fg_IsInProtectedDirectory("Symbols/Job", {NFile::CFile::fs_GetCurrentDirectory() / "Symbols/"}, true));
			}

			{
				DMibTestPath("EntryTrailingSeparatorExpandedAway");
				DMibExpectFalse(fg_IsInProtectedDirectory("C:/Build/Shared /Job", {"C:/Build/Shared ./"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:/Build/Shared/Job", {"C:/Build/Shared ./"}, true));
			}

			{
				DMibTestPath("DriveRelativeEntry");
				DMibExpectFalse(fg_IsInProtectedDirectory("C:Work/Shared", {"C:Work/Shared/"}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory("C:Work/Shared/Job", {"C:Work/Shared"}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory("C:", {"C:"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:/Work/Shared/Job", {"C:\\Work\\Shared"}, true));
			}

			{
				DMibTestPath("DriveRelativeArgument");
				DMibExpectFalse(fg_IsInProtectedDirectory("C:", {"C:/"}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory("C:.", {"C:/"}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory("C:Shared", {"C:/"}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory("C:", {"C:/"}, false));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:/", {"C:/"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:\\", {"C:/"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:/Build/..", {"C:/"}, true));
				DMibExpectTrue(fg_IsInProtectedDirectory("C:/Shared", {"C:/"}, true));
			}

			{
				DMibTestPath("EmptyEntry");
				DMibExpectFalse(fg_IsInProtectedDirectory("/Source", {""}));
			}

			{
				DMibTestPath("EntryNormalizedToNothing");
				DMibExpectFalse(fg_IsInProtectedDirectory("//server/Shared/Job", {""}, true));
				DMibExpectFalse(fg_IsInProtectedDirectory("//server/Shared/Job", {"..."}, true));
			}

			DMibTestCategory("ForwardPath")
			{
				auto fCheckForward = [&](CStr const &_Case, CStr const &_Param, bool _bWin32PathNames, CStr const &_Expected)
					{
						DMibTestPath(_Case);
						DMibExpect(fg_ProtectedDirectoryForwardPath(_Param, _bWin32PathNames), ==, _Expected);
					}
				;

				CStr CurrentDirectory = NFile::CFile::fs_GetCurrentDirectory();
				// Expansion adds the current drive to a path from the root where file systems have drives
				CStr Drive = NFile::CFile::fs_GetExpandedPath(CStr("/"), true).f_TrimRight("/");

				fCheckForward("Win32ChangesNames", "C:/Build./Symbols", true, "C:/Build/Symbols");
				fCheckForward("Win32KeepsNames", "C:/Build/Symbols", true, "C:/Build/Symbols");
				fCheckForward("Win32RelativeIsAbsolute", "Symbols", true, CurrentDirectory / "Symbols");
				fCheckForward("RelativeIsAbsolute", "Symbols", false, CurrentDirectory / "Symbols");
				fCheckForward("RelativeTrailingSeparator", "Symbols/", false, (CurrentDirectory / "Symbols") + "/");
				fCheckForward("Win32RelativeTrailingSeparator", "Symbols/", true, (CurrentDirectory / "Symbols") + "/");
				fCheckForward("RelativeComponentsResolved", "/Build/./Tools/../Symbols", false, Drive + "/Build/Symbols");
				fCheckForward("PosixNamesAreLiteral", "/Build./Symbols", false, Drive + "/Build./Symbols");
#ifndef DPlatformFamily_Windows
				fCheckForward("Root", "/", false, "/");
#endif
				fCheckForward("DriveRoot", "C:/", true, "C:/");
				fCheckForward("DriveRootFromParent", "C:/Build/..", true, "C:/");
				fCheckForward("DriveRootFromParentPosixNames", "C:/Build/..", false, "C:/");
				fCheckForward("ExtendedLengthKeepsPrefix", "//?/C:/Build/Shared", true, "\\\\?\\C:\\Build\\Shared");
				fCheckForward("ExtendedLengthNativeSeparators", "\\\\?\\C:\\Build\\Shared", true, "\\\\?\\C:\\Build\\Shared");
				fCheckForward("ExtendedLengthUNCKeepsPrefix", "//?/UNC/server/share/Build", true, "\\\\?\\UNC\\server\\share\\Build");
				fCheckForward("ExtendedLengthPosixNamesAreLiteral", "//?/C:/Build/Shared", false, "//?/C:/Build/Shared");
				{
					DMibTestPath("ExtendedLengthLiteralWindowsNamesKeepPrefix");
					DMibExpect(fg_ProtectedDirectoryForwardPath("//?/C:/Build/Shared", false, true), ==, "\\\\?\\C:\\Build\\Shared");
					DMibExpect(fg_ProtectedDirectoryForwardPath("//?/C:/Build/Shared./Job", false, true), ==, "\\\\?\\C:\\Build\\Shared.\\Job");
				}
				fCheckForward("ExtendedLengthChangedNamesLosePrefix", "//?/C:/Build./Shared", true, "C:/Build/Shared");

				// The forwarded path is expanded again by the tool, so the separator that keeps the trailing space is doubled
				fCheckForward("ExpandedTrailingSeparatorKeepsSpace", "C:/Build/Shared //", true, "C:/Build/Shared //");
				fCheckForward("Win32ChangesNamesKeepsSpace", "C:/Build./Shared //", true, "C:/Build/Shared //");
				fCheckForward("SingleTrailingSeparatorIsExpandedAway", "C:/Build./Shared /", true, "C:/Build/Shared/");

				for
				(
					auto const &Param :
					{
						CStr("C:/Build/Shared //")
						, CStr("C:/Build./Shared //")
						, CStr("/Build/./Symbols/")
						, CStr("//?/C:/Build/Shared //")
						, CStr("//?/C:/Build/Shared")
						, CStr("//?/C:/Build./Shared")
						, CStr("\\\\?\\C:\\Build\\Shared")
						, CStr("//?/UNC/server/share/Build")
						, CStr("C:/Build/..")
					}
				)
				{
					DMibTestPath(Param);
					CStr Forwarded = fg_ProtectedDirectoryForwardPath(Param, true);
					DMibExpect(fg_ProtectedDirectoryComparePath(Forwarded, true), ==, fg_ProtectedDirectoryComparePath(Param, true));
				}
			};
		};
	}

	DMibTestRegister(CBuildServerHelpers_Tests, Malterlib::Tool);
}
