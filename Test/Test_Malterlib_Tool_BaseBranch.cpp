// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		// A column of a repository's row in the status table. The table leaves out empty columns.
		CStr fg_StatusColumn(CStr const &_Status, CStr const &_Repo, CStr const &_Column)
		{
			TCVector<CStr> Header;
			for (auto &Line : _Status.f_SplitLine())
			{
				TCVector<CStr> Columns;
				for (auto &Column : Line.f_Split("│"))
					Columns.f_Insert(Column.f_Trim());

				if (Columns.f_GetLen() < 3)
					continue;

				if (Columns[1] == "Repo")
					Header = Columns;
				else if (Columns[1] == _Repo)
				{
					for (umint i = 0; i < Header.f_GetLen() && i < Columns.f_GetLen(); ++i)
					{
						if (Header[i] == _Column)
							return Columns[i];
					}

					return {};
				}
			}

			return {};
		}

		// The output of a command without the progress lines before it
		CStr fg_LastLine(CStr const &_Output)
		{
			auto Lines = _Output.f_Trim().f_SplitLine();
			return Lines.f_IsEmpty() ? CStr() : Lines.f_GetLast().f_Trim();
		}
	}

	struct CBaseBranch_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("BaseBranch") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CCheckoutFixture Checkout(Repo);
				co_await Checkout.f_Init({"A", "B"});

				CStr A = Checkout.f_Repository("A");

				co_await Checkout.f_MibSucceeds({"update-repos"});
				co_await Checkout.f_MibSucceeds({"repo-commit"});
				co_await Repo.f_Git({"push", "-q", "origin", "main"});

				// The base branch changes A, and every repository has the branch on origin, except B
				co_await Checkout.f_MibSucceeds({"branch", "base1"});
				co_await Checkout.f_CommitIn(A, "OnBase1");
				co_await Checkout.f_MibSucceeds({"repo-commit"});
				for (auto &Directory : {Repo.m_Path, A})
					co_await Checkout.f_GitIn(Directory, {"push", "-q", "-u", "origin", "base1"});

				CStr BaseCommit = co_await Checkout.f_GitIn(Repo.m_Path, {"rev-parse", "HEAD"});

				co_await Checkout.f_MibSucceeds({"branch", "stacked", "--base-branch", "base1"});

				DMibTestCategory("BaseCommand") -> TCFuture<void>
				{
					CStr Base = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(Base, ==, "base1");

					// Each branch keeps its own base
					co_await Checkout.f_MibSucceeds({"branch", "other"});
					CStr OtherBase = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(OtherBase.f_Find("has no base branch"), >=, 0);
					co_await Checkout.f_MibSucceeds({"branch", "stacked"});
					CStr BaseAfterSwitch = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(BaseAfterSwitch, ==, "base1");

					co_await Checkout.f_MibSucceeds({"base", "--clear"});
					CStr ClearedBase = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(ClearedBase.f_Find("has no base branch"), >=, 0);
					DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / ".malterlib-base"));

					co_await Checkout.f_MibSucceeds({"base", "base1"});
					CStr BaseAgain = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(BaseAgain, ==, "base1");

					auto Ignored = co_await Repo.f_Run("git", {"check-ignore", "-q", ".malterlib-base"}, Repo.m_Path);
					DMibExpect(Ignored.m_ExitCode, ==, 0);
					DMibExpect(co_await Checkout.f_GitIn(Repo.m_Path, {"status", "--porcelain"}), ==, "");

					auto Own = co_await Checkout.f_Mib({"base", "stacked"});
					DMibExpect(Own.m_ExitCode, !=, 0);
					auto OwnBranch = co_await Checkout.f_Mib({"branch", "stacked", "--base-branch", "stacked"});
					DMibExpect(OwnBranch.m_ExitCode, !=, 0);
					CStr BaseAfterOwn = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(BaseAfterOwn, ==, "base1");

					// A branch command that fails leaves the base as it was
					auto Filtered = co_await Checkout.f_Mib({"branch", "stacked", "--base-branch", "other", "--repo-name", "Repos/A"});
					DMibExpect(Filtered.m_ExitCode, !=, 0);
					CStr BaseAfterFailure = fg_LastLine(co_await Checkout.f_MibSucceeds({"base"}));
					DMibExpect(BaseAfterFailure, ==, "base1");

					co_return {};
				};

				DMibTestCategory("RepoCommit") -> TCFuture<void>
				{
					co_await Checkout.f_CommitIn(A, "OnStacked");
					co_await Checkout.f_MibSucceeds({"repo-commit"});

					// The commits of the base branch stay as they are, below the stacked branch's own commit
					auto IsAncestor = co_await Repo.f_Run("git", {"merge-base", "--is-ancestor", BaseCommit, "HEAD"}, Repo.m_Path);
					DMibExpect(IsAncestor.m_ExitCode, ==, 0);
					CStr nAboveBase = co_await Checkout.f_GitIn(Repo.m_Path, {"rev-list", "--count", "origin/base1..HEAD"});
					DMibExpect(nAboveBase, ==, "1");

					CStr Message = co_await Checkout.f_GitIn(Repo.m_Path, {"log", "-1", "--format=%B"});
					DMibExpect(Message.f_Find("OnStacked"), >=, 0);
					DMibExpect(Message.f_Find("OnBase1"), <, 0);

					// A second run folds into the stacked branch's commit
					co_await Checkout.f_CommitIn(A, "OnStackedAgain");
					co_await Checkout.f_MibSucceeds({"repo-commit"});
					CStr nAboveBaseAgain = co_await Checkout.f_GitIn(Repo.m_Path, {"rev-list", "--count", "origin/base1..HEAD"});
					DMibExpect(nAboveBaseAgain, ==, "1");
					auto IsStillAncestor = co_await Repo.f_Run("git", {"merge-base", "--is-ancestor", BaseCommit, "HEAD"}, Repo.m_Path);
					DMibExpect(IsStillAncestor.m_ExitCode, ==, 0);

					co_return {};
				};

				DMibTestCategory("Status") -> TCFuture<void>
				{
					CStr Status = co_await Checkout.f_MibSucceeds({"status", "--show-unchanged"});

					// A compares with the base, and B, which doesn't have it on origin, with its default branch
					DMibExpect(fg_StatusColumn(Status, "Repos/A", "Others"), ==, "origin/base1 Push 2");
					DMibExpect(fg_StatusColumn(Status, "Repos/B", "Others").f_Find("origin/base1"), <, 0);

					// The stacked branch of A holds commits that only the base has besides it, so it still needs a push
					DMibExpect(fg_StatusColumn(Status, "Repos/A", "Branch on origin").f_Find("Push"), >=, 0);

					CStr Overridden = co_await Checkout.f_MibSucceeds({"status", "--show-unchanged", "--base-branch", "main"});
					DMibExpect(Overridden.f_Find("origin/base1"), <, 0);
					DMibExpect(fg_StatusColumn(Overridden, "Repos/A", "Others"), ==, "origin/main Push 3");

					// A repository on another branch than the root still compares with the base of the root's branch
					co_await Checkout.f_GitIn(A, {"checkout", "-q", "-b", "status-other"});
					CStr OtherBranch = co_await Checkout.f_MibSucceeds({"status", "--show-unchanged", "--skip-update"});
					co_await Checkout.f_GitIn(A, {"checkout", "-q", "stacked"});
					co_await Checkout.f_GitIn(A, {"branch", "-q", "-D", "status-other"});
					DMibExpect(fg_StatusColumn(OtherBranch, "Repos/A", "Others").f_Find("origin/base1"), >=, 0);

					co_return {};
				};

				DMibTestCategory("Push") -> TCFuture<void>
				{
					// Pushing never skips a branch because the base has its commits: the base can be force-pushed
					CStr Pretend = co_await Checkout.f_MibSucceeds({"push", "--pretend"});

					umint nNewA = 0;
					for (auto &Line : Pretend.f_SplitLine())
					{
						if (Line.f_Find("Repos/A") >= 0 && Line.f_Find("New branch stacked on origin") >= 0)
							++nNewA;
					}
					DMibExpect(nNewA, ==, 1);

					co_return {};
				};

				DMibTestCategory("ListCommits") -> TCFuture<void>
				{
					CStr Commits = co_await Checkout.f_MibSucceeds({"list-commits", "--local"});
					DMibExpect(Commits.f_Find("From origin/base1"), >=, 0);
					DMibExpect(Commits.f_Find("OnStacked"), >=, 0);
					DMibExpect(Commits.f_Find("OnBase1"), <, 0);

					// A repository that has the base on origin starts from it, also when its parent compares with its default branch
					co_await Checkout.f_GitIn(Repo.m_Path, {"update-ref", "-d", "refs/remotes/origin/base1"});
					CStr ChildOnly = co_await Checkout.f_MibSucceeds({"list-commits", "--local"});
					co_await Checkout.f_GitIn(Repo.m_Path, {"fetch", "-q", "origin"});
					DMibExpect(ChildOnly.f_Find("OnStacked"), >=, 0);
					DMibExpect(ChildOnly.f_Find("OnBase1"), <, 0);

					// B doesn't have the base on origin, so it starts from its own default branch, which landed a commit after the base
					CStr B = Checkout.f_Repository("B");
					CStr WorkB = Repo.m_Root / "work/B";
					co_await Checkout.f_GitIn(WorkB, {"pull", "-q", "--ff-only", Checkout.f_Remote("B"), "main"});
					co_await Checkout.f_CommitIn(WorkB, "LandedOnB");
					co_await Checkout.f_GitIn(WorkB, {"push", "-q", Checkout.f_Remote("B"), "main"});
					co_await Checkout.f_GitIn(B, {"fetch", "-q", "origin"});
					co_await Checkout.f_GitIn(B, {"reset", "-q", "--hard", "origin/main"});
					co_await Checkout.f_MibSucceeds({"repo-commit"});
					CStr Landed = co_await Checkout.f_MibSucceeds({"list-commits", "--local"});
					DMibExpect(Landed.f_Find("LandedOnB"), <, 0);
					CStr LandedMessage = co_await Checkout.f_GitIn(Repo.m_Path, {"log", "-1", "--format=%B"});
					DMibExpect(LandedMessage.f_Find("LandedOnB"), <, 0);

					co_return {};
				};

				DMibTestCategory("ChildBase") -> TCFuture<void>
				{
					// Only A has the base on origin, so the root compares with its default branch, and A with its base
					co_await Checkout.f_GitIn(Repo.m_Path, {"push", "-q", "origin", "--delete", "base1"});
					co_await Checkout.f_GitIn(Repo.m_Path, {"fetch", "-q", "--prune", "origin"});
					co_await Checkout.f_MibSucceeds({"repo-commit"});

					CStr Message = co_await Checkout.f_GitIn(Repo.m_Path, {"log", "-1", "--format=%B"});
					DMibExpect(Message.f_Find("OnStacked"), >=, 0);
					DMibExpect(Message.f_Find("OnBase1"), <, 0);

					co_return {};
				};

				DMibTestCategory("DeletedBase") -> TCFuture<void>
				{
					// ChildBase already deleted the base branch of the root on origin
					co_await Checkout.f_GitIn(A, {"push", "-q", "origin", "--delete", "base1"});
					co_await Checkout.f_GitIn(A, {"fetch", "-q", "--prune", "origin"});

					CStr Status = co_await Checkout.f_MibSucceeds({"status", "--show-unchanged"});
					DMibExpect(Status.f_Find("Base branch base1 no longer exists on origin"), >=, 0);
					DMibExpect(fg_StatusColumn(Status, "Repos/A", "Others"), ==, "origin/main Push 3");

					CStr Commits = co_await Checkout.f_MibSucceeds({"list-commits", "--local"});
					DMibExpect(Commits.f_Find("From origin/main"), >=, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CBaseBranch_Tests, Malterlib::Tool);
}
