// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CPushBackupTags_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("PushBackupTags") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CCheckoutFixture Checkout(Repo);
				co_await Checkout.f_Init({"A"});
				co_await Checkout.f_MibSucceeds({"update-repos"});

				CStr A = Checkout.f_Repository("A");

				// The default branch of A is rewritten locally after the remote got a commit, so pushing it overwrites that commit
				auto fDiverge = [&](CStr _Name) -> TCFuture<CStr>
					{
						return Checkout.f_CommitIn(A, _Name);
					}
				;

				CStr Overwritten = co_await fDiverge("Overwritten");
				co_await Checkout.f_GitIn(A, {"push", "-q", "origin", "main"});
				co_await Checkout.f_GitIn(A, {"reset", "-q", "--hard", "HEAD~1"});
				CStr Rewritten = co_await fDiverge("Rewritten");

				CStr ShortOverwritten = co_await Checkout.f_GitIn(A, {"rev-parse", "--short", Overwritten});
				CStr TagName = "main_" + ShortOverwritten;

				auto fRemoteRef = [&](CStr const &_Ref) -> TCFuture<CStr>
					{
						return Checkout.f_GitIn(A, {"ls-remote", "origin", _Ref});
					}
				;

				DMibTestCategory("TagAtOtherCommit") -> TCFuture<void>
				{
					// A tag of that name that keeps another commit is never published in its place
					co_await Checkout.f_GitIn(A, {"tag", TagName, Rewritten});

					auto Result = co_await Checkout.f_Mib({"push", "--force"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("already exists at another commit"), >=, 0);

					CStr RemoteTag = co_await fRemoteRef("refs/tags/" + TagName);
					DMibExpect(RemoteTag, ==, "");
					CStr RemoteMain = co_await fRemoteRef("refs/heads/main");
					DMibExpect(RemoteMain.f_StartsWith(Overwritten), ==, true);

					co_await Checkout.f_GitIn(A, {"tag", "-d", TagName});

					co_return {};
				};

				DMibTestCategory("RerunPublishesTag") -> TCFuture<void>
				{
					// An earlier run created the tag and stopped before publishing it
					co_await Checkout.f_GitIn(A, {"tag", TagName, Overwritten});

					co_await Checkout.f_MibSucceeds({"push", "--force"});

					CStr RemoteTag = co_await fRemoteRef("refs/tags/" + TagName);
					DMibExpect(RemoteTag.f_StartsWith(Overwritten), ==, true);
					CStr RemoteMain = co_await fRemoteRef("refs/heads/main");
					DMibExpect(RemoteMain.f_StartsWith(Rewritten), ==, true);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CPushBackupTags_Tests, Malterlib::Tool);
}
