// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CMissingCommit_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("MissingCommit") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CCheckoutFixture Checkout(Repo);
				co_await Checkout.f_Init({"A"});
				co_await Checkout.f_MibSucceeds({"update-repos"});

				// A commit that no ref of the remote points at, which the remote serves by its full hash, as GitHub serves the
				// commits of a fork through the parent repository
				CStr Work = Repo.m_Root / "work/A";
				CStr ForkCommit = co_await Checkout.f_CommitIn(Work, "Fork");
				co_await Checkout.f_GitIn(Work, {"push", "-q", Checkout.f_Remote("A"), ForkCommit + ":refs/heads/fork"});
				co_await Checkout.f_GitIn(Checkout.f_Remote("A"), {"update-ref", "-d", "refs/heads/fork"});
				co_await Checkout.f_GitIn(Checkout.f_Remote("A"), {"config", "uploadpack.allowAnySHA1InWant", "true"});

				auto fPin = [&](CStr const &_Hash)
					{
						CEJsonSorted Pins;
						Pins["Repos/A"]["Hash"] = _Hash;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}
				;

				DMibTestCategory("FetchedByHash") -> TCFuture<void>
				{
					fPin(ForkCommit);
					co_await Checkout.f_MibSucceeds({"update-repos", "--reconcile=*:reset"});

					CStr Head = co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"rev-parse", "HEAD"});
					DMibExpect(Head, ==, ForkCommit);

					// No remote-tracking ref has the commit
					CStr Branches = co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"branch", "-r", "--contains", ForkCommit});
					DMibExpect(Branches, ==, "");

					co_return {};
				};

				DMibTestCategory("Missing") -> TCFuture<void>
				{
					CStr Missing = "0123456789abcdef0123456789abcdef01234567";
					fPin(Missing);
					auto Result = co_await Checkout.f_Mib({"update-repos", "--reconcile=*:reset"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find(CStr("Commit {} not found in repository '"_f << Missing)), >=, 0);

					co_return {};
				};

				DMibTestCategory("OptionAsPin") -> TCFuture<void>
				{
					// A pin comes from a file in a repository, and git fetch would read this one as an option that runs a command
					CStr Marker = Repo.m_Root / "upload-pack-ran";
					fPin("--upload-pack=touch " + Marker);
					auto Result = co_await Checkout.f_Mib({"update-repos", "--reconcile=*:reset"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpectFalse(CFile::fs_FileExists(Marker));

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CMissingCommit_Tests, Malterlib::Tool);
}
