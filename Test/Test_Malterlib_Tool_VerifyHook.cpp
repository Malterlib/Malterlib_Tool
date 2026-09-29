// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		// Keeps each batch, and the files of the checkout's repositories while the batch is verified. It refuses while the file
		// refuse exists, refuses batches of submodules while refuse-submodules exists, and batches of repositories while
		// refuse-repositories exists. Its context comes from the environment of update-repos.
		constexpr ch8 const gc_VerifyHook[] =
			"#!/bin/sh\n"
			"n=$(ls \"$HookTestDirectory/batches\" | grep -c '\\.json$')\n"
			"n=$((n + 1))\n"
			"cat > \"$HookTestDirectory/batches/$n.json\"\n"
			"(cd \"$HookTestCheckout\" && find Repos -maxdepth 3 -not -path '*/.git/*' | sort) > \"$HookTestDirectory/batches/$n.files\"\n"
			"if [ -e \"$HookTestDirectory/refuse\" ]; then\n"
			"  echo \"Refused by the test\" >&2\n"
			"  exit 1\n"
			"fi\n"
			"if [ -e \"$HookTestDirectory/refuse-submodules\" ] && grep -q '\"Submodule\": true' \"$HookTestDirectory/batches/$n.json\"; then\n"
			"  echo \"Refused submodules by the test\" >&2\n"
			"  exit 1\n"
			"fi\n"
			"if [ -e \"$HookTestDirectory/refuse-repositories\" ] && grep -q '\"Submodule\": false' \"$HookTestDirectory/batches/$n.json\"; then\n"
			"  echo \"Refused repositories by the test\" >&2\n"
			"  exit 1\n"
			"fi\n"
		;
	}

	struct CVerifyHook_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("VerifyHook") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// The submodule of E is a local repository, which git only clones for a submodule when the user allows it
				CFile::fs_WriteStringToFile(Repo.m_Root / "gitconfig", CFile::fs_ReadStringFromFile(Repo.m_Root / "gitconfig", true) + "[protocol \"file\"]\n\tallow = always\n", false);

				CCheckoutFixture Checkout(Repo);

				// C is pinned in A, so it is in the level after A and B. E has a submodule.
				CStr Repositories = "%Repository \"C\"\n{\n\tRepository\n\t{\n\t\tDefaultBranch \"main\"\n";
				Repositories += "\t\tURL " + CEJsonSorted(Checkout.f_Remote("C")).f_ToString() + "\n";
				Repositories += "\t\tLocation \"Repos/C\"->MakeAbsolute()\n";
				Repositories += "\t\tConfigFile \"Repos/A/Sub/Nested.MRepo\"->MakeAbsolute()\n";
				Repositories += "\t\tStateFile \"Repos/A/Sub/Nested.MRepoState\"->MakeAbsolute()\n\t}\n}\n";

				CStr HookDirectory = Repo.m_Root / "hook";
				CFile::fs_CreateDirectory(HookDirectory / "batches");
				CFile::fs_WriteStringToFile(HookDirectory / "hook.sh", gc_VerifyHook, false);
				Repo.m_Environment["HookTestDirectory"] = HookDirectory;
				Repo.m_Environment["HookTestCheckout"] = Repo.m_Path;

				CStr PinA = co_await Checkout.f_CreateRemote("A");
				CStr PinB = co_await Checkout.f_CreateRemote("B");
				CStr PinC = co_await Checkout.f_CreateRemote("C");
				co_await Checkout.f_CreateRemote("S");

				// A pins C in a config file of its own, and E has S as a submodule
				CStr WorkA = Repo.m_Root / "work/A";
				{
					CEJsonSorted Pins;
					Pins["../../C"]["Hash"] = PinC;
					CFile::fs_CreateDirectory(WorkA / "Sub");
					CFile::fs_WriteStringToFile(WorkA / "Sub/Nested.MRepo", Pins.f_ToString(), false);
					PinA = co_await Checkout.f_CommitIn(WorkA, "PinC");
					co_await Checkout.f_GitIn(WorkA, {"push", "-q", Checkout.f_Remote("A"), "main"});
				}

				CStr WorkE = Repo.m_Root / "work/E";
				CFile::fs_CreateDirectory(Checkout.f_Remote("E"));
				CFile::fs_CreateDirectory(WorkE);
				co_await Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, Checkout.f_Remote("E"));
				co_await Repo.f_Run("git", {"init", "-q", "-b", "main"}, WorkE);
				// A submodule name with a space
				co_await Checkout.f_GitIn(WorkE, {"-c", "protocol.file.allow=always", "submodule", "add", "-q", "--name", "Third Party", Checkout.f_Remote("S"), "Sub"});
				CStr PinE = co_await Checkout.f_CommitIn(WorkE, "Initial");
				CStr PinS = co_await Checkout.f_GitIn(WorkE / "Sub", {"rev-parse", "HEAD"});
				co_await Checkout.f_GitIn(WorkE, {"push", "-q", Checkout.f_Remote("E"), "main"});

				co_await Checkout.f_Init({}, Checkout.f_Configuration({"A", "B", "E"}, Repositories, {{"E", "\t\tUpdateSubmodules true\n"}}));
				{
					CEJsonSorted Pins;
					Pins["Repos/A"]["Hash"] = PinA;
					Pins["Repos/B"]["Hash"] = PinB;
					Pins["Repos/E"]["Hash"] = PinE;
					Repo.f_Write("Repos.MRepo", Pins.f_ToString());
				}

				// f_Init writes a configuration of its own, so the one with the repositories replaces it
				Repo.f_Write("Test.MBuildSystem", Checkout.f_Configuration({"A", "B", "E"}, Repositories, {{"E", "\t\tUpdateSubmodules true\n"}}));

				CStr Hook = CStr("\"{}\" \"{}\""_f << Repo.m_Shell << (HookDirectory / "hook.sh"));

				auto fBatch = [&](umint _iBatch) -> CEJsonOrdered
					{
						CStr File = HookDirectory / "batches" / ("{}.json"_f << _iBatch);
						if (!CFile::fs_FileExists(File))
							return {};

						return CEJsonOrdered::fs_FromString(CFile::fs_ReadStringFromFile(File, true), File);
					}
				;
				auto fFilesDuringBatch = [&](umint _iBatch) -> TCVector<CStr>
					{
						CStr File = HookDirectory / "batches" / ("{}.files"_f << _iBatch);
						if (!CFile::fs_FileExists(File))
							return {};

						return CFile::fs_ReadStringFromFile(File, true).f_SplitLine<true>();
					}
				;
				auto fBatchCount = [&]() -> umint
					{
						return CFile::fs_FindFiles(HookDirectory / "batches/*.json").f_GetLen();
					}
				;
				auto fEntry = [](CEJsonOrdered const &_Batch, CStr const &_Path) -> CEJsonOrdered
					{
						if (auto *pRepositories = _Batch.f_GetMember("Repositories"))
						{
							for (auto &Entry : pRepositories->f_Array())
							{
								if (Entry["Path"].f_String() == _Path)
									return Entry;
							}
						}

						return {};
					}
				;

				DMibTestCategory("FreshCheckout") -> TCFuture<void>
				{
					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook});

					DMibAssert(fBatchCount(), ==, 3);

					// The first level's repositories are cloned, and nothing is checked out while the hook runs
					auto Batch1 = fBatch(1);
					DMibExpect(Batch1["Batch"].f_Integer(), ==, 1);
					DMibExpect(Batch1["Repositories"].f_Array().f_GetLen(), ==, 3);

					auto A = fEntry(Batch1, "Repos/A");
					DMibAssertTrue(A.f_IsObject());
					DMibExpect(A["Url"].f_String(), ==, Checkout.f_Remote("A"));
					DMibExpect(A["From"].f_String(), ==, "");
					DMibExpect(A["To"].f_String(), ==, PinA);
					DMibExpect(A["New"].f_Boolean(), ==, true);
					DMibExpect(A["Submodule"].f_Boolean(), ==, false);
					DMibExpect(A["Parent"].f_String(), ==, ".");
					DMibExpect(A["ConfigFile"].f_String(), ==, "Repos.MRepo");
					DMibExpect(A["ConfigKey"].f_String(), ==, "Repos/A");

					auto Files1 = fFilesDuringBatch(1);
					DMibExpectTrue(Files1.f_Contains("Repos/A/.git") >= 0);
					DMibExpect(Files1.f_Contains("Repos/A/Initial.txt"), <, 0);
					DMibExpect(Files1.f_Contains("Repos/E/.gitmodules"), <, 0);

					// The submodules of the first level are a batch of their own, after the level is checked out
					auto Batch2 = fBatch(2);
					DMibExpect(Batch2["Batch"].f_Integer(), ==, 2);
					auto Submodule = fEntry(Batch2, "Repos/E/Sub");
					DMibAssertTrue(Submodule.f_IsObject());
					DMibExpect(Submodule["Submodule"].f_Boolean(), ==, true);
					DMibExpect(Submodule["New"].f_Boolean(), ==, true);
					DMibExpect(Submodule["To"].f_String(), ==, PinS);
					DMibExpect(Submodule["Parent"].f_String(), ==, "Repos/E");
					DMibExpect(Submodule["ConfigFile"].f_String(), ==, "");

					auto Files2 = fFilesDuringBatch(2);
					DMibExpectTrue(Files2.f_Contains("Repos/E/.gitmodules") >= 0);
					DMibExpectTrue(Files2.f_Contains("Repos/E/Sub/.git") >= 0);
					DMibExpect(Files2.f_Contains("Repos/E/Sub/Initial.txt"), <, 0);

					// The next level is pinned in a config file of A
					auto Batch3 = fBatch(3);
					DMibExpect(Batch3["Batch"].f_Integer(), ==, 3);
					auto C = fEntry(Batch3, "Repos/C");
					DMibAssertTrue(C.f_IsObject());
					DMibExpect(C["Parent"].f_String(), ==, "Repos/A");
					DMibExpect(C["ConfigFile"].f_String(), ==, "Repos/A/Sub/Nested.MRepo");
					DMibExpect(C["ConfigKey"].f_String(), ==, "../../C");
					DMibExpect(C["To"].f_String(), ==, PinC);

					DMibExpectTrue(CFile::fs_FileExists(Checkout.f_Repository("C") / "Initial.txt"));
					DMibExpectTrue(CFile::fs_FileExists(Checkout.f_Repository("E") / "Sub/Initial.txt"));
					DMibExpect(co_await Checkout.f_GitIn(Checkout.f_Repository("E"), {"status", "--porcelain"}), ==, "");

					co_return {};
				};

				DMibTestCategory("Unchanged") -> TCFuture<void>
				{
					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook});
					DMibExpect(fBatchCount(), ==, 3);

					co_return {};
				};

				DMibTestCategory("Refused") -> TCFuture<void>
				{
					CStr NewB = co_await Checkout.f_CommitIn(Repo.m_Root / "work/B", "Second");
					co_await Checkout.f_GitIn(Repo.m_Root / "work/B", {"push", "-q", Checkout.f_Remote("B"), "main"});
					{
						CEJsonSorted Pins;
						Pins["Repos/A"]["Hash"] = PinA;
						Pins["Repos/B"]["Hash"] = NewB;
						Pins["Repos/E"]["Hash"] = PinE;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}

					CFile::fs_WriteStringToFile(HookDirectory / "refuse", "", false);
					auto Result = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					// Each run numbers its batches from 1
					DMibExpect(Result.f_GetCombinedOut().f_Find("Verify hook refused batch 1:"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Refused by the test"), >=, 0);
					DMibExpect(co_await Checkout.f_GitIn(Checkout.f_Repository("B"), {"rev-parse", "HEAD"}), ==, PinB);

					// Only the repository that changes is in the batch
					auto Batch4 = fBatch(4);
					DMibExpect(Batch4["Batch"].f_Integer(), ==, 1);
					DMibExpect(Batch4["Repositories"].f_Array().f_GetLen(), ==, 1);
					auto B = fEntry(Batch4, "Repos/B");
					DMibExpect(B["From"].f_String(), ==, PinB);
					DMibExpect(B["To"].f_String(), ==, NewB);
					DMibExpect(B["New"].f_Boolean(), ==, false);

					CFile::fs_DeleteFile(HookDirectory / "refuse");
					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset"});
					DMibExpect(co_await Checkout.f_GitIn(Checkout.f_Repository("B"), {"rev-parse", "HEAD"}), ==, NewB);
					DMibExpect(fBatchCount(), ==, 5);

					co_return {};
				};

				DMibTestCategory("HardReset") -> TCFuture<void>
				{
					// As the review bot updates its checkouts
					CStr NewA = co_await Checkout.f_CommitIn(Repo.m_Root / "work/A", "Third");
					co_await Checkout.f_GitIn(Repo.m_Root / "work/A", {"push", "-q", Checkout.f_Remote("A"), "main"});
					CStr PinB = co_await Checkout.f_GitIn(Checkout.f_Repository("B"), {"rev-parse", "HEAD"});
					{
						CEJsonSorted Pins;
						Pins["Repos/A"]["Hash"] = NewA;
						Pins["Repos/B"]["Hash"] = PinB;
						Pins["Repos/E"]["Hash"] = PinE;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}

					Repo.m_Environment["MalterlibRepositoryHardReset"] = "true";
					CFile::fs_WriteStringToFile(HookDirectory / "refuse", "", false);
					auto Refused = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset", "--reconcile-force"});
					CFile::fs_DeleteFile(HookDirectory / "refuse");
					DMibExpect(Refused.m_ExitCode, !=, 0);
					DMibExpect(co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"rev-parse", "HEAD"}), !=, NewA);

					umint nBefore = fBatchCount();
					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset", "--reconcile-force"});
					Repo.m_Environment.f_Remove("MalterlibRepositoryHardReset");

					auto A = fEntry(fBatch(nBefore), "Repos/A");
					DMibExpect(A["To"].f_String(), ==, NewA);
					DMibExpect(co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"rev-parse", "HEAD"}), ==, NewA);

					co_return {};
				};

				DMibTestCategory("SubmoduleRecurse") -> TCFuture<void>
				{
					// A recursive checkout of E would write the new commit of its submodule along with E
					CStr E = Checkout.f_Repository("E");
					co_await Checkout.f_GitIn(E, {"config", "submodule.recurse", "true"});

					CStr NewS = co_await Checkout.f_CommitIn(Repo.m_Root / "work/S", "SecondS");
					co_await Checkout.f_GitIn(Repo.m_Root / "work/S", {"push", "-q", Checkout.f_Remote("S"), "main"});
					co_await Checkout.f_GitIn(WorkE / "Sub", {"fetch", "-q", "origin"});
					co_await Checkout.f_GitIn(WorkE / "Sub", {"checkout", "-q", NewS});
					CStr NewE = co_await Checkout.f_CommitIn(WorkE, "SecondE");
					co_await Checkout.f_GitIn(WorkE, {"push", "-q", Checkout.f_Remote("E"), "main"});

					CStr OldS = co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"});
					{
						CEJsonSorted Pins = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true));
						Pins["Repos/E"]["Hash"] = NewE;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}

					CFile::fs_WriteStringToFile(HookDirectory / "refuse-submodules", "", false);
					auto Result = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset"});
					CFile::fs_DeleteFile(HookDirectory / "refuse-submodules");
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Refused submodules by the test"), >=, 0);
					DMibExpect(co_await Checkout.f_GitIn(E, {"rev-parse", "HEAD"}), ==, NewE);
					DMibExpect(co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"}), ==, OldS);

					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset"});
					DMibExpect(co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"}), ==, NewS);

					// A submodule commit staged in E's index is what a submodule update would check out, so it is the one verified
					co_await Checkout.f_GitIn(E, {"update-index", "--cacheinfo", "160000,{},Sub"_f << OldS});
					CFile::fs_WriteStringToFile(HookDirectory / "refuse-submodules", "", false);
					auto Staged = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					CFile::fs_DeleteFile(HookDirectory / "refuse-submodules");
					DMibExpect(Staged.f_GetCombinedOut().f_Find("Refused submodules by the test"), >=, 0);
					CStr StagedRefusedS = co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"});
					DMibExpect(StagedRefusedS, ==, NewS);

					// An inactive submodule is left as it is, as git submodule update leaves it, so it isn't in a batch. Without a
					// submodule.active that leaves it out, git submodule init would activate it again.
					co_await Checkout.f_GitIn(E, {"config", "submodule.active", "Other"});
					co_await Checkout.f_GitIn(E, {"config", "submodule.Third Party.active", "false"});
					CFile::fs_WriteStringToFile(HookDirectory / "refuse-submodules", "", false);
					auto Inactive = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					CFile::fs_DeleteFile(HookDirectory / "refuse-submodules");
					DMibExpect(Inactive.m_ExitCode, ==, 0);
					CStr InactiveS = co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"});
					DMibExpect(InactiveS, ==, NewS);
					co_await Checkout.f_GitIn(E, {"config", "--unset", "submodule.Third Party.active"});

					// A submodule.active pathspec that matches it makes it active, so its staged commit is in a batch
					co_await Checkout.f_GitIn(E, {"config", "submodule.active", "S*"});
					CFile::fs_WriteStringToFile(HookDirectory / "refuse-submodules", "", false);
					auto ActiveByPathspec = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					CFile::fs_DeleteFile(HookDirectory / "refuse-submodules");
					DMibExpect(ActiveByPathspec.f_GetCombinedOut().f_Find("Refused submodules by the test"), >=, 0);
					co_await Checkout.f_GitIn(E, {"config", "--unset", "submodule.active"});

					// A refused batch stops the level, also the batch of submodules that E's staged commit would make
					CStr NewA = co_await Checkout.f_CommitIn(WorkA, "RefusedA");
					co_await Checkout.f_GitIn(WorkA, {"push", "-q", Checkout.f_Remote("A"), "main"});
					CStr PinsBeforeRefusal = CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true);
					{
						CEJsonSorted Pins = CEJsonSorted::fs_FromString(PinsBeforeRefusal);
						Pins["Repos/A"]["Hash"] = NewA;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}
					CStr SubBeforeRefusal = co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"});
					CFile::fs_WriteStringToFile(HookDirectory / "refuse-repositories", "", false);
					umint nBeforeRefusal = fBatchCount();
					auto RefusedLevel = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook, "--reconcile=Repos/A:reset"});
					CFile::fs_DeleteFile(HookDirectory / "refuse-repositories");
					Repo.f_Write("Repos.MRepo", PinsBeforeRefusal);
					DMibExpect(RefusedLevel.f_GetCombinedOut().f_Find("Refused repositories by the test"), >=, 0);
					DMibExpect(fBatchCount(), ==, nBeforeRefusal + 1);
					CStr RefusedLevelS = co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"});
					DMibExpect(RefusedLevelS, ==, NewS);

					// A sparse checkout that leaves .gitmodules out of the working tree has its submodules from the index
					co_await Checkout.f_GitIn(E, {"update-index", "--skip-worktree", ".gitmodules"});
					CFile::fs_DeleteFile(E / ".gitmodules");
					CFile::fs_WriteStringToFile(HookDirectory / "refuse-submodules", "", false);
					auto Sparse = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					CFile::fs_DeleteFile(HookDirectory / "refuse-submodules");
					co_await Checkout.f_GitIn(E, {"update-index", "--no-skip-worktree", ".gitmodules"});
					co_await Checkout.f_GitIn(E, {"checkout", "-q", "--", ".gitmodules"});
					DMibExpect(Sparse.f_GetCombinedOut().f_Find("Refused submodules by the test"), >=, 0);

					co_await Checkout.f_GitIn(E, {"reset", "-q", "--", "Sub"});

					// A submodule path that leads elsewhere through a symbolic link isn't checked out
					{
						DMibTestPath("SymbolicLink");
						co_await Checkout.f_GitIn(E, {"update-index", "--cacheinfo", "160000,{},Sub"_f << OldS});
						CFile::fs_RenameFile(E / "Sub", E / "Elsewhere");
						CFile::fs_CreateSymbolicLink(E / "Elsewhere", E / "Sub", EFileAttrib_Directory, ESymbolicLinkFlag_None);

						auto Linked = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});

						CFile::fs_DeleteFile(E / "Sub");
						CFile::fs_RenameFile(E / "Elsewhere", E / "Sub");
						co_await Checkout.f_GitIn(E, {"reset", "-q", "--", "Sub"});

						DMibExpect(Linked.m_ExitCode, !=, 0);
						DMibExpect(Linked.f_GetCombinedOut().f_Find("goes through the symbolic link"), >=, 0);
						DMibExpect(co_await Checkout.f_GitIn(E / "Sub", {"rev-parse", "HEAD"}), ==, NewS);
					}

					co_return {};
				};

				DMibTestCategory("RefusedClone") -> TCFuture<void>
				{
					// A refused clone stays without a checkout, and a retry, here with a hard reset, verifies it again. The pin is
					// older than the tip of F's default branch, which the clone leaves HEAD at.
					CStr PinF = co_await Checkout.f_CreateRemote("F");
					co_await Checkout.f_CommitIn(Repo.m_Root / "work/F", "SecondF");
					co_await Checkout.f_GitIn(Repo.m_Root / "work/F", {"push", "-q", Checkout.f_Remote("F"), "main"});
					Repo.f_Write("Test.MBuildSystem", Checkout.f_Configuration({"A", "B", "E", "F"}, Repositories, {{"E", "\t\tUpdateSubmodules true\n"}}));
					{
						CEJsonSorted Pins = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true));
						Pins["Repos/F"]["Hash"] = PinF;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}

					CStr F = Checkout.f_Repository("F");
					CFile::fs_WriteStringToFile(HookDirectory / "refuse", "", false);
					auto Refused = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					DMibExpect(Refused.m_ExitCode, !=, 0);
					bool bRefusedHasFiles = CFile::fs_FileExists(F / "Initial.txt");
					DMibExpectFalse(bRefusedHasFiles);

					Repo.m_Environment["MalterlibRepositoryHardReset"] = "true";
					auto Retried = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook, "--reconcile=*:reset", "--reconcile-force"});
					Repo.m_Environment.f_Remove("MalterlibRepositoryHardReset");
					DMibExpect(Retried.m_ExitCode, !=, 0);
					DMibExpect(Retried.f_GetCombinedOut().f_Find("Refused by the test"), >=, 0);
					bool bRetriedHasFiles = CFile::fs_FileExists(F / "Initial.txt");
					DMibExpectFalse(bRetriedHasFiles);

					// An untracked file, as Finder leaves, doesn't make the clone checked out
					CFile::fs_DeleteFile(HookDirectory / "refuse");
					CFile::fs_WriteStringToFile(F / ".DS_Store", "", false);
					umint nBefore = fBatchCount();
					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook});
					DMibExpectTrue(CFile::fs_FileExists(F / "Initial.txt"));
					DMibExpect(co_await Checkout.f_GitIn(F, {"rev-parse", "HEAD"}), ==, PinF);

					auto Entry = fEntry(fBatch(nBefore + 1), "Repos/F");
					DMibExpect(Entry["From"].f_String(), ==, "");
					DMibExpect(Entry["New"].f_Boolean(), ==, true);
					DMibExpect(Entry["To"].f_String(), ==, PinF);

					co_return {};
				};

				DMibTestCategory("WithoutPin") -> TCFuture<void>
				{
					// D has no pin, so update-repos would follow its default branch
					co_await Checkout.f_CreateRemote("D");
					Repo.f_Write("Test.MBuildSystem", Checkout.f_Configuration({"A", "B", "D", "E", "F"}, Repositories, {{"E", "\t\tUpdateSubmodules true\n"}}));

					auto Result = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("without a pin"), >=, 0);
					DMibExpectFalse(CFile::fs_FileExists(Checkout.f_Repository("D")));

					// A branch name as a pin resolves again at checkout, to a commit the hook may not have seen, as does a name of a hash's
					// length in a repository whose object IDs have the other length
					CStr PinsBefore = CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true);
					{
						CEJsonSorted Pins = CEJsonSorted::fs_FromString(PinsBefore);
						Pins["Repos/D"]["Hash"] = "main";
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}
					auto Symbolic = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					{
						CStr TagLikeHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
						co_await Checkout.f_GitIn(Checkout.f_Remote("D"), {"tag", TagLikeHash, "main"});
						CEJsonSorted Pins = CEJsonSorted::fs_FromString(PinsBefore);
						Pins["Repos/D"]["Hash"] = TagLikeHash;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}
					auto WrongLength = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					Repo.f_Write("Repos.MRepo", PinsBefore);
					DMibExpect(Symbolic.m_ExitCode, !=, 0);
					DMibExpect(Symbolic.f_GetCombinedOut().f_Find("isn't a full commit hash"), >=, 0);
					DMibExpect(WrongLength.m_ExitCode, !=, 0);
					DMibExpect(WrongLength.f_GetCombinedOut().f_Find("isn't a full commit hash"), >=, 0);
					DMibExpectFalse(CFile::fs_FileExists(Checkout.f_Repository("D") / "Initial.txt"));

					Repo.f_Write("Test.MBuildSystem", Checkout.f_Configuration({"A", "B", "E", "F"}, Repositories, {{"E", "\t\tUpdateSubmodules true\n"}}));

					co_return {};
				};

				DMibTestCategory("Worktree") -> TCFuture<void>
				{
					// A worktree of the checkout adds its repositories as worktrees of the checkout's
					CStr Worktree = Repo.m_Root / "worktree";
					co_await Repo.f_Git({"worktree", "add", "-q", "-b", "worktree", Worktree});
					CFile::fs_CopyFile(Repo.m_Path / "Repos.MRepo", Worktree / "Repos.MRepo");
					CFile::fs_CopyFile(Repo.m_Path / "Test.MBuildSystem", Worktree / "Test.MBuildSystem");
					Repo.m_Environment["HookTestCheckout"] = Worktree;

					// The post-checkout hook of the worktree's initial checkout runs once its files are there
					CStr HooksMainA = Repo.m_Root / "hooks-main-A";
					CFile::fs_CreateDirectory(HooksMainA);
					CFile::fs_WriteStringToFile(HooksMainA / "post-checkout", "#!/bin/sh\ntest -e Initial.txt && echo \"$3\" > \"$HookTestDirectory/worktree-post-checkout\"\n", false);
					CFile::fs_SetAttributes(HooksMainA / "post-checkout", CFile::fs_GetAttributes(HooksMainA / "post-checkout") | EFileAttrib_Executable);
					co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"config", "core.hooksPath", HooksMainA});

					umint nBefore = fBatchCount();
					auto Result = co_await Repo.f_Run
						(
							Repo.m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension)
							,
							{
								"update-repos"
								, "--no-color"
								, "--build-system"
								, Worktree / "Test.MBuildSystem"
								, "--output-directory"
								, Worktree / "BuildSystem/Default"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
								, "--verify-hook"
								, Hook
							}
							, Worktree
							, true
						)
					;
					Repo.m_Environment["HookTestCheckout"] = Repo.m_Path;
					co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"config", "--unset", "core.hooksPath"});
					DMibAssert(Result.m_ExitCode, ==, 0);
					DMibExpect(CFile::fs_ReadStringFromFile(HookDirectory / "worktree-post-checkout", true).f_Trim(), ==, "1");

					CStr CurrentPinA = co_await Checkout.f_GitIn(Checkout.f_Repository("A"), {"rev-parse", "HEAD"});
					auto Batch = fBatch(nBefore + 1);
					auto A = fEntry(Batch, "Repos/A");
					DMibAssertTrue(A.f_IsObject());
					DMibExpect(A["New"].f_Boolean(), ==, true);
					DMibExpect(A["To"].f_String(), ==, CurrentPinA);

					// The worktree has its git file, and no files of A, while the hook runs
					auto Files = fFilesDuringBatch(nBefore + 1);
					DMibExpectTrue(Files.f_Contains("Repos/A/.git") >= 0);
					DMibExpect(Files.f_Contains("Repos/A/Initial.txt"), <, 0);

					bool bGitFile = CFile::fs_FileExists(Worktree / "Repos/A/.git", EFileAttrib_File);
					DMibExpectTrue(bGitFile);
					DMibExpectTrue(CFile::fs_FileExists(Worktree / "Repos/A/Initial.txt"));
					// The ignore file of C's state file is the only change, as in the checkout
					DMibExpect(co_await Checkout.f_GitIn(Worktree / "Repos/A", {"status", "--porcelain", "--untracked-files=no"}), ==, "");

					co_return {};
				};

				DMibTestCategory("MissingIndex") -> TCFuture<void>
				{
					// A checkout that lost its index keeps its files, unlike a clone that was never checked out
					CStr A = Checkout.f_Repository("A");
					CFile::fs_DeleteFile(A / ".git/index");
					CFile::fs_WriteStringToFile(A / "Initial.txt", "Local edit", false);

					co_await Checkout.f_MibSucceeds({"update-repos"});
					DMibExpect(CFile::fs_ReadStringFromFile(A / "Initial.txt", true), ==, "Local edit");

					co_await Checkout.f_GitIn(A, {"reset", "-q"});
					co_await Checkout.f_GitIn(A, {"checkout", "-q", "--", "Initial.txt"});

					co_return {};
				};

				DMibTestCategory("BranchSwitch") -> TCFuture<void>
				{
					// On the root's branch feature, A switches to its own branch feature, at the commit the hook allowed
					CStr A = Checkout.f_Repository("A");
					CStr OldA = co_await Checkout.f_GitIn(A, {"rev-parse", "HEAD"});
					CStr FeatureA = co_await Checkout.f_CommitIn(A, "FeatureA");
					co_await Checkout.f_GitIn(A, {"branch", "feature"});
					co_await Checkout.f_GitIn(A, {"reset", "-q", "--hard", OldA});

					co_await Checkout.f_GitIn(Repo.m_Path, {"checkout", "-q", "-b", "feature"});
					{
						CEJsonSorted Pins = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(Repo.m_Path / "Repos.MRepo", true));
						Pins["Repos/A"]["Hash"] = FeatureA;
						Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					}

					// As git checkout, it refuses a branch that another worktree has checked out
					CStr OtherWorktree = Repo.m_Root / "AFeature";
					co_await Checkout.f_GitIn(A, {"worktree", "add", "-q", OtherWorktree, "feature"});
					auto Occupied = co_await Checkout.f_Mib({"update-repos", "--verify-hook", Hook});
					co_await Checkout.f_GitIn(A, {"worktree", "remove", OtherWorktree});
					DMibExpect(Occupied.m_ExitCode, !=, 0);
					DMibExpect(Occupied.f_GetCombinedOut().f_Find("is already checked out in the worktree"), >=, 0);
					CStr OccupiedHead = co_await Checkout.f_GitIn(A, {"rev-parse", "HEAD"});
					DMibExpect(OccupiedHead, ==, OldA);

					// A post-checkout hook sees the branch, as for a checkout of it
					CStr HooksA = Repo.m_Root / "hooks-A";
					CFile::fs_CreateDirectory(HooksA);
					CFile::fs_WriteStringToFile(HooksA / "post-checkout", "#!/bin/sh\ngit symbolic-ref --short HEAD > \"$HookTestDirectory/post-checkout-branch\"\n", false);
					CFile::fs_SetAttributes(HooksA / "post-checkout", CFile::fs_GetAttributes(HooksA / "post-checkout") | EFileAttrib_Executable);
					co_await Checkout.f_GitIn(A, {"config", "core.hooksPath", HooksA});

					umint nBefore = fBatchCount();
					co_await Checkout.f_MibSucceeds({"update-repos", "--verify-hook", Hook});
					co_await Checkout.f_GitIn(A, {"config", "--unset", "core.hooksPath"});
					DMibExpect(CFile::fs_ReadStringFromFile(HookDirectory / "post-checkout-branch", true).f_Trim(), ==, "feature");

					auto Entry = fEntry(fBatch(nBefore + 1), "Repos/A");
					DMibExpect(Entry["From"].f_String(), ==, OldA);
					DMibExpect(Entry["To"].f_String(), ==, FeatureA);
					DMibExpect(co_await Checkout.f_GitIn(A, {"symbolic-ref", "--short", "HEAD"}), ==, "feature");
					DMibExpect(co_await Checkout.f_GitIn(A, {"rev-parse", "HEAD"}), ==, FeatureA);
					DMibExpect(co_await Checkout.f_GitIn(A, {"status", "--porcelain", "--untracked-files=no"}), ==, "");

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CVerifyHook_Tests, Malterlib::Tool);
}
