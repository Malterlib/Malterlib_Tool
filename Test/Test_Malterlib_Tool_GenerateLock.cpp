// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CGenerateLock_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("GenerateLock") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// The scenarios set how MTool waits themselves, whatever the environment of the test says
				Repo.m_Environment.f_Remove("MalterlibBuildLockWait");
				Repo.m_Environment.f_Remove("MalterlibBuildLockTimeout");

				Repo.f_Write
					(
						"Test.MBuildSystem"
						, "%Repository \"Root\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n"
						"\t\tURL \"https://example.invalid/generate-lock-fixture.git\"\n\t\tLocation " + CEJsonSorted(Repo.m_Path).f_ToString() + "\n\t}\n}\n"
					)
				;

				auto fGenerateParams = [&](CStr _OutputDirectory, TCVector<CStr> _Extra)
					{
						TCVector<CStr> Params =
							{
								"generate"
								, "--no-color"
								, "--skip-update"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, _OutputDirectory
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						;
						Params.f_Insert(_Extra);

						return Params;
					}
				;

				auto fGenerate = [&](CStr _OutputDirectory, TCVector<CStr> _Extra) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						return Repo.f_Tool(fGenerateParams(_OutputDirectory, _Extra), true);
					}
				;

				// The lock is released once the generation reports that it waits for it
				auto fGenerateWaiting = [&](CStr _OutputDirectory, TCVector<CStr> _Extra, TCPromise<void> _Waiting)
					-> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						return Repo.f_ToolWatchingOutput(fGenerateParams(_OutputDirectory, _Extra), true, "Waiting for another generation", _Waiting);
					}
				;

				// The test holds the lock of the output directory, as a running generation does
				CStr OutputDirectory = Repo.m_Root / "output";
				CFile::fs_CreateDirectory(OutputDirectory);
				CFile::fs_WriteStringToFile(OutputDirectory / ".mib_generate_lock.owner", "process 1, since 2026-10-30, the generation of the test", false);

				CLockFile Lock(OutputDirectory / ".mib_generate_lock");
				DMibAssert(Lock.f_Lock(0.0), ==, CLockFile::ELockResult_Locked);

				DMibTestCategory("Refuse") -> TCFuture<void>
				{
					auto Result = co_await fGenerate(OutputDirectory, {});

					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("the generation of the test"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("--wait-for-lock"), >=, 0);
					DMibExpectFalse(CFile::fs_FileExists(OutputDirectory / "GeneratorStates"));

					co_return {};
				};

				DMibTestCategory("Timeout") -> TCFuture<void>
				{
					auto Result = co_await fGenerate(OutputDirectory, {"--lock-timeout", "0.5"});

					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Gave up after waiting"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("the generation of the test"), >=, 0);

					co_return {};
				};

				DMibTestCategory("OtherOutputDirectory") -> TCFuture<void>
				{
					auto Result = co_await fGenerate(Repo.m_Root / "other output", {});

					DMibExpect(Result.m_ExitCode, ==, 0);

					co_return {};
				};

				DMibTestCategory("Wait") -> TCFuture<void>
				{
					TCPromise<void> Waiting;
					auto Generation = fGenerateWaiting(OutputDirectory, {"--wait-for-lock"}, Waiting);

					co_await Waiting.f_Future().f_Timeout(120.0, "The generation did not wait for the lock");
					Lock.f_Unlock();

					auto Result = co_await fg_Move(Generation);

					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Waiting for another generation"), >=, 0);
					DMibExpectTrue(CFile::fs_FileExists(OutputDirectory / "GeneratorStates"));

					co_return {};
				};

				DMibTestCategory("WaitFromEnvironment") -> TCFuture<void>
				{
					DMibAssert(Lock.f_Lock(0.0), ==, CLockFile::ELockResult_Locked);

					Repo.m_Environment["MalterlibBuildLockWait"] = "true";
					TCPromise<void> Waiting;
					auto Generation = fGenerateWaiting(OutputDirectory, {}, Waiting);

					co_await Waiting.f_Future().f_Timeout(120.0, "The generation did not wait for the lock");
					Lock.f_Unlock();

					auto Result = co_await fg_Move(Generation);
					Repo.m_Environment.f_Remove("MalterlibBuildLockWait");

					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Waiting for another generation"), >=, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CGenerateLock_Tests, Malterlib::Tool);
}
