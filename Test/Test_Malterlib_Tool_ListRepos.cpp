// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CListRepos_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("ListRepos") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CCheckoutFixture Checkout(Repo);

				// C is pinned in a config file in a sub-directory of A, so A is its parent and the key is relative to that directory
				CStr Nested = "%Repository \"C\"\n{\n\tRepository\n\t{\n\t\tDefaultBranch \"main\"\n";
				Nested += "\t\tURL " + CEJsonSorted(Checkout.f_Remote("C")).f_ToString() + "\n";
				Nested += "\t\tLocation \"Repos/C\"->MakeAbsolute()\n";
				Nested += "\t\tConfigFile \"Repos/A/Sub/Nested.MRepo\"->MakeAbsolute()\n";
				Nested += "\t\tStateFile \"Repos/A/Sub/Nested.MRepoState\"->MakeAbsolute()\n\t}\n}\n";

				CStr WorkC = Repo.m_Root / "work/C";
				CFile::fs_CreateDirectory(Checkout.f_Remote("C"));
				CFile::fs_CreateDirectory(WorkC);
				co_await Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, Checkout.f_Remote("C"));
				co_await Repo.f_Run("git", {"init", "-q", "-b", "main"}, WorkC);
				co_await Checkout.f_CommitIn(WorkC, "Initial");
				co_await Checkout.f_GitIn(WorkC, {"push", "-q", Checkout.f_Remote("C"), "main"});

				co_await Checkout.f_Init({"A", "B"}, Nested);

				// The directory of C's config file is in A
				CStr WorkASub = Repo.m_Root / "work/A/Sub";
				CFile::fs_CreateDirectory(WorkASub);
				co_await Checkout.f_CommitIn(WorkASub, "Keep");
				co_await Checkout.f_GitIn(WorkASub, {"push", "-q", Checkout.f_Remote("A"), "main"});

				co_await Checkout.f_MibSucceeds({"update-repos"});

				// A key written by hand in another form than mib writes it
				CStr PinB = co_await Checkout.f_GitIn(Checkout.f_Repository("B"), {"rev-parse", "HEAD"});
				CStr PinA = co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"rev-parse", "HEAD"});
				CEJsonSorted Pins;
				Pins["Repos/A"]["Hash"] = PinA;
				Pins["./Repos/B"]["Hash"] = PinB;
				Repo.f_Write("Repos.MRepo", Pins.f_ToString());

				CStr Output = co_await Checkout.f_MibSucceeds({"list-repos", "--json"});
				auto iJson = Output.f_Find("[");
				DMibAssert(iJson, >=, 0);

				auto Repositories = CEJsonOrdered::fs_FromString(Output.f_Extract(iJson), "list-repos");
				DMibAssertTrue(Repositories.f_IsArray());

				TCVector<CStr> Paths;
				TCMap<CStr, CEJsonOrdered> ByPath;
				for (auto &Repository : Repositories.f_Array())
				{
					Paths.f_Insert(Repository["Path"].f_String());
					ByPath[Repository["Path"].f_String()] = Repository;
				}

				// Parents come before their children
				DMibExpect(Paths, ==, (TCVector<CStr>{".", "Repos/A", "Repos/B", "Repos/C"}));

				CStr RootHead = co_await Checkout.f_GitIn(Repo.m_Path, {"rev-parse", "HEAD"});
				auto &Root = ByPath["."];
				DMibExpect(Root["Pin"].f_String(), ==, RootHead);
				DMibExpect(Root["Parent"].f_String(), ==, "");
				DMibExpect(Root["ConfigFile"].f_String(), ==, "");
				DMibExpect(Root["Url"].f_String(), ==, Checkout.f_Remote("Root"));

				auto &A = ByPath["Repos/A"];
				DMibExpect(A["Url"].f_String(), ==, Checkout.f_Remote("A"));
				DMibExpect(A["DefaultBranch"].f_String(), ==, "main");
				DMibExpect(A["Binary"].f_Boolean(), ==, false);
				DMibExpect(A["UpdateSubmodules"].f_Boolean(), ==, false);
				DMibExpect(A["Parent"].f_String(), ==, ".");
				DMibExpect(A["ConfigFile"].f_String(), ==, "Repos.MRepo");
				DMibExpect(A["ConfigKey"].f_String(), ==, "Repos/A");
				DMibExpect(A["Pin"].f_String(), ==, PinA);

				auto &B = ByPath["Repos/B"];
				DMibExpect(B["ConfigKey"].f_String(), ==, "./Repos/B");
				DMibExpect(B["Pin"].f_String(), ==, PinB);
				DMibExpectFalse(B.f_GetMember("UpstreamUrl"));

				CStr PinC = co_await Checkout.f_GitIn(Checkout.f_Repository("C"), {"rev-parse", "HEAD"});
				auto &C = ByPath["Repos/C"];
				DMibExpect(C["Parent"].f_String(), ==, "Repos/A");
				DMibExpect(C["ConfigFile"].f_String(), ==, "Repos/A/Sub/Nested.MRepo");
				DMibExpect(C["ConfigKey"].f_String(), ==, "../../C");
				DMibExpect(C["Pin"].f_String(), ==, PinC);

				auto fListPaths = [](CStr const &_Output) -> TCVector<CStr>
					{
						TCVector<CStr> Paths;
						auto iJson = _Output.f_Find("[");
						if (iJson < 0)
							return Paths;

						for (auto &Repository : CEJsonOrdered::fs_FromString(_Output.f_Extract(iJson), "list-repos").f_Array())
							Paths.f_Insert(Repository["Path"].f_String());

						return Paths;
					}
				;

				// The root without a definition matches a name filter like the repositories with one
				auto RootOnly = fListPaths(co_await Checkout.f_MibSucceeds({"list-repos", "--json", "--repo-name", "."}));
				DMibExpect(RootOnly, ==, (TCVector<CStr>{"."}));
				auto WithoutRoot = fListPaths(co_await Checkout.f_MibSucceeds({"list-repos", "--json", "--repo-name", "Repos/*"}));
				DMibExpect(WithoutRoot, ==, (TCVector<CStr>{"Repos/A", "Repos/B", "Repos/C"}));

				// A repository that isn't cloned yet is listed with its pin
				CFile::fs_RenameFile(Checkout.f_Repository("B"), Repo.m_Root / "MovedB");
				auto NotCloned = fListPaths(co_await Checkout.f_MibSucceeds({"list-repos", "--json"}));
				// A filter on checkout state leaves it out
				auto OnBranch = fListPaths(co_await Checkout.f_MibSucceeds({"list-repos", "--json", "--repo-branch", "main"}));
				CFile::fs_RenameFile(Repo.m_Root / "MovedB", Checkout.f_Repository("B"));
				DMibExpect(NotCloned, ==, (TCVector<CStr>{".", "Repos/A", "Repos/B", "Repos/C"}));
				DMibExpect(OnBranch, ==, (TCVector<CStr>{".", "Repos/A", "Repos/C"}));

				co_return {};
			};
		}
	};

	DMibTestRegister(CListRepos_Tests, Malterlib::Tool);
}
