// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>

#include "Malterlib_Tool_TestPerforceHelpers.h"

#include <Mib/Perforce/TestServer>

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CGeneratePerforce_Tests : CTest
	{
		void f_DoTests();
	};

	void CGeneratePerforce_Tests::f_DoTests()
	{
		// mib generate opens a read-only file of a Perforce workspace for edit before it replaces it. A file Perforce keeps opened
		// for delete stops the generation and is left as it is, as does one whose open state can't be told once p4 edit has run,
		// while one p4 edit can't open, as one outside the depot, loses its write protection instead.
		DMibTestSuite("GeneratePerforce") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing mib generate in a Perforce workspace");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of mib generate in a Perforce workspace: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Output/Edited.txt", "old\n"}, {"Output/Deleted.txt", "old\n"}, {"Output/Verified.txt", "old\n"}}
					, CStr("Add the generated files")
				)
			;

			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Workspace = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						Client.f_Delete(Workspace / "Output/Deleted.txt");
					}
					% "Failed to set up the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;

			// Read-only, as a file the workspace has not opened is: the synced file, the file opened for delete, back in the workspace,
			// and a file outside the depot.
			Repo.f_MakeReadOnly("Output/Edited.txt");
			Repo.f_MakeReadOnly("Output/Verified.txt");
			// The text with the line ends p4 writes in the workspace, as it wrote the synced files.
			CStr const OldText = NPerforce::CPerforceTestServer::fs_WorkspaceText("old\n");
			Repo.f_WriteWorkspaceText("Output/Deleted.txt", "old\n");
			Repo.f_MakeReadOnly("Output/Deleted.txt");
			Repo.f_Write("Output/Local.txt", "old\n");
			Repo.f_MakeReadOnly("Output/Local.txt");

			auto fGenerate = [&](TCVector<CStr> _Names) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
				{
					CStr BuildSystem = "*Configuration\n{\n\tDebug \"Debug\"\n}\n*Platform\n{\n\tTestPlatform \"Test platform\"\n}\n"
						"*Architecture\n{\n\tTestArch \"Test architecture\"\n}\n"
					;
					for (auto const &Name : _Names)
						BuildSystem += "%GenerateFile \"" + Name + "\"\n{\n\tGenerateFile\n\t{\n\t\tName `@(BuildSystemOutputDir)/" + Name + ".txt`\n\t\tContents \"new\"\n\t}\n}\n";
					Repo.f_Write("Test.MBuildSystem", BuildSystem);

					return Repo.f_Tool
						(
							{
								"generate"
								, "--no-color"
								, "--skip-update"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Path / "Output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
								, "--no-signal-changed"
							}
							, true
						)
					;
				}
			;

			auto fAction = [&](CStr _Name) -> TCUnsafeFuture<CStr>
				{
					co_return co_await
						(
							g_Dispatch(fg_BlockingActor()) / [Connection, Path = Repo.m_Path / _Name]() -> CStr
							{
								NPerforce::CPerforceClientThrow Client(Connection);
								Client.f_Login(CStr());
								return Client.f_FileStats(Path).m_Action;
							}
						)
					;
				}
			;

			{
				DMibTestPath("Edited");
				auto Result = co_await fGenerate({"Edited", "Local"});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find(CStr("Opened file for edit in Perforce: {}"_f << (Repo.m_Path / "Output/Edited.txt"))), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Output/Edited.txt", true).f_Find("new"), >=, 0);
				DMibExpect(co_await fAction("Output/Edited.txt"), ==, "edit");
				DMibExpect(Output.f_Find(CStr("Removed file write protection: {}"_f << (Repo.m_Path / "Output/Local.txt"))), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Output/Local.txt", true).f_Find("new"), >=, 0);
			}

			{
				DMibTestPath("OpenedForDelete");
				auto Result = co_await fGenerate({"Deleted"});
				DMibExpect(Result.m_ExitCode, !=, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("is opened for delete, which p4 edit leaves as it is"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Output/Deleted.txt", true), ==, OldText);
				DMibExpect(co_await fAction("Output/Deleted.txt"), ==, "delete");
			}

			{
				// A command trigger refuses the fstat that tells the action p4 edit left the file in.
				DMibTestPath("ActionUnknown");
				co_await fg_SetPerforceTriggers(Connection, "Triggers:\n" + fg_RefusingTrigger(Repo, "fstat", "Verified"));
				auto Result = co_await fGenerate({"Verified"});
				co_await fg_SetPerforceTriggers(Connection, "Triggers:\n");
				DMibExpect(Result.m_ExitCode, !=, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Refused fstat"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Output/Verified.txt", true), ==, OldText);
			}

			co_return {};
		};
	}

	DMibTestRegister(CGeneratePerforce_Tests, Malterlib::Tool);
}
