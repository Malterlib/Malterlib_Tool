// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CGeneratorState_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("GeneratorState") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CStr Repository = "%Repository \"Root\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n"
					"\t\tURL \"https://example.invalid/generator-state-fixture.git\"\n\t\tLocation " + CEJsonSorted(Repo.m_Path).f_ToString() + "\n\t}\n}\n"
					"*Configuration\n{\n\tDebug \"Debug\"\n}\n*Platform\n{\n\tTestPlatform \"Test platform\"\n}\n"
					"*Architecture\n{\n\tTestArch \"Test architecture\"\n}\n"
				;

				auto fGenerateFile = [](CStr const &_Name) -> CStr
					{
						return "%GenerateFile \"" + _Name + "\"\n{\n\tGenerateFile\n\t{\n\t\tName `@(BuildSystemOutputDir)/" + _Name + ".txt`\n"
							"\t\tContents \"" + _Name + "\"\n\t}\n}\n"
						;
					}
				;

				auto fGenerate = [&](CStr _OutputDirectory) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						return Repo.f_Tool
							(
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
									, "--no-signal-changed"
								}
								, true
							)
						;
					}
				;

				auto fWriteBuildSystem = [&](bool _bDropped)
					{
						Repo.f_Write("Test.MBuildSystem", Repository + fGenerateFile("Kept") + (_bDropped ? fGenerateFile("Dropped") : CStr()));
					}
				;

				auto fSetStateVersions = [](CStr const &_OutputDirectory, uint32 _Version, uint32 _OldestReader)
					{
						auto StateFiles = CFile::fs_FindFiles(_OutputDirectory / "GeneratorStates" / "*.MGeneratorState");
						DMibAssertFalse(StateFiles.f_IsEmpty());

						for (auto &StateFile : StateFiles)
						{
							DMibTestPath(CFile::fs_GetFile(StateFile));
							CByteVector Data = CFile::fs_ReadFile(StateFile);
							DMibAssert(Data.f_GetLen(), >=, 2 * sizeof(uint32));
							NMemory::fg_MemCopy(Data.f_GetArray(), &_Version, sizeof(_Version));
							NMemory::fg_MemCopy(Data.f_GetArray() + sizeof(_Version), &_OldestReader, sizeof(_OldestReader));
							CFile::fs_WriteFile(StateFile, Data);
						}
					}
				;

				DMibTestCategory("LaterVersion") -> TCFuture<void>
				{
					CStr OutputDirectory = Repo.m_Root / "later";
					fWriteBuildSystem(true);
					{
						DMibTestPath("Initial");
						auto Result = co_await fGenerate(OutputDirectory);
						DMibAssert(Result.m_ExitCode, ==, 0);
						DMibAssertTrue(CFile::fs_FileExists(OutputDirectory / "Dropped.txt"));
					}

					fSetStateVersions(OutputDirectory, 0x10000, 0x123);
					fWriteBuildSystem(false);
					auto Result = co_await fGenerate(OutputDirectory);

					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpectTrue(CFile::fs_FileExists(OutputDirectory / "Kept.txt"));
					DMibExpectFalse(CFile::fs_FileExists(OutputDirectory / "Dropped.txt"));
					DMibExpect(Result.f_GetCombinedOut().f_Find("Could not read the generator state"), <, 0);

					co_return {};
				};

				DMibTestCategory("UnreadableVersion") -> TCFuture<void>
				{
					CStr OutputDirectory = Repo.m_Root / "unreadable";
					fWriteBuildSystem(true);
					{
						DMibTestPath("Initial");
						auto Result = co_await fGenerate(OutputDirectory);
						DMibAssert(Result.m_ExitCode, ==, 0);
						DMibAssertTrue(CFile::fs_FileExists(OutputDirectory / "Dropped.txt"));
					}

					fSetStateVersions(OutputDirectory, 0x10000, 0x10000);
					fWriteBuildSystem(false);
					auto Result = co_await fGenerate(OutputDirectory);

					DMibExpect(Result.m_ExitCode, ==, 0);
					DMibExpectTrue(CFile::fs_FileExists(OutputDirectory / "Kept.txt"));
					DMibExpect(Result.f_GetCombinedOut().f_Find("Could not read the generator state"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("needs a reader of version 0x10000 or later"), >=, 0);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CGeneratorState_Tests, Malterlib::Tool);
}
