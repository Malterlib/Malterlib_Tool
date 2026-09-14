// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CPostCopy_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("PrintDestination") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CStr Config = "Projects\n{\n\tTests\n\t{\n\t\tDestination \"excluded\"\n\t\t{\n\t\t\tTag \"Inactive\"\n\t\t}\n"
					"\t\tDestination \"custom tests\"\n\t\t{\n\t\t\tRename \"renamed runner\"\n\t\t}\n\t\tDestination \"second\"\n\t}\n}\n"
				;
				Repo.f_Write("PostCopy.MConfig", Config);

				auto Result = co_await Repo.f_Tool({"PostCopy", "missing-runner", "Config=" + (Repo.m_Path / "PostCopy.MConfig"), "Project=Tests", "PrintDestination=true"});

				DMibExpect(Result.m_ExitCode, ==, 0);
				DMibExpect(Result.f_GetStdOut().f_Trim(), ==, "custom tests/renamed runner");
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "PostCopy.MConfig"), ==, Config);
				DMibExpectFalse(CFile::fs_FileExists(Repo.m_Root / "custom tests"));

				Repo.m_Environment["MalterlibDeployRoot"] = Repo.m_Root / "default deploy";
				auto Default = co_await Repo.f_Tool({"PostCopy", "missing-runner", "Config=" + (Repo.m_Path / "missing.MConfig"), "Project=Tests", "PrintDestination=true"});

				DMibExpect(Default.m_ExitCode, ==, 0);
				DMibExpect(Default.f_GetStdOut().f_Trim(), ==, Repo.m_Root / "default deploy/Tests/missing-runner");
				DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / "missing.MConfig"));

				Repo.f_Write("PostCopy.MConfig", "Projects\n{\n\tTests\n\t{\n\t\tDestination \"excluded\"\n\t\t{\n\t\t\tTag \"Inactive\"\n\t\t}\n\t}\n}\n");
				auto Disabled = co_await Repo.f_Tool({"PostCopy", "missing-runner", "Config=" + (Repo.m_Path / "PostCopy.MConfig"), "Project=Tests", "PrintDestination=true"});

				DMibExpect(Disabled.m_ExitCode, !=, 0);
				DMibExpect(Disabled.f_GetCombinedOut().f_Find("No enabled destination"), >=, 0);

				Repo.f_Write
					(
						"PostCopy.MConfig"
						, "Projects\n{\n\tTests\n\t{\n\t\tDestination \"debug deploy\"\n\t\t{\n"
						"\t\t\tEnableIf \"/Debug/\"\n\t\t\tRename \"selected runner\"\n\t\t}\n\t}\n}\n"
					)
				;
				auto BySourcePath = co_await Repo.f_Tool
					(
						{"PostCopy", Repo.m_Root / "build/Debug/Deployed/RunAllTests", "Config=" + (Repo.m_Path / "PostCopy.MConfig"),
						"Project=Tests", "OutFolder=tools", "PrintDestination=true"}
					)
				;
				DMibExpect(BySourcePath.m_ExitCode, ==, 0);
				DMibExpect(BySourcePath.f_GetStdOut().f_Trim(), ==, "debug deploy/tools/selected runner");

				co_return {};
			};
		}
	};

	DMibTestRegister(CPostCopy_Tests, Malterlib::Tool);
}
