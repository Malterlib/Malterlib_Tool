// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CUserSettings_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("UserSettings") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// FromA is declared in A, which isn't cloned before the first update
				CStr Extra = "Property\n{\n\tMalterlibStrictUserSettings: bool = false\n\tDeclared: bool = false\n";
				Extra += "\tDocumented: bool = false // User(Test)=true: A documented setting\n}\n";
				Extra += "Import \"Repos/*/Settings.MSettings\"\n";

				CCheckoutFixture Checkout(Repo);
				co_await Checkout.f_Init({"A"}, Extra);
				Checkout.m_bUseUserSettings = true;

				CStr WorkA = Repo.m_Root / "work/A";
				CFile::fs_WriteStringToFile(WorkA / "Settings.MSettings", "Property\n{\n\tFromA: bool = false\n}\n", false);
				co_await Checkout.f_GitIn(WorkA, {"add", "-A"});
				co_await Checkout.f_GitIn(WorkA, {"-c", "core.hooksPath=" + (Repo.m_Root / "no-hooks"), "commit", "-qm", "Settings"});
				co_await Checkout.f_GitIn(WorkA, {"push", "-q", Checkout.f_Remote("A"), "main"});

				CStr UserSettingsFile = Repo.m_Path / "BuildSystem/Default/UserSettings.MSettings";
				auto fUserSettings = [&](CStr const &_Properties)
					{
						CFile::fs_CreateDirectoryForFile(UserSettingsFile);
						CFile::fs_WriteStringToFile(UserSettingsFile, "Property\n{\n" + _Properties + "}\n", false);
					}
				;

				DMibTestCategory("FreshCheckout") -> TCFuture<void>
				{
					// Checked once nothing is left to update, when A, which declares FromA, is on disk
					fUserSettings("\tMalterlibStrictUserSettings true\n\tDeclared true\n\tFromA true\n");
					co_await Checkout.f_MibSucceeds({"update-repos"});
					DMibExpectTrue(CFile::fs_FileExists(Checkout.f_Repository("A") / "Settings.MSettings"));

					co_return {};
				};

				DMibTestCategory("Undeclared") -> TCFuture<void>
				{
					fUserSettings("\tMalterlibStrictUserSettings true\n\tDeclared true\n\tMisspeled true\n\tAlsoMisspeled true\n");
					auto Result = co_await Checkout.f_Mib({"list-repos"});
					DMibExpect(Result.m_ExitCode, !=, 0);

					CStr Output = Result.f_GetCombinedOut();
					auto fNamed = [&](umint _Line, CStr const &_Property)
						{
							for (auto &Line : Output.f_SplitLine())
							{
								CStr Trimmed = Line.f_Trim();
								CStr Prefix = "{}:{}:"_f << UserSettingsFile << _Line;
								CStr Suffix = ": Property.{} is not a setting the build system declares"_f << _Property;
								if (Trimmed.f_StartsWith(Prefix) && Trimmed.f_EndsWith(Suffix))
									return true;
							}

							return false;
						}
					;
					DMibExpectTrue(fNamed(5, "Misspeled"));
					DMibExpectTrue(fNamed(6, "AlsoMisspeled"));
					DMibExpect(Output.f_Find("Property.Declared is not"), <, 0);

					co_return {};
				};

				DMibTestCategory("NotStrict") -> TCFuture<void>
				{
					fUserSettings("\tDeclared true\n\tMisspeled true\n");
					co_await Checkout.f_MibSucceeds({"list-repos"});

					co_return {};
				};

				DMibTestCategory("GlobalUserSettingsDisabled") -> TCFuture<void>
				{
					// Generation adds the documented settings to the local file, and leaves the global one in the home directory as it is
					CStr GlobalFile = CFile::fs_GetUserHomeDirectory() / "UserSettingGlobalV2.MSettings";
					bool bGlobalExists = CFile::fs_FileExists(GlobalFile);
					CStr GlobalBefore = bGlobalExists ? CFile::fs_ReadStringFromFile(GlobalFile, true) : CStr();

					fUserSettings("\tDeclared true\n");
					co_await Checkout.f_MibSucceeds({"generate"});

					CStr Local = CFile::fs_ReadStringFromFile(UserSettingsFile, true);
					DMibExpect(Local.f_Find("//Documented true // A documented setting"), >=, 0);

					bool bGlobalExistsAfter = CFile::fs_FileExists(GlobalFile);
					DMibExpect(bGlobalExistsAfter, ==, bGlobalExists);
					if (bGlobalExists && bGlobalExistsAfter)
					{
						CStr GlobalAfter = CFile::fs_ReadStringFromFile(GlobalFile, true);
						DMibExpectTrue(GlobalAfter == GlobalBefore);
					}

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CUserSettings_Tests, Malterlib::Tool);
}
