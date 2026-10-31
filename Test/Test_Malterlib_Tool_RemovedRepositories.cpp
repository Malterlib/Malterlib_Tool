// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CRemovedRepositories_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("RemovedRepositories") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// CI sets it, and it deletes removed repositories instead of reporting them
				Repo.m_Environment.f_Remove("MalterlibRepositoryHardReset");

				// The checkout and its repositories A, B and C push to bare repositories of their own
				CStr Remotes = Repo.m_Root / "remotes";
				for (auto &Name : {"Root", "A", "B", "C"})
				{
					CStr Remote = Remotes / (CStr(Name) + ".git");
					CStr Work = Repo.m_Root / "work" / Name;
					CFile::fs_CreateDirectory(Remote);
					CFile::fs_CreateDirectory(Work);
					co_await Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, Remote);

					if (CStr(Name) == "Root")
						continue;

					co_await Repo.f_Run("git", {"init", "-q", "-b", "main"}, Work);
					CFile::fs_WriteStringToFile(Work / "File.txt", CStr(Name) + "\n", false);
					co_await Repo.f_Run("git", {"add", "File.txt"}, Work);
					co_await Repo.f_Run("git", {"commit", "-qm", "Initial"}, Work);
					co_await Repo.f_Run("git", {"push", "-q", Remote, "main"}, Work);
				}

				auto fRepository = [&](CStr const &_Name, CStr const &_Condition, CStr const &_Extra)
					{
						CStr Repository = "%Repository \"" + _Name + "\"\n{\n" + _Condition;
						Repository += "\tRepository\n\t{\n\t\tDefaultBranch \"main\"\n";
						Repository += "\t\tURL " + CEJsonSorted(Remotes / (_Name + ".git")).f_ToString() + "\n";
						Repository += "\t\tLocation " + CEJsonSorted(Repo.m_Path / "Repos" / _Name).f_ToString() + "\n";
						Repository += "\t\tConfigFile " + CEJsonSorted(Repo.m_Path / "Repos.MRepo").f_ToString() + "\n";
						Repository += "\t\tStateFile " + CEJsonSorted(Repo.m_Path / "Repos.MRepoState").f_ToString() + "\n";
						return Repository + _Extra + "\t}\n}\n";
					}
				;

				CStr Configuration = "Property\n{\n\tUseB: bool = true\n\tUseC: bool = true\n}\n\n";
				Configuration += "%Repository \"Root\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n";
				Configuration += "\t\tURL " + CEJsonSorted(Remotes / "Root.git").f_ToString() + "\n";
				Configuration += "\t\tLocation " + CEJsonSorted(Repo.m_Path).f_ToString() + "\n\t}\n}\n";
				Configuration += fRepository("A", "", "");
				Configuration += fRepository("B", "\t!!UseB true\n", "");
				Configuration += fRepository("C", "\t!!UseC true\n", "\t\tReconcileRemoved \"Leave\"\n");
				Repo.f_Write("Test.MBuildSystem", Configuration);
				Repo.f_Write(".gitignore", "/Repos/\n/BuildSystem/\n/Repos.MRepo*\n");
				co_await Repo.f_Commit();
				co_await Repo.f_Git({"push", "-q", Remotes / "Root.git", "main"});
				co_await Repo.f_Git({"remote", "add", "origin", Remotes / "Root.git"});

				// The build systems differ in which repositories they use through UseB and UseC
				auto fUpdate = [&](CStr _BuildSystem, bool _bUseB, bool _bUseC, TCVector<CStr> _Extra)
					-> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						Repo.m_Environment["UseB"] = _bUseB ? "true" : "false";
						Repo.m_Environment["UseC"] = _bUseC ? "true" : "false";

						TCVector<CStr> Params =
							{
								"update-repos"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Path / "BuildSystem" / _BuildSystem
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						;
						Params.f_Insert(_Extra);

						return Repo.f_Tool(fg_Move(Params), true);
					}
				;

				auto fRegistry = [&]
					{
						CStr RegistryFile = Repo.m_Path / "BuildSystem/BuildSystems.json";
						return CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(RegistryFile, true), RegistryFile);
					}
				;

				auto fKept = [&]
					{
						TCVector<CStr> Kept;
						auto Registry = fRegistry();
						if (auto *pKept = Registry.f_GetMember("KeptRepositories", EEJsonType_Object))
						{
							for (auto &Repository : pKept->f_Object())
								Kept.f_Insert(Repository.f_Name());
						}

						return Kept;
					}
				;

				DMibTestCategory("Union") -> TCFuture<void>
				{
					auto Default = co_await fUpdate("Default", true, true, {});
					DMibExpect(Default.m_ExitCode, ==, 0);
					bool bClonedB = CFile::fs_FileExists(Repo.m_Path / "Repos/B", EFileAttrib_Directory);
					DMibExpectTrue(bClonedB);

					auto Other = co_await fUpdate("Other", true, false, {});
					DMibExpect(Other.m_ExitCode, ==, 0);

					// Default still uses B, so Other drops it without a message
					auto OtherWithoutB = co_await fUpdate("Other", false, false, {});
					DMibExpect(OtherWithoutB.m_ExitCode, ==, 0);
					DMibExpect(OtherWithoutB.f_GetCombinedOut().f_Find("Repository has been"), <, 0);
					DMibExpectFalse(fRegistry()["BuildSystems"]["BuildSystem/Other"]["SeenRepositories"].f_GetMember("Repos/B"));
					bool bKeptB = CFile::fs_FileExists(Repo.m_Path / "Repos/B", EFileAttrib_Directory);
					DMibExpectTrue(bKeptB);

					co_return {};
				};

				DMibTestCategory("LastBuildSystem") -> TCFuture<void>
				{
					auto Result = co_await fUpdate("Default", false, true, {});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Repos/B"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("--reconcile-removed=*:leave"), >=, 0);

					auto Leave = co_await fUpdate("Default", false, true, {"--reconcile-removed=*:leave"});
					DMibExpect(Leave.m_ExitCode, ==, 0);
					DMibExpectTrue(CFile::fs_FileExists(Repo.m_Path / "Repos/B", EFileAttrib_Directory));
					DMibExpect(fKept(), ==, TCVector<CStr>{"Repos/B"});

					co_return {};
				};

				DMibTestCategory("LeaveSetting") -> TCFuture<void>
				{
					auto Result = co_await fUpdate("Default", false, false, {});
					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("leaving it on disk"), >=, 0);
					DMibExpectTrue(CFile::fs_FileExists(Repo.m_Path / "Repos/C", EFileAttrib_Directory));
					DMibExpect(fKept(), ==, (TCVector<CStr>{"Repos/B", "Repos/C"}));

					co_return {};
				};

				DMibTestCategory("IgnoreLeave") -> TCFuture<void>
				{
					auto Result = co_await fUpdate("Default", false, false, {"--ignore-leave"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Repos/C"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("--ignore-leave '--reconcile-removed=*:delete'"), >=, 0);
					bool bListedCKept = CFile::fs_FileExists(Repo.m_Path / "Repos/C", EFileAttrib_Directory);
					DMibExpectTrue(bListedCKept);

					auto Delete = co_await fUpdate("Default", false, false, {"--ignore-leave", "--reconcile-removed=Repos/B:leave,Repos/C:delete"});
					DMibExpect(Delete.m_ExitCode, ==, 0);
					bool bDeletedC = !CFile::fs_FileExists(Repo.m_Path / "Repos/C", EFileAttrib_Directory);
					DMibExpectTrue(bDeletedC);
					bool bLeftB = CFile::fs_FileExists(Repo.m_Path / "Repos/B", EFileAttrib_Directory);
					DMibExpectTrue(bLeftB);
					DMibExpect(fKept(), ==, TCVector<CStr>{"Repos/B"});

					co_return {};
				};

				DMibTestCategory("Orphaned") -> TCFuture<void>
				{
					// Other uses the kept repository B again, so it is no longer kept
					auto Other = co_await fUpdate("Other", true, false, {});
					DMibExpect(Other.m_ExitCode, ==, 0);
					DMibExpect(fKept(), ==, TCVector<CStr>{});

					// Other is deleted, and Default reports the repository that only Other used
					CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "BuildSystem/Other");
					auto Result = co_await fUpdate("Default", false, false, {});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Repos/B"), >=, 0);

					auto Delete = co_await fUpdate("Default", false, false, {"--reconcile-removed=*:delete"});
					DMibExpect(Delete.m_ExitCode, ==, 0);
					DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / "Repos/B", EFileAttrib_Directory));
					DMibExpectFalse(fRegistry()["BuildSystems"].f_GetMember("BuildSystem/Other"));

					co_return {};
				};

				DMibTestCategory("StateWithoutRegistry") -> TCFuture<void>
				{
					// A state from before the list of build systems, without LeaveWhenRemoved, as older MTools write it
					CFile::fs_DeleteFile(Repo.m_Path / "BuildSystem/BuildSystems.json");
					CFile::fs_WriteStringToFile
						(
							Repo.m_Path / "BuildSystem/Default/RepositoryState.json"
							, "{\"SeenRepositories\": {\"\": 1, \"Repos/A\": 1}}"
							, false
						)
					;

					auto Result = co_await fUpdate("Default", false, false, {});
					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpectTrue(fRegistry()["BuildSystems"]["BuildSystem/Default"]["SeenRepositories"].f_GetMember("Repos/A"));

					co_return {};
				};

				DMibTestCategory("IgnoreLeaveOfDeletedBuildSystem") -> TCFuture<void>
				{
					auto Other = co_await fUpdate("Other", false, true, {});
					DMibExpect(Other.m_ExitCode, ==, 0);

					// C sets the leave setting, but --ignore-leave reports it when its build system is deleted too
					CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "BuildSystem/Other");
					auto Result = co_await fUpdate("Default", false, false, {"--ignore-leave"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Repos/C"), >=, 0);

					auto Delete = co_await fUpdate("Default", false, false, {"--ignore-leave", "--reconcile-removed=*:delete"});
					DMibExpect(Delete.m_ExitCode, ==, 0);
					DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / "Repos/C", EFileAttrib_Directory));

					co_return {};
				};

				DMibTestCategory("OwnStateDeleted") -> TCFuture<void>
				{
					auto Default = co_await fUpdate("Default", true, false, {});
					DMibExpect(Default.m_ExitCode, ==, 0);

					// The list of build systems still has what Default saw, so B is reported
					CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "BuildSystem/Default");
					auto Result = co_await fUpdate("Default", false, false, {});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Repos/B"), >=, 0);

					co_return {};
				};

				DMibTestCategory("StateWrittenByOlderMTool") -> TCFuture<void>
				{
					auto Default = co_await fUpdate("Default", true, false, {});
					DMibExpect(Default.m_ExitCode, ==, 0);

					// An older MTool updates the state of Other without the list of build systems
					CFile::fs_CreateDirectory(Repo.m_Path / "BuildSystem/Other");
					CFile::fs_WriteStringToFile
						(
							Repo.m_Path / "BuildSystem/Other/RepositoryState.json"
							, "{\"SeenRepositories\": {\"\": 1, \"Repos/A\": 1, \"Repos/B\": 1}}"
							, false
						)
					;

					auto Result = co_await fUpdate("Default", false, false, {"--reconcile-removed=*:delete"});
					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpectTrue(CFile::fs_FileExists(Repo.m_Path / "Repos/B", EFileAttrib_Directory));

					co_return {};
				};

				DMibTestCategory("PinsOfUnusedRepositories") -> TCFuture<void>
				{
					auto fPinned = [&]
						{
							CStr ConfigFile = Repo.m_Path / "Repos.MRepo";
							auto Config = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(ConfigFile, true), ConfigFile);
							TCVector<CStr> Pinned;
							for (auto &Entry : Config.f_Object())
								Pinned.f_Insert(Entry.f_Name());
							return Pinned;
						}
					;

					auto Used = co_await fUpdate("Default", true, true, {});
					DMibAssert(Used.m_ExitCode, ==, 0);
					TCVector<CStr> AllPinned = {"Repos/A", "Repos/B", "Repos/C"};
					DMibAssert(fPinned(), ==, AllPinned);

					// A repository that a build system stops using keeps its pin, whichever way the removal is reconciled
					umint nLost = 0;
					for (umint i = 0; i < 10; ++i)
					{
						auto Unused = co_await fUpdate("Default", false, false, {"--reconcile-removed=*:leave"});
						if (Unused.m_ExitCode != 0 || fPinned() != AllPinned)
							++nLost;

						auto UsedAgain = co_await fUpdate("Default", true, true, {});
						if (UsedAgain.m_ExitCode != 0 || fPinned() != AllPinned)
							++nLost;
					}
					DMibExpect(nLost, ==, umint(0));
					auto PinnedAfter = fPinned();
					DMibExpect(PinnedAfter, ==, AllPinned);

					co_return {};
				};

				DMibTestCategory("CaseAlias") -> TCFuture<void>
				{
					if (!CFile::fs_FileExists(Repo.m_Path / "buildsystem/default", EFileAttrib_Directory))
						co_return {}; // The file system tells case apart, so no other spelling names Default

					auto Default = co_await fUpdate("Default", true, false, {});
					DMibExpect(Default.m_ExitCode, ==, 0);

					auto Other = co_await fUpdate("Other", false, false, {});
					DMibExpect(Other.m_ExitCode, ==, 0);

					// Default spelled in another case is the same build system, which was the last to see B
					auto Result = co_await fUpdate("default", false, false, {});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Repos/B"), >=, 0);

					// The list of build systems still has what Default saw when its state is gone
					CFile::fs_DeleteFile(Repo.m_Path / "BuildSystem/Default/RepositoryState.json");
					auto WithoutState = co_await fUpdate("default", false, false, {});
					DMibExpect(WithoutState.m_ExitCode, !=, 0);
					DMibExpect(WithoutState.f_GetCombinedOut().f_Find("Repos/B"), >=, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CRemovedRepositories_Tests, Malterlib::Tool);
}
