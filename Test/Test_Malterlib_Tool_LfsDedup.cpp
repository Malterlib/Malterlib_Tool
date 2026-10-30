// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CLfsDedup_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("LfsDedup") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				auto LfsInstall = co_await Repo.f_Run("git", {"lfs", "install"}, Repo.m_Root);
				if (LfsInstall.m_ExitCode != 0)
					co_return {}; // git-lfs isn't installed

				CCheckoutFixture Checkout(Repo);

				// git-lfs only transfers LFS objects to a local repository through a file URL
				CStr RemoteA = Checkout.f_Remote("A");
				CStr UrlA = "file://" + RemoteA;
				CStr WorkA = Repo.m_Root / "work/A";
				CFile::fs_CreateDirectory(RemoteA);
				CFile::fs_CreateDirectory(WorkA);
				co_await Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, RemoteA);
				co_await Repo.f_Run("git", {"init", "-q", "-b", "main"}, WorkA);
				co_await Checkout.f_GitIn(WorkA, {"lfs", "track", "*.bin"});

				auto fCommitBinary = [&](CStr _Name) -> TCFuture<CStr>
					{
						CByteVector Data;
						Data.f_SetLen(4096);
						for (umint i = 0; i < Data.f_GetLen(); ++i)
							Data[i] = uint8(i * 7 + _Name.f_GetLen());
						CFile::fs_WriteFile(WorkA / (_Name + ".bin"), Data);

						return Checkout.f_CommitIn(WorkA, _Name);
					}
				;

				co_await fCommitBinary("First");
				co_await Checkout.f_GitIn(WorkA, {"push", "-q", UrlA, "main"});

				CStr Extra = "Property\n{\n\tMalterlibRepositoryLfsDedup: bool = true\n}\n";
				Extra += "%Repository \"A\"\n{\n\tRepository\n\t{\n\t\tDefaultBranch \"main\"\n";
				Extra += "\t\tURL " + CEJsonSorted(UrlA).f_ToString() + "\n";
				Extra += "\t\tLocation \"Repos/A\"->MakeAbsolute()\n";
				Extra += "\t\tConfigFile \"Repos.MRepo\"->MakeAbsolute()\n";
				Extra += "\t\tStateFile \"Repos.MRepoState\"->MakeAbsolute()\n\t}\n}\n";
				co_await Checkout.f_Init({}, Extra);

				// git's own trace shows each git-lfs command that the update runs
				CStr TraceFile = Repo.m_Root / "trace.log";
				auto fUpdate = [&](TCVector<CStr> _Params) -> TCFuture<CStr>
					{
						if (CFile::fs_FileExists(TraceFile))
							CFile::fs_DeleteFile(TraceFile);
						Repo.m_Environment["GIT_TRACE"] = TraceFile;

						TCVector<CStr> Params = {"update-repos"};
						Params.f_Insert(fg_Move(_Params));
						return Checkout.f_MibSucceeds(fg_Move(Params));
					}
				;

				// The number of times the update ran git lfs dedup, not counting its tests of whether the filesystem supports it
				auto fDedups = [&]() -> umint
					{
						Repo.m_Environment.f_Remove("GIT_TRACE");
						if (!CFile::fs_FileExists(TraceFile))
							return 0;

						umint nDedups = 0;
						for (auto &Line : CFile::fs_ReadStringFromFile(TraceFile, true).f_SplitLine())
						{
							if (Line.f_Find("trace: exec: git-lfs dedup") >= 0 && Line.f_Find("--test") < 0)
								++nDedups;
						}

						return nDedups;
					}
				;

				CStr A = Checkout.f_Repository("A");

				DMibTestCategory("Clone") -> TCFuture<void>
				{
					CStr Output = co_await fUpdate({});
					umint nDedups = fDedups();

					auto Supported = co_await Repo.f_Run("git", {"lfs", "dedup", "--test"}, A);
					DMibExpect(nDedups, ==, Supported.m_ExitCode == 0 ? 1u : 0u);

					// The index is refreshed, and a filesystem without reflinks gives no message
					DMibExpect(co_await Checkout.f_GitIn(A, {"status", "--porcelain"}), ==, "");
					DMibExpect(Output.f_Find("LFS files keep their own copies"), <, 0);

					co_return {};
				};

				DMibTestCategory("Unchanged") -> TCFuture<void>
				{
					co_await fUpdate({});
					DMibExpect(fDedups(), ==, 0);

					co_return {};
				};

				DMibTestCategory("Disabled") -> TCFuture<void>
				{
					CStr Second = co_await fCommitBinary("Second");
					co_await Checkout.f_GitIn(WorkA, {"push", "-q", UrlA, "main"});

					CEJsonSorted Pins;
					Pins["Repos/A"]["Hash"] = Second;
					Repo.f_Write("Repos.MRepo", Pins.f_ToString());

					Repo.m_Environment["MalterlibRepositoryLfsDedup"] = "false";
					co_await fUpdate({"--reconcile=*:reset"});
					Repo.m_Environment.f_Remove("MalterlibRepositoryLfsDedup");

					DMibExpect(co_await Checkout.f_GitIn(A, {"rev-parse", "HEAD"}), ==, Second);
					DMibExpect(fDedups(), ==, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CLfsDedup_Tests, Malterlib::Tool);
}
