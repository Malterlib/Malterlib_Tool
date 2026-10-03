// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		// Stands in for clang-tidy: logs its arguments, one run per line, and reports a diagnostic in the source
		// and one in a header every source includes.
		CStr const gc_FakeClangTidy =
			"#!/bin/sh\n"
			"if [ \"$1\" = --verify-config ]; then echo 'No config errors detected.'; exit 0; fi\n"
			"for Last; do :; done\n"
			"if [ \"$(basename \"$Last\")\" = Unchecked.cpp ]; then echo 'Error: no checks enabled.' >&2; exit 1; fi\n"
			"printf '%s\\n' \"$*\" >> \"$(dirname \"$0\")/tidy.log\"\n"
			"printf '%s:1:2: warning: in source [fake-check]\\n    1 | x\\n      | ^\\n' \"$Last\"\n"
			"printf '%s/Shared.h:3:4: warning: in header [fake-check]\\n' \"$(dirname \"$Last\")\"\n"
		;

		struct CTidyFixture
		{
			CTidyFixture(CRepositoryFixture &_Repo)
				: m_Repo(_Repo)
				, m_Output(_Repo.m_Root / "output")
				, m_Tool(_Repo.m_Root / "bin/clang-tidy")
			{
				CFile::fs_CreateDirectoryForFile(m_Tool);
				CFile::fs_WriteStringToFile(m_Tool, gc_FakeClangTidy, false);
				CFile::fs_SetAttributes(m_Tool, CFile::fs_GetAttributes(m_Tool) | EFileAttrib_Executable);
			}

			void f_Configure(bool _bTidy) const
			{
				CStr Configuration = "%Repository \".\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n"
					"\t\tURL \"https://example.invalid/tidy-fixture.git\"\n"
				;
				Configuration += "\t\tLocation " + CEJsonSorted(m_Repo.m_Path).f_ToString() + "\n";
				Configuration += _bTidy ? "\t\tTidy true\n\t}\n}\n" : "\t}\n}\n";
				m_Repo.f_Write("Test.MBuildSystem", Configuration);
			}

			// One target's database for one configuration, compiling each source with the defines given.
			void f_AddDatabase
				(
					CStr const &_Workspace
					, int64 _WorkspacePriority
					, CStr const &_Target
					, CStr const &_Configuration
					, int64 _Priority
					, TCVector<CStr> const &_Sources
					, CStr const &_Define
				) const
			{
				CStr WorkspaceDirectory = m_Output / "ConfigStore" / _Workspace;
				CStr FileName = "Linux x86 {}"_f << _Configuration;
				CFile::fs_CreateDirectory(WorkspaceDirectory / "Configs");
				CFile::fs_WriteStringToFile(WorkspaceDirectory / "Workspace.json", CEJsonSorted{"name"_= _Workspace, "priority"_= _WorkspacePriority}.f_ToString(), false);
				CFile::fs_WriteStringToFile(WorkspaceDirectory / "Configs" / (FileName + ".json"), CEJsonSorted{"configurationPriority"_= _Priority}.f_ToString(), false);

				CStr TargetDirectory = WorkspaceDirectory / "Targets" / _Target;
				CStr Database = TargetDirectory / FileName / "compile_commands.json";
				CEJsonSorted Entries = _[];
				for (auto &Source : _Sources)
				{
					// The dependency file of the compile names the header every source includes.
					CStr Object = m_Output / _Target / (Source + ".o");
					CFile::fs_CreateDirectory(CFile::fs_GetPath(Object));
					CFile::fs_WriteStringToFile(Object + ".d", "{}: {} {}\n"_f << Object << (m_Repo.m_Path / Source) << (m_Repo.m_Path / "Shared.h"), false);

					Entries.f_Array().f_Insert
						(
							CEJsonSorted
							{
								"directory"_= m_Repo.m_Path
								, "file"_= m_Repo.m_Path / Source
								, "output"_= m_Output / _Target / (Source + ".o")
								, "arguments"_= _["clang", "-c", "-D" + _Define, "-MD", "-MF", Object + ".d", "-o", Object, m_Repo.m_Path / Source]
							}
						)
					;
				}
				CFile::fs_CreateDirectory(CFile::fs_GetPath(Database));
				CFile::fs_WriteStringToFile(Database, Entries.f_ToString(), false);

				CFile::fs_CreateDirectory(TargetDirectory / "Configs");
				CFile::fs_WriteStringToFile
					(
						TargetDirectory / "Configs" / (FileName + ".json")
						, CEJsonSorted
						{
							"compileCommands"_= Database
							, "platform"_= "Linux"
							, "architecture"_= "x86"
							, "configuration"_= _Configuration
							, "targetName"_= _Target
						}
						.f_ToString()
						, false
					)
				;
			}

			TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_Run(TCVector<CStr> _Extra) const
			{
				if (CFile::fs_FileExists(m_Repo.m_Root / "bin/tidy.log"))
					CFile::fs_DeleteFile(m_Repo.m_Root / "bin/tidy.log");

				TCVector<CStr> Params =
					{
						"tidy", "--skip-update", "--no-color", "--build-system", m_Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory", m_Output, "--no-use-user-settings", "--no-use-cached-environment", "--clang-tidy", m_Tool
					}
				;
				Params.f_Insert(_Extra);

				return m_Repo.f_Tool(fg_Move(Params), true);
			}

			// The sources clang-tidy was run on, in the order of the log.
			TCVector<CStr> f_Checked() const
			{
				TCVector<CStr> Checked;
				if (!CFile::fs_FileExists(m_Repo.m_Root / "bin/tidy.log"))
					return Checked;

				for (auto &Line : CFile::fs_ReadStringFromFile(m_Repo.m_Root / "bin/tidy.log", true).f_SplitLine<true>())
					Checked.f_Insert(CFile::fs_MakePathRelative(Line.f_Split(" ").f_GetLast(), m_Repo.m_Path));
				Checked.f_Sort();

				return Checked;
			}

			CRepositoryFixture &m_Repo;
			CStr m_Output;
			CStr m_Tool;
		};
	}

	struct CTidy_Tests : CTest
	{
		void f_DoTests()
		{
#ifndef DPlatformFamily_Windows
			DMibTestSuite("Tidy") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing mib tidy over a workspace");

				DMibTestCategory("Modes") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Modes");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("A.cpp", "");
					Repo.f_Write("B.cpp", "");
					Repo.f_Write("Shared.h", "");
					CTidyFixture Tidy(Repo);
					Tidy.f_Configure(true);

					// Two targets of one workspace build A.cpp alike, and B.cpp is built by another workspace's target.
					Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp"}, "DEBUG");
					Tidy.f_AddDatabase("Main", 200, "Lib_A", "Release", 100, {"A.cpp"}, "RELEASE");
					Tidy.f_AddDatabase("Main", 200, "Lib_AlsoA", "Debug", 101, {"A.cpp"}, "DEBUG");
					Tidy.f_AddDatabase("Other", 100, "Lib_B", "Debug", 101, {"B.cpp"}, "DEBUG");
					Tidy.f_AddDatabase("Other", 100, "Lib_A", "Debug", 101, {"A.cpp"}, "OTHER");

					{
						DMibTestPath("File");
						auto Result = co_await Tidy.f_Run({});
						DMibExpect(Result.m_ExitCode, ==, 1u);
						DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "B.cpp"}));
						CStr Output = Result.f_GetCombinedOut();
						DMibExpect(Output.f_Find("Checked 2 files with 2 commands: 3 warnings, 0 errors"), >=, 0);

						// The header both sources report is written once, after the diagnostics of the sources before it.
						aint iA = Output.f_Find(Repo.m_Path / "A.cpp:1:2: warning: in source [fake-check]");
						aint iB = Output.f_Find(Repo.m_Path / "B.cpp:1:2: warning: in source [fake-check]");
						aint iShared = Output.f_Find(Repo.m_Path / "Shared.h:3:4: warning: in header [fake-check]");
						DMibExpect(iA, >=, 0);
						DMibExpect(iB, >, iA);
						DMibExpect(iShared, >, iB);
						DMibExpect(Output.f_Find("in header", iShared + 1), <, 0);
					}

					{
						DMibTestPath("Target");
						auto Result = co_await Tidy.f_Run({"--mode", "target", "--target", "Lib_B"});
						DMibExpect(Result.m_ExitCode, ==, 1u);
						DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"B.cpp"}));

						// A target in two workspaces is checked once, in the workspace of the highest priority.
						co_await Tidy.f_Run({"--mode", "target", "--target", "Lib_A"});
						DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp"}));
					}

					{
						// Every distinct command: A.cpp with each set of defines, once although two targets build it alike.
						DMibTestPath("AllTargets");
						auto Result = co_await Tidy.f_Run({"--mode", "all", "--workspace", "Main", "--config", "Linux x86 Debug,Linux x86 Release"});
						DMibExpect(Result.m_ExitCode, ==, 1u);
						DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "A.cpp"}));
					}

					{
						// The headers of the source's repository are reported, and the checks requested are added.
						DMibTestPath("Arguments");
						co_await Tidy.f_Run({"--target", "Lib_B", "--checks", "-*,fake-check"});
						CStr Log = CFile::fs_ReadStringFromFile(Repo.m_Root / "bin/tidy.log", true);
						DMibExpect(Log.f_Find("--header-filter=^"), >=, 0);
						DMibExpect(Log.f_Find("--checks=-*,fake-check"), >=, 0);
					}

					{
						// Only the changed files are checked and reported.
						DMibTestPath("Staged");
						co_await Repo.f_Commit();
						Repo.f_Write("B.cpp", "int x;\n");
						co_await Repo.f_Stage();
						auto Result = co_await Tidy.f_Run({"--staged"});
						DMibExpect(Result.m_ExitCode, ==, 1u);
						DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"B.cpp"}));
						CStr Output = Result.f_GetCombinedOut();
						DMibExpect(Output.f_Find("1 warning, 0 errors"), >=, 0);
						DMibExpect(Output.f_Find("in header"), <, 0);
					}

					{
						// A changed header is checked through the sources that include it.
						DMibTestPath("StagedHeader");
						co_await Repo.f_Commit();
						Repo.f_Write("Shared.h", "int y;\n");
						co_await Repo.f_Stage();
						auto Result = co_await Tidy.f_Run({"--staged"});
						DMibExpect(Result.m_ExitCode, ==, 1u);
						DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "B.cpp"}));
						CStr Output = Result.f_GetCombinedOut();
						DMibExpect(Output.f_Find("1 warning, 0 errors"), >=, 0);
						DMibExpect(Output.f_Find("in source"), <, 0);
					}

					{
						// A selector that matches nothing is an error, not a clean run.
						DMibTestPath("UnmatchedTarget");
						auto Result = co_await Tidy.f_Run({"--mode", "target", "--target", "Lib_A,Lib_Missing"});
						DMibExpect(Result.m_ExitCode, !=, 0u);
						DMibExpect(Result.f_GetCombinedOut().f_Find("No compile database belongs to a target that matches"), >=, 0);

						// Also when the target has a database, but not of the requested configuration.
						auto WrongConfiguration = co_await Tidy.f_Run({"--mode", "target", "--target", "Lib_A,Lib_AlsoA", "--config", "Linux x86 Release"});
						DMibExpect(WrongConfiguration.m_ExitCode, !=, 0u);
						DMibExpect(WrongConfiguration.f_GetCombinedOut().f_Find("No target that matches --target Lib_AlsoA has a compile database for the configuration"), >=, 0);
					}

					{
						// A repository that does not opt in is not checked, and none is no error.
						DMibTestPath("NotOptedIn");
						Tidy.f_Configure(false);
						auto Result = co_await Tidy.f_Run({});
						DMibExpect(Result.m_ExitCode, ==, 0u);
						DMibExpect(Result.f_GetCombinedOut().f_Find("No repository sets Repository.Tidy"), >=, 0);
						DMibExpect(Tidy.f_Checked(), ==, TCVector<CStr>{});
					}

					co_return {};
				};

				DMibTestCategory("WithoutChecks") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "WithoutChecks");

					// A source no .clang-tidy enables a check for is skipped, not failed.
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("Unchecked.cpp", "");
					CTidyFixture Tidy(Repo);
					Tidy.f_Configure(true);
					Tidy.f_AddDatabase("Main", 200, "Lib_Unchecked", "Debug", 101, {"Unchecked.cpp"}, "DEBUG");

					auto Result = co_await Tidy.f_Run({});
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Checked 0 files with 0 commands: 0 warnings, 0 errors, 1 command without enabled checks"), >=, 0);

					co_return {};
				};

				co_return {};
			};
#endif
		}
	};

	DMibTestRegister(CTidy_Tests, Malterlib::Tool);
}
