// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CLocalGitIgnore_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("LocalGitIgnore") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CStr Remotes = Repo.m_Root / "remotes";
				for (auto &Name : {"Root", "A"})
				{
					CStr Remote = Remotes / (CStr(Name) + ".git");
					CFile::fs_CreateDirectory(Remote);
					co_await Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, Remote);
				}

				CStr Work = Repo.m_Root / "work/A";
				CFile::fs_CreateDirectory(Work);
				co_await Repo.f_Run("git", {"init", "-q", "-b", "main"}, Work);
				CFile::fs_WriteStringToFile(Work / "File.txt", "A\n", false);
				co_await Repo.f_Run("git", {"add", "File.txt"}, Work);
				co_await Repo.f_Run("git", {"commit", "-qm", "Initial"}, Work);
				co_await Repo.f_Run("git", {"push", "-q", Remotes / "A.git", "main"}, Work);

				// The locations are relative to the build system file, so that a worktree of the checkout has repositories of its own
				CStr Configuration = "%Repository \"Root\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n";
				Configuration += "\t\tURL " + CEJsonSorted(Remotes / "Root.git").f_ToString() + "\n";
				Configuration += "\t\tLocation \".\"->MakeAbsolute()\n\t}\n}\n";
				Configuration += "%Repository \"A\"\n{\n\tRepository\n\t{\n\t\tDefaultBranch \"main\"\n";
				Configuration += "\t\tURL " + CEJsonSorted(Remotes / "A.git").f_ToString() + "\n";
				Configuration += "\t\tLocation \"Repos/A\"->MakeAbsolute()\n";
				Configuration += "\t\tConfigFile \"Repos.MRepo\"->MakeAbsolute()\n";
				Configuration += "\t\tStateFile \"Repos.MRepoState\"->MakeAbsolute()\n";
				Configuration += "\t\tGitIgnoreType \"CoreExcludesFile\"\n\t}\n}\n";
				Repo.f_Write("Test.MBuildSystem", Configuration);
				Repo.f_Write(".gitignore", "/BuildSystem/\n/Repos.MRepo*\n");
				co_await Repo.f_Commit();
				co_await Repo.f_Git({"push", "-q", Remotes / "Root.git", "main"});
				co_await Repo.f_Git({"remote", "add", "origin", Remotes / "Root.git"});

				auto fUpdate = [&](CStr _Checkout) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						return Repo.f_Run
							(
								Repo.m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension)
								,
								{
									"update-repos"
									, "--no-color"
									, "--build-system"
									, _Checkout / "Test.MBuildSystem"
									, "--output-directory"
									, _Checkout / "BuildSystem/Default"
									, "--no-use-user-settings"
									, "--no-use-cached-environment"
								}
								, _Checkout
								, true
							)
						;
					}
				;

				auto fConfig = [&](CStr _Checkout, CStr _Scope) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						return Repo.f_Run("git", {"config", _Scope, "core.excludesFile"}, _Checkout);
					}
				;

				auto fStatus = [&](CStr _Checkout) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						return Repo.f_Run("git", {"status", "--porcelain", "--ignored", "Repos"}, _Checkout);
					}
				;

				DMibTestCategory("Checkout") -> TCFuture<void>
				{
					auto Result = co_await fUpdate(Repo.m_Path);
					DMibAssert(Result.m_ExitCode, ==, 0);

					CStr ExcludesFile = (co_await fConfig(Repo.m_Path, "--local")).f_GetStdOut().f_Trim();
					CStr Status = (co_await fStatus(Repo.m_Path)).f_GetStdOut();

					DMibExpect(ExcludesFile, ==, "BuildSystem/.localgitignore");
					DMibExpect(Status.f_Find("!! Repos/A/"), >=, 0);

					co_return {};
				};

				// Tools that do not read config.worktree, as Sublime Merge, see the shared configuration, which has to name the
				// file of each worktree
				DMibTestCategory("Worktree") -> TCFuture<void>
				{
					CStr Worktree = Repo.m_Root / "worktree";
					co_await Repo.f_Git({"worktree", "add", "-q", "-b", "worktree", Worktree});

					// As an older MTool sets it
					co_await Repo.f_Run("git", {"config", "--local", "extensions.worktreeConfig", "true"}, Worktree);
					co_await Repo.f_Run("git", {"config", "--worktree", "core.excludesFile", Worktree / "BuildSystem/.localgitignore"}, Worktree);

					auto Result = co_await fUpdate(Worktree);
					DMibAssert(Result.m_ExitCode, ==, 0);

					CStr WorktreeExcludesFile = (co_await fConfig(Worktree, "--worktree")).f_GetStdOut().f_Trim();
					CStr SharedExcludesFile = (co_await fConfig(Worktree, "--local")).f_GetStdOut().f_Trim();
					CStr WorktreeStatus = (co_await fStatus(Worktree)).f_GetStdOut();
					CStr CheckoutStatus = (co_await fStatus(Repo.m_Path)).f_GetStdOut();

					DMibExpect(WorktreeExcludesFile, ==, "");
					DMibExpect(SharedExcludesFile, ==, "BuildSystem/.localgitignore");
					DMibExpect(WorktreeStatus.f_Find("!! Repos/A/"), >=, 0);
					DMibExpect(CheckoutStatus.f_Find("!! Repos/A/"), >=, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CLocalGitIgnore_Tests, Malterlib::Tool);
}
