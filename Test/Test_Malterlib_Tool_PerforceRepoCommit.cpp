// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>

#include "Malterlib_Tool_TestPerforceHelpers.h"

#include <Mib/Perforce/TestServer>

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CPerforceRepoCommit_Tests : CTest
	{
		void f_DoTests();
	};

	void CPerforceRepoCommit_Tests::f_DoTests()
	{
		// repo-commit in a Perforce root refuses a config file opened for delete, which its changelist would submit as a delete,
		// before it changes any changelist: also where the file is in the changelist repo-commit made, which is otherwise up to
		// date, and where that changelist holds a foreign file, which stays in it. Pins with CRLF or CR in a workspace with LineEnd
		// win or mac are unchanged. Pins opened in another changelist go to its changelist, where they stay, also unchanged, and a
		// run with nothing changed leaves it as it is. A query of the pins that fails makes list-commits and repo-commit fail, and
		// pins a pending p4 delete removed are listed as a pending change. Where the file system ignores case the depot spells the
		// file in other case than the checkout does, on a server that ignores case, which tells the file by its own case rules.
		DMibTestSuite("ConfigFileOpenedForDelete") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing repo-commit with a config file opened for delete");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of repo-commit in a Perforce root: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			CCheckoutFixture Checkout(Repo);
			co_await Checkout.f_Init({"A"});

			bool bIgnoresCase = CFile::fs_FileExists(Repo.m_Path / "test.mbuildsystem", EFileAttrib_File);
			CStr DepotPins = bIgnoresCase ? "//Streams/Main/repos.mrepo" : "//Streams/Main/Repos.MRepo";

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			if (bIgnoresCase)
				co_await Server(&NPerforce::CPerforceTestServer::f_StartCaseInsensitive, Repo.m_Root / "Perforce", CStr("tester"));
			else
				co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);

			// A Perforce root is no git repository, so the build system has no root repository.
			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			CStr BuildSystem = CFile::fs_ReadStringFromFile(Repo.m_Path / "Test.MBuildSystem", true);
			Repo.f_Write("Test.MBuildSystem", BuildSystem.f_Extract(BuildSystem.f_Find("%Repository \"A\"")));
			co_await Checkout.f_MibSucceeds({"update-repos"});
			CStr Pins = CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			int64 FirstPinsChangelist = co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{CFile::fs_GetFile(DepotPins), Pins}, {"Foreign.txt", "a\n"}}
					, CStr("Add the pins")
				)
			;

			// The workspace syncs the pins it has already, which p4 doesn't write over.
			CFile::fs_DeleteFile(Repo.m_Path / "Repos.MRepo");
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
					}
					% "Failed to set up the test workspace"
				)
			;

			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;

			struct COpened
			{
				TCVector<CStr> m_Files; // "<Depot file> <action> <changelist>", the changelist repo-commit made as "ours"
				umint m_nPending = 0;
			};

			auto fOpened = [&](uint32 _OurChangelist) -> TCUnsafeFuture<COpened>
				{
					co_return co_await
						(
							g_Dispatch(fg_BlockingActor()) / [Connection, Workspace = Repo.m_Path, _OurChangelist]() -> COpened
							{
								NPerforce::CPerforceClientThrow Client(Connection);
								Client.f_Login(CStr());

								COpened Opened;
								TCVector<NPerforce::CPerforceClient::COpenedFile> Files;
								Client.f_NoThrow().f_GetOpenedFiles(Workspace / "...", CStr(), Files);
								for (auto &File : Files)
								{
									CStr Change = File.m_Change == CStr::fs_ToStr(_OurChangelist) ? CStr("ours") : File.m_Change;
									Opened.m_Files.f_Insert("{} {} {}"_f << File.m_DepotFile << File.m_Action << Change);
								}
								Opened.m_Files.f_Sort();
								Opened.m_nPending = Client.f_GetChangelists(CStr(), false, Client.f_GetActiveClient(), "pending").f_GetLen();

								return Opened;
							}
						)
					;
				}
			;

			{
				// A workspace with LineEnd win or mac has the submitted pins with CRLF or CR, as p4 writes them, which are no change of
				// them: list-commits selects the range between the two changelists that submitted them.
				DMibTestPath("CrlfPins");
				int64 SecondPinsChangelist = co_await Server
					(
						&NPerforce::CPerforceTestServer::f_SubmitFiles
						, CStr("//Streams/Main")
						, TCVector<CPerforceFile>{{CFile::fs_GetFile(DepotPins), Pins}}
						, CStr("Submit the pins again")
					)
				;
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Sync(Workspace / "...");
						}
						% "Failed to sync the pins"
					)
				;

				auto fSetLineEnd = [&](CStr _LineEnd, CStr _Pins) -> TCUnsafeFuture<void>
					{
						co_await
							(
								g_Dispatch(fg_BlockingActor())
								/ [Connection, Workspace = Repo.m_Path, _LineEnd, _Pins]()
								{
									NPerforce::CPerforceClientThrow Client(Connection);
									Client.f_Login(CStr());
									auto Spec = Client.f_GetClientScruct("TestWork");
									Spec.m_LineEndings = _LineEnd;
									Client.f_SetClient("TestWork", Spec);
									CFile::fs_WriteStringToFile(Workspace / "Repos.MRepo", _Pins, false);
								}
								% "Failed to set the workspace's line ends"
							)
						;

						co_return {};
					}
				;

				for (CStr LineEnd : {"win", "mac"})
				{
					DMibTestPath(LineEnd);
					co_await fSetLineEnd(LineEnd, Pins.f_Replace("\n", LineEnd == "win" ? "\r\n" : "\r"));
					auto ListCommits = co_await Checkout.f_Mib({"list-commits", "--local"});
					// A range that ends in the workspace has the unreconciled check look at the pins.
					auto ListCommitsToWorkspace = co_await Checkout.f_Mib({"list-commits", "--local", CStr("@{}"_f << FirstPinsChangelist)});
					auto RepoCommit = co_await Checkout.f_Mib({"repo-commit"});
					auto Opened = co_await fOpened(0);
					co_await fSetLineEnd("local", Pins);

					DMibExpect(ListCommits.m_ExitCode, ==, 0u);
					DMibExpect(ListCommits.f_GetCombinedOut().f_Find("Unreconciled"), <, 0);
					DMibExpect(ListCommits.f_GetCombinedOut().f_Find(CStr("From @{}"_f << FirstPinsChangelist)), >=, 0);
					DMibExpect(ListCommits.f_GetCombinedOut().f_Find(CStr("To   @{}"_f << SecondPinsChangelist)), >=, 0);
					DMibExpect(ListCommitsToWorkspace.m_ExitCode, ==, 0u);
					DMibExpect(ListCommitsToWorkspace.f_GetCombinedOut().f_Find("To   workspace"), >=, 0);
					DMibExpect(ListCommitsToWorkspace.f_GetCombinedOut().f_Find("Unreconciled"), <, 0);
					DMibExpect(RepoCommit.m_ExitCode, ==, 0u);
					DMibExpect(RepoCommit.f_GetCombinedOut().f_Find("Already up to date"), >=, 0);
					DMibExpect(Opened.m_Files, ==, TCVector<CStr>{});
					DMibExpect(Opened.m_nPending, ==, 0u);
				}
			}

			{
				// The pins opened in the default changelist, as they were submitted, go to the changelist repo-commit makes.
				DMibTestPath("PinInDefault");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_OpenForEdit(Workspace / "Repos.MRepo");
						}
						% "Failed to open the pins"
					)
				;

				auto Result = co_await Checkout.f_Mib({"repo-commit"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				auto Opened = co_await fOpened(0);
				DMibAssert(Opened.m_Files.f_GetLen(), ==, 1u);
				DMibExpect(Opened.m_Files[0].f_StartsWith(DepotPins + " edit "), ==, true);
				DMibExpect(Opened.m_Files[0].f_EndsWith(" default"), ==, false);
				DMibExpect(Opened.m_nPending, ==, 1u);

				// The unchanged pins stay in that changelist on reruns, rather than moving out and back.
				for (umint iRun = 0; iRun < 3; ++iRun)
				{
					DMibTestPath("Rerun {}"_f << iRun);
					auto Rerun = co_await Checkout.f_Mib({"repo-commit"});
					DMibExpect(Rerun.m_ExitCode, ==, 0u);
					auto Again = co_await fOpened(0);
					DMibExpect(Again.m_Files, ==, Opened.m_Files);
					DMibExpect(Again.m_nPending, ==, 1u);
				}
			}

			// A pin to update, which repo-commit opens for edit in a changelist of its own.
			co_await Checkout.f_CommitIn(Checkout.f_Repository("A"), "Change");
			co_await Checkout.f_MibSucceeds({"repo-commit"});
			CStr UpdatedPins = CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true);

			{
				// Run again with nothing changed, repo-commit finds its changelist up to date and leaves it as it is, every time.
				DMibTestPath("UnchangedRerun");
				auto Before = co_await fOpened(0);
				for (umint iRun = 0; iRun < 3; ++iRun)
				{
					DMibTestPath("Run {}"_f << iRun);
					auto Result = co_await Checkout.f_Mib({"repo-commit"});
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Already up to date"), >=, 0);
					auto After = co_await fOpened(0);
					DMibExpect(After.m_Files, ==, Before.m_Files);
					DMibExpect(After.m_nPending, ==, 1u);
				}
			}

			{
				// A query of the pins that fails isn't taken for pins without history or changelists: list-commits fails, also where only
				// the read of a revision or the changelists are refused, and repo-commit where its reads are.
				DMibTestPath("LookupFails");

				// A git ahead of the real one in PATH holds back a log from a commit to one of its ancestors, the reverse log of a pin,
				// so that it runs on after the refused query has failed: the commands wait for it before they fail.
				CStr SlowGit = Repo.m_Root / "slowgit/git";
				CFile::fs_CreateDirectoryForFile(SlowGit);
				CFile::fs_WriteStringToFile
					(
						SlowGit
						, "#!/bin/sh\n"
						"PATH=\"${PATH#*:}\"\n"
						"export PATH\n"
						"if [ \"$3\" = log ]; then\n"
						"\tcase \"$4\" in\n"
						"\t\t*..*)\n"
						"\t\t\tif git -C \"$2\" merge-base --is-ancestor \"${4#*..}\" \"${4%%..*}\" 2> /dev/null; then\n"
						"\t\t\t\tsleep 1\n"
						"\t\t\tfi\n"
						"\t\t\t;;\n"
						"\tesac\n"
						"fi\n"
						"exec git \"$@\"\n"
						, false
					)
				;
				CStr SlowGitDirectory = CFile::fs_GetPath(Repo.f_ShellScriptTool(SlowGit));
				CStr OriginalPath = Repo.m_Environment["PATH"];
#ifdef DPlatformFamily_Windows
				Repo.m_Environment["PATH"] = SlowGitDirectory.f_Replace("/", "\\") + ";" + OriginalPath;
#else
				Repo.m_Environment["PATH"] = SlowGitDirectory + ":" + OriginalPath;
#endif
				auto RestorePath = g_OnScopeExit / [&]
					{
						Repo.m_Environment["PATH"] = OriginalPath;
					}
				;

				for (CStr Command : {"fstat", "print", "changes"})
				{
					DMibTestPath(Command);
					co_await fg_SetPerforceTriggers(Connection, "Triggers:\n" + fg_RefusingTrigger(Repo, Command, ""));
					auto ListCommits = co_await Checkout.f_Mib({"list-commits"});
					auto RepoCommit = co_await Checkout.f_Mib({"repo-commit"});
					co_await fg_SetPerforceTriggers(Connection, "Triggers:\n");
					DMibExpect(ListCommits.m_ExitCode, !=, 0u);
					DMibExpect(ListCommits.f_GetCombinedOut().f_Find(CStr("Refused {}"_f << Command)), >=, 0);
					DMibExpect(RepoCommit.m_ExitCode, !=, 0u);
					DMibExpect(RepoCommit.f_GetCombinedOut().f_Find(CStr("Refused {}"_f << Command)), >=, 0);
				}
			}

			// The pins opened for delete in the changelist repo-commit made, back in the workspace as repo-commit wrote them.
			uint32 OurChangelist = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, Workspace = Repo.m_Path, UpdatedPins, DepotPins]() -> uint32
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						TCVector<NPerforce::CPerforceClient::COpenedFile> Opened;
						Client.f_NoThrow().f_GetOpenedFiles(Workspace / "Repos.MRepo", CStr(), Opened);
						uint32 Changelist = Opened.f_IsEmpty() ? 0 : Opened[0].m_Change.f_ToInt(uint32(0));
						Client.f_Revert(Workspace / "Repos.MRepo");
						Client.f_Delete(Workspace / "Repos.MRepo");
						Client.f_MoveToChangelist({DepotPins}, Changelist);
						CFile::fs_WriteStringToFile(Workspace / "Repos.MRepo", UpdatedPins, false);

						return Changelist;
					}
					% "Failed to open the pins for delete"
				)
			;
			DMibAssert(OurChangelist, !=, 0u);

			{
				DMibTestPath("UpToDateOtherwise");
				auto Result = co_await Checkout.f_Mib({"repo-commit"});
				DMibExpect(Result.m_ExitCode, !=, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Repos.MRepo' is opened for delete in Perforce"), >=, 0);
				auto Opened = co_await fOpened(OurChangelist);
				DMibExpect(Opened.m_Files, ==, TCVector<CStr>{DepotPins + " delete ours"});
				DMibExpect(Opened.m_nPending, ==, 1u);
			}

			{
				DMibTestPath("ForeignFile");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path, OurChangelist]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_OpenForEdit(Workspace / "Foreign.txt");
							Client.f_MoveToChangelist({"//Streams/Main/Foreign.txt"}, OurChangelist);
						}
						% "Failed to open the foreign file"
					)
				;

				auto Result = co_await Checkout.f_Mib({"repo-commit"});
				DMibExpect(Result.m_ExitCode, !=, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Repos.MRepo' is opened for delete in Perforce"), >=, 0);
				auto Opened = co_await fOpened(OurChangelist);
				DMibExpect(Opened.m_Files, ==, (TCVector<CStr>{"//Streams/Main/Foreign.txt edit ours", DepotPins + " delete ours"}));
				DMibExpect(Opened.m_nPending, ==, 1u);
			}

			{
				// Pins a pending p4 delete removed from the workspace are an absent snapshot to list-commits, which lists the pending
				// change.
				DMibTestPath("ListCommitsPendingDelete");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Revert(Workspace / "...");
							Client.f_Delete(Workspace / "Repos.MRepo");
						}
						% "Failed to delete the pins"
					)
				;

				DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / "Repos.MRepo"));
				auto Result = co_await Checkout.f_Mib({"list-commits"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Default changelist"), >=, 0);
			}

			co_return {};
		};
	}

	DMibTestRegister(CPerforceRepoCommit_Tests, Malterlib::Tool);
}
