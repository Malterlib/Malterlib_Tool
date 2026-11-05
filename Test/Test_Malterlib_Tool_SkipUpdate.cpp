// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CSkipUpdate_Tests : CTest
	{
		void f_DoTests();
	};

	void CSkipUpdate_Tests::f_DoTests()
	{
		// The pins in the .MRepo files and the .MRepoState files, which an earlier generate's update wrote.
		DMibTestSuite("SkipUpdate") -> TCFuture<void>
		{
			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			CCheckoutFixture Checkout(Repo);
			co_await Checkout.f_Init
				(
					{"A", "B"}
					, "*Configuration\n{\n\tDebug \"Debug\"\n}\n*Platform\n{\n\tTestPlatform \"Test platform\"\n}\n*Architecture\n{\n\tTestArch \"Test architecture\"\n}\n"
				)
			;

			CStr ConfigFile = Repo.m_Path / "Repos.MRepo";
			CStr StateFile = Repo.m_Path / "Repos.MRepoState";

			co_await Checkout.f_MibSucceeds({"generate", "--no-signal-changed"});
			DMibAssertTrue(CFile::fs_FileExists(ConfigFile));
			DMibAssertTrue(CFile::fs_FileExists(StateFile));

			CStr Config = CFile::fs_ReadStringFromFile(ConfigFile, true);
			CStr State = CFile::fs_ReadStringFromFile(StateFile, true);

			// A generate that finds nothing changed leaves the state as it is
			umint nChanges = 0;
			auto fChangeBuildSystem = [&]()
				{
					CStr BuildSystem = CFile::fs_ReadStringFromFile(Repo.m_Path / "Test.MBuildSystem", true);
					Repo.f_Write("Test.MBuildSystem", BuildSystem + CStr("// Change {}\n"_f << ++nChanges));
				}
			;

			auto fExpectKept = [&](CStr const &_Case)
				{
					DMibTestPath(_Case);
					DMibExpectTrue(CFile::fs_FileExists(ConfigFile));
					DMibExpectTrue(CFile::fs_FileExists(StateFile));
					if (CFile::fs_FileExists(ConfigFile))
						DMibExpect(CFile::fs_ReadStringFromFile(ConfigFile, true), ==, Config);
					if (CFile::fs_FileExists(StateFile))
						DMibExpect(CFile::fs_ReadStringFromFile(StateFile, true), ==, State);
				}
			;

			{
				fChangeBuildSystem();
				CStr Output = co_await Checkout.f_MibSucceeds({"generate", "--skip-update", "--no-signal-changed"});
				fExpectKept("SkipUpdateOption");
				DMibExpect(Output.f_Find("unused file"), <, 0);
			}

			// Twice in a row: the first one's state keeps them too
			fChangeBuildSystem();
			co_await Checkout.f_MibSucceeds({"generate", "--skip-update", "--no-signal-changed"});
			fExpectKept("SkipUpdateTwice");

			{
				fChangeBuildSystem();
				Repo.m_Environment["Malterlib_SkipUpdate"] = "true";
				co_await Checkout.f_MibSucceeds({"generate", "--no-signal-changed"});
				Repo.m_Environment.f_Remove("Malterlib_SkipUpdate");
				fExpectKept("SkipUpdateEnvironment");
			}

			fChangeBuildSystem();
			co_await Checkout.f_MibSucceeds({"generate", "--no-signal-changed"});
			fExpectKept("UpdateAfterSkipped");

			co_return {};
		};
	}

	DMibTestRegister(CSkipUpdate_Tests, Malterlib::Tool);
}
