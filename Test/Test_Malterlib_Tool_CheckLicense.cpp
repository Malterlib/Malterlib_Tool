// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		struct CLicenseFixture : CRepositoryFixture
		{
			TCFuture<void> f_Check(CStr _Case, uint32 _Expected = 0, bool _bFix = false)
			{
				auto Capture = co_await (g_CaptureExceptions % "Checking fixture licenses");
				DMibTestPath(_Case);

				TCVector<CStr> Params =
					{
						"check-license"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, m_Path / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
				;
				if (_bFix)
					Params.f_Insert("--fix");

				auto Result = co_await f_Tool(fg_Move(Params), true);
				DMibExpect(Result.m_ExitCode, ==, _Expected);
				if (Result.m_ExitCode != _Expected)
					co_return DMibErrorInstance(Result.f_GetCombinedOut());

				co_return {};
			}

			TCFuture<void> f_InitLicense()
			{
				auto Capture = co_await (g_CaptureExceptions % "Initializing license fixture");
				co_await f_Init();
				f_Write("upstream/single.txt", "License text\nSecond line\n");
				f_Write("upstream/first.txt", "First license\n");
				f_Write("upstream/second.txt", "Second license\n");

				CEJsonOrdered License =
					{
						"Header"_o= _o["Copyright fixture"]
						, "Include"_o= _o["checked.cpp"]
						, "LicenseFiles"_o=
						{
							"LICENSE"_o= m_Path / "upstream/single.txt"
							, "BUNDLE"_o= _o
							[
								CEJsonOrdered{"Source"_o= m_Path / "upstream/first.txt"}
								, CEJsonOrdered{"Source"_o= m_Path / "upstream/second.txt", "Name"_o= "Dependency"}
							]
						}
						, "ReuseAnnotations"_o= _o
						[
							CEJsonOrdered{"Path"_o= _o["data/*"], "CopyrightText"_o= "Fixture", "LicenseIdentifier"_o= "MIT"}
						]
					}
				;

				// A self-contained repository policy keeps this test independent of a Core checkout.
				CStr Configuration = R"(Repository
{
	CheckLicense: bool = false
	License: {
		Header: [string]
		, Include: [string]
		, LicenseFiles: {...: one_of(string, [{Source: string, Name?: string}])}
		, ReuseAnnotations: [{Path: [string], CopyrightText: string, LicenseIdentifier: string}]
	}?
}
%Repository "."
{
	Repository
	{
		Type "Root"
		DefaultBranch "main"
		URL "https://example.invalid/license-fixture.git"
		CheckLicense true
)";
				Configuration += "\t\tLocation " + CEJsonSorted(m_Path).f_ToString() + "\n";
				Configuration += "\t\tLicense " + License.f_ToString() + "\n\t}\n}\n";
				f_Write("Test.MBuildSystem", Configuration);
				co_await f_Check("CreateFiles", 0, true);

				for (auto pName : {"LICENSE", "BUNDLE", "REUSE.toml"})
					m_Canonical[pName] = CFile::fs_ReadStringFromFile(m_Path / pName, true);

				DMibExpect(m_Canonical["LICENSE"], ==, CStr("License text\nSecond line\n"));
				DMibExpect(m_Canonical["BUNDLE"], ==, CStr("First license\n\n--- Dependency ---\n\nSecond license\n"));

				co_return {};
			}

			TCMap<CStr, CStr> m_Canonical;
		};
	}

	struct CCheckLicense_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("LineEndings") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing license line endings");

				DMibTestCategory("EquivalentLineEndings") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "EquivalentLineEndings");

					CLicenseFixture Repo;
					co_await Repo.f_InitLicense();

					for (auto pEnding : {"\r\n", "\r"})
					{
						DMibTestPath(CStr(pEnding) == "\r\n" ? "CRLF" : "CR");
						for (auto const &Entry : Repo.m_Canonical.f_Entries())
							Repo.f_Write(Entry.f_Key(), Entry.f_Value().f_Replace("\n", pEnding));

						co_await Repo.f_Check("Compare");
					}

					co_return {};
				};

				DMibTestCategory("MixedSourcesAndSeparators") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "MixedSourcesAndSeparators");

					CLicenseFixture Repo;
					co_await Repo.f_InitLicense();
					TCMap<CStr, CStr> Sources =
						{
							{"upstream/single.txt", "License text\r\nSecond line\r"}
							, {"upstream/first.txt", "First license\r"}
							, {"upstream/second.txt", "Second license\r\n"}
						}
					;
					for (auto const &Entry : Sources.f_Entries())
						Repo.f_Write(Entry.f_Key(), Entry.f_Value());

					co_await Repo.f_Check("Compare");
					for (auto const &Entry : Repo.m_Canonical.f_Entries())
						CFile::fs_DeleteFile(Repo.m_Path / Entry.f_Key());
					co_await Repo.f_Check("Regenerate", 0, true);

					for (auto const &Entry : Repo.m_Canonical.f_Entries())
					{
						DMibTestPath(Entry.f_Key());
						DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / Entry.f_Key(), true), ==, Entry.f_Value());
					}
					for (auto const &Entry : Sources.f_Entries())
					{
						DMibTestPath(Entry.f_Key());
						DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / Entry.f_Key(), true), ==, Entry.f_Value());
					}

					co_return {};
				};

				DMibTestCategory("RealContentChanges") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "RealContentChanges");

					CLicenseFixture Repo;
					co_await Repo.f_InitLicense();
					Repo.f_Write("LICENSE", "License text \r\nSecond line\r\n");

					co_await Repo.f_Check("WrongLicense", 1);
					co_await Repo.f_Check("FixLicense", 0, true);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "LICENSE", true), ==, Repo.m_Canonical["LICENSE"]);

					Repo.f_Write("REUSE.toml", Repo.m_Canonical["REUSE.toml"].f_Replace("MIT", "Apache-2.0"));
					co_await Repo.f_Check("WrongReuse", 1);
					co_await Repo.f_Check("FixReuse", 0, true);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "REUSE.toml", true), ==, Repo.m_Canonical["REUSE.toml"]);

					co_return {};
				};

				DMibTestCategory("PreserveMatchingCRLF") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "PreserveMatchingCRLF");

					CLicenseFixture Repo;
					co_await Repo.f_InitLicense();
					for (auto const &Entry : Repo.m_Canonical.f_Entries())
						Repo.f_Write(Entry.f_Key(), Entry.f_Value().f_Replace("\n", "\r\n"));

					co_await Repo.f_Check("Compare");
					co_await Repo.f_Check("Fix", 0, true);

					for (auto const &Entry : Repo.m_Canonical.f_Entries())
					{
						DMibTestPath(Entry.f_Key());
						DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / Entry.f_Key(), true), ==, Entry.f_Value().f_Replace("\n", "\r\n"));
					}

					co_return {};
				};

				co_return {};
			};

			DMibTestSuite("PerforceRoot") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing check-license in a Perforce root");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_MakePerforceRoot("P4CLIENT=Offline\n");
				Repo.f_Write(".p4ignore", "/Ignored/\n");
				Repo.f_Write("Missing.cpp", "int a;\n");
				Repo.f_Write("Ignored/Missing.cpp", "int a;\n");
				Repo.f_Write("Unknown/Missing.cpp", "int a;\n");
				auto UnknownInit = co_await Repo.f_Run("git", {"init", "-q", "-b", "main", Repo.m_Path / "Unknown"}, Repo.m_Path);
				DMibAssert(UnknownInit.m_ExitCode, ==, 0u);

				auto fConfigure = [&](CStr const &_PerforceRoot)
					{
						Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot " + _PerforceRoot + "\n}\n");
					}
				;

				auto fCheck = [&](bool _bFix) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						TCVector<CStr> Params =
							{
								"check-license"
								, "--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						;
						if (_bFix)
							Params.f_Insert("--fix");

						return Repo.f_Tool(fg_Move(Params), true);
					}
				;

				fConfigure("{CheckLicense: true, License: {Header: [\"Copyright fixture\"], Include: [\"*.cpp\"]}}");

				{
					DMibTestPath("Check");
					auto Result = co_await fCheck(false);
					DMibExpect(Result.m_ExitCode, !=, 0u);
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("Missing header: Missing.cpp"), >=, 0);
					DMibExpect(Output.f_Find("Ignored"), <, 0);
					DMibExpect(Output.f_Find("Unknown"), <, 0);
				}

				{
					DMibTestPath("Fix");
					auto Fixed = co_await fCheck(true);
					DMibExpect(Fixed.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Missing.cpp", true).f_Find("Copyright fixture"), >=, 0);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Ignored/Missing.cpp", true), ==, "int a;\n");
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Unknown/Missing.cpp", true), ==, "int a;\n");

					auto Result = co_await fCheck(false);
					DMibExpect(Result.m_ExitCode, ==, 0u);
				}

				{
					DMibTestPath("NotOptedIn");
					Repo.f_Write("Missing.cpp", "int a;\n");
					fConfigure("{CheckLicense: false, License: {Header: [\"Copyright fixture\"], Include: [\"*.cpp\"]}}");
					auto Result = co_await fCheck(false);
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Missing header"), <, 0);
				}

				{
					// The default Exclude of CLicenseConfig keeps REUSE.toml and LICENSE out.
					DMibTestPath("DefaultExclude");
					Repo.f_Write("LICENSE", "License text\n");
					fConfigure
						(
							"{CheckLicense: true, License: {Header: [\"Copyright fixture\"], Include: [\"*.cpp\", \"*.toml\", \"LICENSE\"]"
							", ReuseAnnotations: [{Path: [\"Missing.cpp\"], CopyrightText: \"Fixture\", LicenseIdentifier: \"MIT\"}]}}"
						)
					;
					auto Fixed = co_await fCheck(true);
					DMibExpect(Fixed.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_FileExists(Repo.m_Path / "REUSE.toml"), ==, true);

					auto Result = co_await fCheck(false);
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Missing header"), <, 0);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "LICENSE", true), ==, "License text\n");
				}

				co_return {};
			};
		}
	};

	DMibTestRegister(CCheckLicense_Tests, Malterlib::Tool);
}
