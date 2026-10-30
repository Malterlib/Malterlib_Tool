// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CExtraFetchSpecs_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("ExtraFetchSpecs") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// What MalterlibFetchPullRequestHeads adds, which the test's build system can't import from Core
				CStr const PullRequestHeads = "+refs/pull/*/head:refs/remotes/origin-pull/*";

				CCheckoutFixture Checkout(Repo);
				co_await Checkout.f_Init
					(
						{"A", "B"}
						, "Property\n{\n\tFetchPullRequestHeads: bool = false\n}\n"
						, {
							{"A", "\t\tExtraFetchSpecs [\"" + PullRequestHeads + "\"]\n\t\t{\n\t\t\t!!FetchPullRequestHeads true\n\t\t}\n"}
							, {"B", "\t\tExtraFetchSpecs [\"" + PullRequestHeads + "\"]\n"}
						}
					)
				;

				// A pull request whose head no branch of the repository has
				CStr Work = Repo.m_Root / "work/A";
				CStr PullRequestHead = co_await Checkout.f_CommitIn(Work, "PullRequest");
				co_await Checkout.f_GitIn(Work, {"push", "-q", Checkout.f_Remote("A"), PullRequestHead + ":refs/pull/1/head"});

				auto fFetchSpecs = [&](CStr const &_Name) -> TCFuture<CStr>
					{
						return Checkout.f_GitIn(Checkout.f_Repository(_Name), {"config", "--get-all", "remote.origin.fetch"});
					}
				;

				DMibTestCategory("Clone") -> TCFuture<void>
				{
					co_await Checkout.f_MibSucceeds({"update-repos"});

					CStr SpecsB = co_await fFetchSpecs("B");
					DMibExpect(SpecsB.f_Find(PullRequestHeads), >=, 0);
					DMibExpect(SpecsB.f_Find("+refs/heads/*:refs/remotes/origin/*"), >=, 0);

					CStr SpecsA = co_await fFetchSpecs("A");
					DMibExpect(SpecsA.f_Find(PullRequestHeads), <, 0);

					co_return {};
				};

				DMibTestCategory("ExistingClone") -> TCFuture<void>
				{
					Repo.m_Environment["FetchPullRequestHeads"] = "true";
					co_await Checkout.f_MibSucceeds({"update-repos"});

					CStr SpecsA = co_await fFetchSpecs("A");
					DMibExpect(SpecsA.f_Find(PullRequestHeads), >=, 0);
					DMibExpect(SpecsA.f_Find("+refs/heads/*:refs/remotes/origin/*"), >=, 0);

					co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"fetch", "-q", "origin"});
					CStr Fetched = co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"rev-parse", "refs/remotes/origin-pull/1"});
					DMibExpect(Fetched, ==, PullRequestHead);

					// Turning it off again removes the fetch spec
					Repo.m_Environment["FetchPullRequestHeads"] = "false";
					co_await Checkout.f_MibSucceeds({"update-repos"});
					CStr SpecsAfterRemoval = co_await fFetchSpecs("A");
					DMibExpect(SpecsAfterRemoval.f_Find(PullRequestHeads), <, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CExtraFetchSpecs_Tests, Malterlib::Tool);
}
