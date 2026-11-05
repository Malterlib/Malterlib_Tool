// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/String/Appender>

// The Perforce headers use Malterlib's types without their namespaces.
namespace NMib::NPerforce
{
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NFunction;
	using namespace NMib::NConcurrency;
	using namespace NMib::NEncoding;
}

#include <Mib/Perforce/TestServer>
#include <Mib/Perforce/Wrapper>

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		// Stands in for clang-tidy: logs its arguments and the checked command's defines, and reports a diagnostic in the source,
		// in Shared.h and in Lib/Other.h when it exists.
		CStr const g_FakeClangTidy =
			"#!/bin/sh\n"
			"if [ \"$1\" = --verify-config ]; then echo 'No config errors detected.'; exit 0; fi\n"
			"for Last; do :; done\n"
			"if [ \"$(basename \"$Last\")\" = Unchecked.cpp ]; then echo 'Error: no checks enabled.' >&2; exit 1; fi\n"
			"printf '%s\\n' \"$*\" >> \"$(dirname \"$0\")/tidy.log\"\n"
			"if [ \"$1\" = -p ]; then grep -o -- '-D[A-Z]*' \"$2/compile_commands.json\" >> \"$(dirname \"$0\")/defines.log\" 2> /dev/null; fi\n"
			"printf '%s:1:2: warning: in source [fake-check]\\n    1 | x\\n      | ^\\n' \"$Last\"\n"
			"printf '%s/Shared.h:3:4: warning: in header [fake-check]\\n' \"$(dirname \"$Last\")\"\n"
			"if [ -e \"$(dirname \"$Last\")/Lib/Other.h\" ]; then\n"
			"  printf '%s/Lib/Other.h:5:6: warning: in other repository [fake-check]\\n' \"$(dirname \"$Last\")\"\n"
			"fi\n"
		;

		struct CTidyFixture
		{
			CTidyFixture(CRepositoryFixture &_Repo);

			void f_Configure(CStr const &_Tidy, CStr const &_LibTidy = {}) const;

			void f_ConfigurePerforceRoot(CStr const &_Tidy, CStr const &_LibTidy = {}) const;

			void f_AddDatabase
				(
					CStr const &_Workspace
					, int64 _WorkspacePriority
					, CStr const &_Target
					, CStr const &_Configuration
					, int64 _Priority
					, TCVector<CStr> const &_Sources
					, CStr const &_Define
				)
				const
			;

			TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_Run(TCVector<CStr> _Extra) const;

			TCVector<CStr> f_Checked() const;

			CRepositoryFixture &m_Repo;
			CStr m_WorkingDirectory = m_Repo.m_Root; // Where MTool runs
			CStr m_Output;
			CStr m_Script;                           // The script that stands in for clang-tidy
			CStr m_Tool;                             // What runs the script
		};

		CTidyFixture::CTidyFixture(CRepositoryFixture &_Repo)
			: m_Repo(_Repo)
			, m_Output(_Repo.m_Root / "output")
			, m_Script(_Repo.m_Root / "bin/clang-tidy")
		{
			CFile::fs_CreateDirectoryForFile(m_Script);
			CFile::fs_WriteStringToFile(m_Script, g_FakeClangTidy, false);
			m_Tool = _Repo.f_ShellScriptTool(m_Script);
		}

		// The repository, with Repository.Tidy when _Tidy is given, and with _LibTidy a repository in Lib nested in it.
		void CTidyFixture::f_Configure(CStr const &_Tidy, CStr const &_LibTidy) const
		{
			auto fRepository = [&](CStr const &_Name, CStr const &_Location, CStr const &_RepositoryTidy)
				{
					CStr Repository = "%Repository " + CEJsonSorted(_Name).f_ToString() + "\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n";
					Repository += "\t\tDefaultBranch \"main\"\n\t\tURL \"https://example.invalid/tidy-fixture.git\"\n";
					Repository += "\t\tLocation " + CEJsonSorted(_Location).f_ToString() + "\n";
					if (_RepositoryTidy)
						Repository += "\t\tTidy " + _RepositoryTidy + "\n";
					return Repository + "\t}\n}\n";
				}
			;
			CStr Configuration = fRepository(".", m_Repo.m_Path, _Tidy);
			if (_LibTidy)
				Configuration += fRepository("Lib", m_Repo.m_Path / "Lib", _LibTidy);
			m_Repo.f_Write("Test.MBuildSystem", Configuration);
		}

		// With _LibTidy, Lib is a repository in the root.
		void CTidyFixture::f_ConfigurePerforceRoot(CStr const &_Tidy, CStr const &_LibTidy) const
		{
			CStr Configuration;
			{
				CStr::CAppender Appender(Configuration);
				if (_Tidy)
				{
					Appender += "Property\n{\n\tPerforceRoot {Tidy: ";
					Appender += _Tidy;
					Appender += "}\n}\n";
				}
				if (_LibTidy)
				{
					Appender += "%Repository \"Lib\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n";
					Appender += "\t\tURL \"https://example.invalid/tidy-fixture.git\"\n\t\tLocation ";
					Appender += CEJsonSorted(m_Repo.m_Path / "Lib").f_ToString();
					Appender += "\n\t\tTidy ";
					Appender += _LibTidy;
					Appender += "\n\t}\n}\n";
				}
			}
			m_Repo.f_Write("Test.MBuildSystem", Configuration);
		}

		// One target's database for one configuration, compiling each source with the defines given.
		void CTidyFixture::f_AddDatabase
			(
				CStr const &_Workspace
				, int64 _WorkspacePriority
				, CStr const &_Target
				, CStr const &_Configuration
				, int64 _Priority
				, TCVector<CStr> const &_Sources
				, CStr const &_Define
			)
			const
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

		TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CTidyFixture::f_Run(TCVector<CStr> _Extra) const
		{
			if (CFile::fs_FileExists(m_Repo.m_Root / "bin/tidy.log"))
				CFile::fs_DeleteFile(m_Repo.m_Root / "bin/tidy.log");

			TCVector<CStr> Params =
				{
					"tidy"
					, "--skip-update"
					, "--no-color"
					, "--build-system"
					, m_Repo.m_Path / "Test.MBuildSystem"
					, "--output-directory"
					, m_Output
					, "--no-use-user-settings"
					, "--no-use-cached-environment"
					, "--clang-tidy"
					, m_Tool
				}
			;
			Params.f_Insert(_Extra);

			return m_Repo.f_Run(m_Repo.m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension), fg_Move(Params), m_WorkingDirectory, true);
		}

		// The sources clang-tidy was run on, in the order of the log.
		TCVector<CStr> CTidyFixture::f_Checked() const
		{
			TCVector<CStr> Checked;
			if (!CFile::fs_FileExists(m_Repo.m_Root / "bin/tidy.log"))
				return Checked;

			for (auto &Line : CFile::fs_ReadStringFromFile(m_Repo.m_Root / "bin/tidy.log", true).f_SplitLine<true>())
				Checked.f_Insert(CFile::fs_MakePathRelative(Line.f_Split(" ").f_GetLast(), m_Repo.m_Path));
			Checked.f_Sort();

			return Checked;
		}
	}

	struct CTidy_Tests : CTest
	{
		void f_DoTests();
	};

	void CTidy_Tests::f_DoTests()
	{
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
				Tidy.f_Configure("{Enabled: true}");

				// Two targets of one workspace build A.cpp alike, and B.cpp is built by another workspace's target.
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp"}, "DEBUG");
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Release", 100, {"A.cpp"}, "RELEASE");
				Tidy.f_AddDatabase("Main", 200, "Lib_AlsoA", "Debug", 101, {"A.cpp"}, "DEBUG");
				Tidy.f_AddDatabase("Other", 100, "Lib_B", "Debug", 101, {"B.cpp"}, "DEBUG");
				Tidy.f_AddDatabase("Other", 100, "Lib_A", "Debug", 101, {"A.cpp"}, "OTHER");

				{
					DMibTestPath("File");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "B.cpp"}));
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("Checked 2 files with 2 commands: 3 warnings, 0 errors"), >=, 0);

					// The header both sources report is written once, and with --sorted, in path order.
					aint iSourceA = Output.f_Find(Repo.m_Path / "A.cpp:1:2: warning: in source [fake-check]");
					aint iSourceB = Output.f_Find(Repo.m_Path / "B.cpp:1:2: warning: in source [fake-check]");
					aint iShared = Output.f_Find(Repo.m_Path / "Shared.h:3:4: warning: in header [fake-check]");
					DMibExpect(iSourceA, >=, 0);
					DMibExpect(iSourceB, >, iSourceA);
					DMibExpect(iShared, >, iSourceB);
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
					// File mode falls back to other configurations per source, not for one that no database has.
					DMibTestPath("UnknownConfigurationInFileMode");
					auto Result = co_await Tidy.f_Run({"--config", "Windows x64 Debug"});
					DMibExpect(Result.m_ExitCode, !=, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("No compile database has the configuration 'Windows x64 Debug'"), >=, 0);
					DMibExpect(Tidy.f_Checked(), ==, TCVector<CStr>{});
				}

				{
					// A repository that does not opt in is not checked, and none is no error.
					DMibTestPath("NotOptedIn");
					Tidy.f_Configure("");
					auto Result = co_await Tidy.f_Run({});
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("No repository sets Repository.Tidy"), >=, 0);
					DMibExpect(Tidy.f_Checked(), ==, TCVector<CStr>{});
				}

				co_return {};
			};

			// Like the system overrides of the malloc library, compiled by both the executable's and the shared library's targets.
			DMibTestCategory("OtherTargetsOfTheBuild") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "OtherTargetsOfTheBuild");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("A.cpp", "");
				Repo.f_Write("Shared.h", "");
				CTidyFixture Tidy(Repo);
				Tidy.f_Configure("{Enabled: true}");

				Tidy.f_AddDatabase("Main", 200, "Lib_Dll", "Debug", 101, {"A.cpp"}, "DLL");
				Tidy.f_AddDatabase("Main", 200, "Lib_Executable", "Debug", 101, {"A.cpp"}, "OVERRIDE");
				Tidy.f_AddDatabase("Main", 200, "Lib_Executable", "Release", 100, {"A.cpp"}, "RELEASE");
				Tidy.f_AddDatabase("Other", 100, "Lib_Other", "Debug", 101, {"A.cpp"}, "OTHER");

				auto Result = co_await Tidy.f_Run({"--sorted"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "A.cpp"}));

				CStr Log = CFile::fs_ReadStringFromFile(Repo.m_Root / "bin/defines.log", true);
				DMibExpect(Log.f_Find("-DDLL"), >=, 0);
				DMibExpect(Log.f_Find("-DOVERRIDE"), >=, 0);
				DMibExpect(Log.f_Find("-DRELEASE"), <, 0);
				DMibExpect(Log.f_Find("-DOTHER"), <, 0);

				{
					// Only the other workspace's equal executable command has a dependency file.
					DMibTestPath("EquivalentOfOtherTarget");
					Tidy.f_AddDatabase("Other", 100, "Lib_OtherExecutable", "Debug", 101, {"A.cpp"}, "OVERRIDE");
					CFile::fs_DeleteFile(Repo.m_Root / "output/Lib_Executable/A.cpp.o.d");
					co_await Repo.f_Commit();
					Repo.f_Write("Shared.h", "int y;\n");
					co_await Repo.f_Stage();
					CFile::fs_DeleteFile(Repo.m_Root / "bin/defines.log");

					auto StagedResult = co_await Tidy.f_Run({"--sorted", "--staged"});
					DMibExpect(StagedResult.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "A.cpp"}));
					CStr StagedLog = CFile::fs_ReadStringFromFile(Repo.m_Root / "bin/defines.log", true);
					DMibExpect(StagedLog.f_Find("-DDLL"), >=, 0);
					DMibExpect(StagedLog.f_Find("-DOVERRIDE"), >=, 0);
				}

				co_return {};
			};

			DMibTestCategory("Repositories") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Repositories");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("A.cpp", "");
				Repo.f_Write("B.cpp", "");
				Repo.f_Write("Shared.h", "");
				Repo.f_Write("Lib/Other.h", "");
				// A repository needs its own Git directory.
				auto LibInit = co_await Repo.f_Run("git", {"init", "-q", "-b", "main", Repo.m_Path / "Lib"}, Repo.m_Path);
				DMibAssert(LibInit.m_ExitCode, ==, 0u);
				CTidyFixture Tidy(Repo);
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp", "B.cpp"}, "DEBUG");

				{
					// The headers of every checked repository are reported, as a problem in a template shows where it is
					// instantiated.
					DMibTestPath("OtherRepository");
					Tidy.f_Configure("{Enabled: true}", "{Enabled: true}");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find(Repo.m_Path / "Lib/Other.h:5:6: warning: in other repository [fake-check]"), >=, 0);
					DMibExpect(Output.f_Find("Checked 2 files with 2 commands: 4 warnings, 0 errors"), >=, 0);
				}

				{
					// The repository filters select the sources compiled, not what their compiles report.
					DMibTestPath("FilteredRunReportsOtherRepository");
					Tidy.f_Configure("{Enabled: true}", "{Enabled: true}");
					auto Result = co_await Tidy.f_Run({"--sorted", "-n", "."});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find(Repo.m_Path / "Lib/Other.h:5:6: warning: in other repository [fake-check]"), >=, 0);
					DMibExpect(Output.f_Find("Checked 2 files with 2 commands: 4 warnings, 0 errors"), >=, 0);
				}

				{
					// But not those of a repository that does not check its files, also when it is nested in one that does.
					DMibTestPath("UncheckedRepository");
					Tidy.f_Configure("{Enabled: true}", "{Enabled: false}");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("in other repository"), <, 0);
					DMibExpect(Output.f_Find("Checked 2 files with 2 commands: 3 warnings, 0 errors"), >=, 0);
				}

				{
					// The files a repository excludes are neither checked nor reported.
					DMibTestPath("ExcludeFiles");
					Tidy.f_Configure("{Enabled: true, ExcludeFiles: [\"B.cpp\", \"*.h\"]}");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp"}));
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("in header"), <, 0);
					DMibExpect(Output.f_Find("in other repository"), <, 0);
					DMibExpect(Output.f_Find("Checked 1 file with 1 command: 1 warning, 0 errors"), >=, 0);
				}

				{
					// With includes, only the files that match one are.
					DMibTestPath("IncludeFiles");
					Tidy.f_Configure("{Enabled: true, IncludeFiles: [\"B.*\", \"Lib/*\"]}");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"B.cpp"}));
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("in header"), <, 0);
					DMibExpect(Output.f_Find(Repo.m_Path / "Lib/Other.h:5:6: warning: in other repository [fake-check]"), >=, 0);
					DMibExpect(Output.f_Find("Checked 1 file with 1 command: 2 warnings, 0 errors"), >=, 0);
				}

				{
					// The repositories selected and those reported come from one read of the build system, whose defines would
					// collide with themselves in a second.
					DMibTestPath("FilteredRunWithDefine");
					Tidy.f_Configure("{Enabled: true}", "{Enabled: true}");
					CStr Configuration = CFile::fs_ReadStringFromFile(Repo.m_Path / "Test.MBuildSystem", true);
					Repo.f_Write("Test.MBuildSystem", Configuration + "Property\n{\n\tTidyFixtureDefine define bool = false\n}\n");
					auto Result = co_await Tidy.f_Run({"--sorted", "-n", "."});
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("Variable definition collision"), <, 0);
					DMibExpect(Output.f_Find("Checked 2 files with 2 commands: 4 warnings, 0 errors"), >=, 0);
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
				Tidy.f_Configure("{Enabled: true}");
				Tidy.f_AddDatabase("Main", 200, "Lib_Unchecked", "Debug", 101, {"Unchecked.cpp"}, "DEBUG");

				auto Result = co_await Tidy.f_Run({});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Checked 0 files with 0 commands: 0 warnings, 0 errors, 1 command without enabled checks"), >=, 0);

				co_return {};
			};

			DMibTestCategory("HeaderConfiguration") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "HeaderConfiguration");

				// A configuration only headers take is verified as a source's is, also one not yet added, while one taken only
				// by files the repository leaves out is not.
				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("A.cpp", "");
				Repo.f_Write("Shared.h", "");
				Repo.f_Write("Headers/Header.h", "");
				Repo.f_Write("Headers/.clang-tidy", "Checks: Invalid\n");
				Repo.f_Write("Vendor/Vendor.h", "");
				Repo.f_Write("Vendor/.clang-tidy", "Checks: Invalid\n");
				// A configuration whose only checked file starts from one below it that does not inherit is never read.
				Repo.f_Write("Unread/.clang-tidy", "Checks: Invalid\n");
				Repo.f_Write("Unread/Sub/.clang-tidy", "InheritParentConfig: false\n");
				Repo.f_Write("Unread/Sub/Header.h", "");
				CTidyFixture Tidy(Repo);
				CFile::fs_WriteStringToFile
					(
						Tidy.m_Script
						, "#!/bin/sh\n"
						"if [ \"$1\" = --verify-config ]; then\n"
						"  if grep -q Invalid .clang-tidy 2>/dev/null; then echo \"error: unknown check 'Invalid'\"; exit 1; fi\n"
						"  echo 'No config errors detected.'; exit 0\n"
						"fi\n"
						, false
					)
				;
				Repo.f_ShellScriptTool(Tidy.m_Script);
				Tidy.f_Configure("{Enabled: true, ExcludeFiles: [\"Vendor/*\"]}");
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp"}, "DEBUG");

				auto Result = co_await Tidy.f_Run({});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Vendor"), <, 0);
				DMibExpect(Output.f_Find("Unread"), <, 0);

				// Nor is one deleted from the working tree but still in the index.
				co_await Repo.f_Stage();
				CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "Headers");
				auto Deleted = co_await Tidy.f_Run({});
				DMibExpect(Deleted.f_GetCombinedOut().f_Find("Headers"), <, 0);
				DMibExpect(Output.f_Find(Repo.m_Path / "Headers: the configuration of the headers is not valid"), >=, 0);
				DMibExpect(Output.f_Find("the configuration of the sources is not valid"), <, 0);

				co_return {};
			};

			DMibTestCategory("StaleFixes") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "StaleFixes");

				// A file written while clang-tidy runs has its fixes left out: their offsets are those of the text analyzed.
				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("A.cpp", "int a;\n");
				Repo.f_Write("Shared.h", "");
				CTidyFixture Tidy(Repo);
				CFile::fs_WriteStringToFile
					(
						Tidy.m_Script
						, "#!/bin/sh\n"
						"if [ \"$1\" = --verify-config ]; then echo 'No config errors detected.'; exit 0; fi\n"
						"for Last; do :; done\n"
						"for Argument; do case \"$Argument\" in --export-fixes=*) Fixes=\"${Argument#--export-fixes=}\";; esac; done\n"
						"printf '%s:1:1: warning: in source [fake-check]\\n' \"$Last\"\n"
						"printf -- \"---\\nMainSourceFile: '%s'\\nDiagnostics:\\n  - DiagnosticName: fake-check\\n    DiagnosticMessage:\\n\"\\\n"
						"\"      Message: 'x'\\n      FilePath: '%s'\\n      FileOffset: 0\\n      Replacements:\\n        - FilePath: '%s'\\n\"\\\n"
						"\"          Offset: 0\\n          Length: 0\\n          ReplacementText: 'y'\\n...\\n\" \"$Last\" \"$Last\" \"$Last\" > \"$Fixes\"\n"
						"echo '// Saved' >> \"$Last\"\n"
						, false
					)
				;
				Repo.f_ShellScriptTool(Tidy.m_Script);
				CStr Apply = CFile::fs_GetPath(Tidy.m_Tool) / "clang-apply-replacements";
				CFile::fs_WriteStringToFile(Apply, "#!/bin/sh\ntouch \"$(dirname \"$0\")/apply.log\"\n", false);
				Repo.f_ShellScriptTool(Apply);
				Tidy.f_Configure("{Enabled: true}");
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp"}, "DEBUG");

				auto Result = co_await Tidy.f_Run({"--fix"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find(Repo.m_Path / "A.cpp: changed while clang-tidy ran"), >=, 0);
				DMibExpectFalse(CFile::fs_FileExists(CFile::fs_GetPath(Tidy.m_Tool) / "apply.log"));

				co_return {};
			};

			DMibTestCategory("FixHighlightingUnchangedFile") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "FixHighlightingUnchangedFile");

				// A note's highlighted range in a file not changed is no replacement there, so the fix of the changed file applies.
				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("A.cpp", "int a;\n");
				Repo.f_Write("Shared.h", "");
				co_await Repo.f_Commit();
				Repo.f_Write("A.cpp", "int a = 1;\n");
				co_await Repo.f_Stage();
				CTidyFixture Tidy(Repo);
				CFile::fs_WriteStringToFile
					(
						Tidy.m_Script
						, "#!/bin/sh\n"
						"if [ \"$1\" = --verify-config ]; then echo 'No config errors detected.'; exit 0; fi\n"
						"for Last; do :; done\n"
						"for Argument; do case \"$Argument\" in --export-fixes=*) Fixes=\"${Argument#--export-fixes=}\";; esac; done\n"
						"Header=\"$(dirname \"$Last\")/Shared.h\"\n"
						"printf '%s:1:1: warning: in source [fake-check]\\n' \"$Last\"\n"
						"printf -- \"---\\nMainSourceFile: '%s'\\nDiagnostics:\\n  - DiagnosticName: fake-check\\n    DiagnosticMessage:\\n\"\\\n"
						"\"      Message: 'x'\\n      FilePath: '%s'\\n      FileOffset: 0\\n      Replacements:\\n        - FilePath: '%s'\\n\"\\\n"
						"\"          Offset: 0\\n          Length: 0\\n          ReplacementText: 'y'\\n      Ranges:\\n        - FilePath: '%s'\\n\"\\\n"
						"\"          FileOffset: 0\\n          Length: 1\\n...\\n\" \"$Last\" \"$Last\" \"$Last\" \"$Header\" > \"$Fixes\"\n"
						, false
					)
				;
				Repo.f_ShellScriptTool(Tidy.m_Script);
				CStr Apply = CFile::fs_GetPath(Tidy.m_Tool) / "clang-apply-replacements";
				CFile::fs_WriteStringToFile(Apply, "#!/bin/sh\ntouch \"$(dirname \"$0\")/apply.log\"\n", false);
				Repo.f_ShellScriptTool(Apply);
				Tidy.f_Configure("{Enabled: true}");
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp"}, "DEBUG");

				co_await Tidy.f_Run({"--staged", "--fix"});
				DMibExpectTrue(CFile::fs_FileExists(CFile::fs_GetPath(Tidy.m_Tool) / "apply.log"));

				co_return {};
			};

			DMibTestCategory("IncludedSource") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "IncludedSource");

				// A source that no database compiles is checked through the source that includes it, and its database is
				// not out of date for that.
				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("A.cpp", "");
				Repo.f_Write("Included.cpp", "");
				Repo.f_Write("Shared.h", "");
				CTidyFixture Tidy(Repo);
				Tidy.f_Configure("{Enabled: true}");
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp"}, "DEBUG");
				CStr Object = Tidy.m_Output / "Lib_A/A.cpp.o";
				CFile::fs_WriteStringToFile(Object + ".d", "{}: {} {}\n"_f << Object << (Repo.m_Path / "A.cpp") << (Repo.m_Path / "Included.cpp"), false);

				co_await Repo.f_Commit();
				Repo.f_Write("Included.cpp", "int x;\n");
				co_await Repo.f_Stage();
				auto Result = co_await Tidy.f_Run({"--staged"});
				DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp"}));
				DMibExpect(Result.f_GetCombinedOut().f_Find("No compile database compiles the changed"), <, 0);

				co_return {};
			};

			DMibTestCategory("PerforceRoot") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "PerforceRoot");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_MakePerforceRoot("P4CLIENT=Offline\n");
				Repo.f_Write(".p4ignore", "/Ignored/\nShared.h\n");
				Repo.f_Write("A.cpp", "");
				Repo.f_Write("Ignored/Ignored.cpp", "");
				Repo.f_Write("Shared.h", "");
				Repo.f_Write("Lib/Other.h", "");
				Repo.f_Write("Lib/.clang-tidy", "Checks: Invalid\n");
				auto LibInit = co_await Repo.f_Run("git", {"init", "-q", "-b", "main", Repo.m_Path / "Lib"}, Repo.m_Path);
				DMibAssert(LibInit.m_ExitCode, ==, 0u);
				// A repository the build system doesn't know.
				Repo.f_Write("Unknown/Header.h", "");
				Repo.f_Write("Unknown/.clang-tidy", "Checks: Invalid\n");
				auto UnknownInit = co_await Repo.f_Run("git", {"init", "-q", "-b", "main", Repo.m_Path / "Unknown"}, Repo.m_Path);
				DMibAssert(UnknownInit.m_ExitCode, ==, 0u);
				// Only the configuration of the root's own headers is verified.
				Repo.f_Write("Ignored/Header.h", "");
				Repo.f_Write("Ignored/.clang-tidy", "Checks: Invalid\n");
				Repo.f_Write("Checked/Header.h", "");
				Repo.f_Write("Checked/.clang-tidy", "Checks: Invalid\n");

				CTidyFixture Tidy(Repo);
				CFile::fs_WriteStringToFile
					(
						Tidy.m_Script
						, "#!/bin/sh\n"
						"if [ \"$1\" = --verify-config ]; then\n"
						"  if grep -q Invalid .clang-tidy 2>/dev/null; then echo \"error: unknown check 'Invalid'\"; exit 1; fi\n"
						"  echo 'No config errors detected.'; exit 0\n"
						"fi\n"
						+ g_FakeClangTidy
						, false
					)
				;
				Repo.f_ShellScriptTool(Tidy.m_Script);
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp", "Ignored/Ignored.cpp"}, "DEBUG");

				{
					DMibTestPath("Checked");
					Tidy.f_ConfigurePerforceRoot("{Enabled: true}", "{Enabled: false}");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp"}));
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find(Repo.m_Path / "A.cpp:1:2: warning: in source [fake-check]"), >=, 0);
					DMibExpect(Output.f_Find(Repo.m_Path / "Checked: the configuration of the headers is not valid"), >=, 0);
					DMibExpect(Output.f_Find("Ignored"), <, 0);
					DMibExpect(Output.f_Find("in header"), <, 0);
					DMibExpect(Output.f_Find("Unknown"), <, 0);
					DMibExpect(Output.f_Find(Repo.m_Path / "Lib"), <, 0);
				}

				{
					DMibTestPath("ExcludeFiles");
					Tidy.f_ConfigurePerforceRoot("{Enabled: true, ExcludeFiles: [\"Checked/*\", \"*.h\"]}", "{Enabled: false}");
					auto Result = co_await Tidy.f_Run({"--sorted"});
					CStr Output = Result.f_GetCombinedOut();
					DMibExpect(Output.f_Find("Checked:"), <, 0);
					DMibExpect(Output.f_Find("in header"), <, 0);
					DMibExpect(Output.f_Find("Checked 1 file with 1 command: 1 warning, 0 errors"), >=, 0);
				}

				{
					DMibTestPath("GitBase");
					Tidy.f_ConfigurePerforceRoot("{Enabled: true}");
					auto Result = co_await Tidy.f_Run({"--base", "main"});
					DMibExpect(Result.m_ExitCode, !=, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("is a Perforce root, whose base is a changelist, like @1234, which 'main' is not"), >=, 0);
					DMibExpect(Tidy.f_Checked(), ==, TCVector<CStr>{});
				}

				{
					DMibTestPath("NotOptedIn");
					Tidy.f_ConfigurePerforceRoot("{Enabled: false}");
					auto Result = co_await Tidy.f_Run({});
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("No repository sets Repository.Tidy"), >=, 0);
				}

				co_return {};
			};

			// Shared_Compile_MalterlibTidy.MSettings declares PerforceRootTidyRules, which generating sets.
			DMibTestCategory("PerforceRootPluginRules") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "PerforceRootPluginRules");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_MakePerforceRoot("P4CLIENT=Offline\n");
				Repo.f_Write(".p4ignore", "/Ignored/\n*.gen.h\n");
				Repo.f_Write("A.cpp", "");
				Repo.f_Write("B.gen.h", "");
				Repo.f_Write("Ignored/A.h", "");
				Repo.f_Write("Sub/C.gen.h", "");
				Repo.f_Write("Unknown/A.h", "");
				Repo.f_Write("Local/Local.h", "");
				auto UnknownInit = co_await Repo.f_Run("git", {"init", "-q", "-b", "main", Repo.m_Path / "Unknown"}, Repo.m_Path);
				DMibAssert(UnknownInit.m_ExitCode, ==, 0u);

				struct CGenerated
				{
					CStr m_Rules;
					CStr m_Output;
				};

				// With _bBuild, as a build generates: only when a dependency changed.
				auto fGenerate = [&](CStr _PerforceRoot, bool _bWriteBuildSystem, bool _bBuild, bool _bCachedEnvironment = false) -> TCUnsafeFuture<CGenerated>
					{
						CStr Configuration;
						{
							CStr::CAppender Appender(Configuration);
							Appender +=
								"Property\n{\n"
								"\tPerforceRootTidyRules: [{Location: string, Tidy: {Enabled: bool, IncludeFiles: [string], ExcludeFiles: [string]}}] = []\n"
							;
							if (_PerforceRoot)
							{
								Appender += "\tPerforceRoot ";
								Appender += _PerforceRoot;
								Appender.f_AddChar('\n');
							}
							Appender +=
								"}\n"
								"*Configuration\n{\n\tDebug \"Debug\"\n}\n*Platform\n{\n\tTestPlatform \"Test platform\"\n}\n"
								"*Architecture\n{\n\tTestArch \"Test architecture\"\n}\n"
								"%GenerateFile \"Rules\"\n{\n\tGenerateFile\n\t{\n\t\tName `@(BuildSystemOutputDir)/Rules.json`\n"
								"\t\tContents PerforceRootTidyRules->ToString()\n\t}\n}\n"
							;
						}
						if (_bWriteBuildSystem)
							Repo.f_Write("Test.MBuildSystem", Configuration);

						TCVector<CStr> Params =
							{
								"generate"
								, "--no-color"
								, "--skip-update"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, _bCachedEnvironment ? "--use-cached-environment" : "--no-use-cached-environment"
								, "--no-signal-changed"
							}
						;
						if (_bBuild)
						{
							Params.f_Insert("--action");
							Params.f_Insert("Build");
						}

						auto Result = co_await Repo.f_Tool(fg_Move(Params), true);
						if (Result.m_ExitCode)
							co_return DMibErrorInstance("Generate failed: {}"_f << Result.f_GetCombinedOut());

						co_return CGenerated{CFile::fs_ReadStringFromFile(Repo.m_Root / "output/Rules.json", true), Result.f_GetCombinedOut()};
					}
				;
				auto fRules = [&](CStr _PerforceRoot) -> TCUnsafeFuture<CStr>
					{
						co_return (co_await fGenerate(_PerforceRoot, true, false)).m_Rules;
					}
				;

				CStr const TidyRoot = "{Tidy: {Enabled: true, ExcludeFiles: [\"Generated/*\"]}}";
				auto fBuildExcluded = [&]() -> TCUnsafeFuture<TCSet<CStr>>
					{
						auto Generated = co_await fGenerate(TidyRoot, false, true);
						DMibExpect(Generated.m_Output.f_Find("No changes found"), <, 0);

						TCSet<CStr> Excluded;
						for (auto &Wildcard : CEJsonSorted::fs_FromString(Generated.m_Rules).f_Array()[0]["Tidy"]["ExcludeFiles"].f_Array())
							Excluded[Wildcard.f_String()];
						co_return Excluded;
					}
				;

				{
					// The plugin knows no P4IGNORE, so what mib tidy leaves out is excluded by name.
					DMibTestPath("Tidy");
					auto Rules = CEJsonSorted::fs_FromString(co_await fRules(TidyRoot));
					DMibExpect
						(
							Rules
							, ==
							, CEJsonSorted
							(
								_
								[
									CEJsonSorted
									{
										"Location"_= Repo.m_Path
										, "Tidy"_= CEJsonSorted
										{
											"Enabled"_= true
											, "IncludeFiles"_= _[]
											, "ExcludeFiles"_= _["Generated/*", "B.gen.h", "Ignored/*", "MToolFixture.p4config", "Sub/C.gen.h", "Unknown/*"]
										}
									}
								]
							)
						)
					;
				}

				{
					DMibTestPath("UnchangedBuildKeepsGeneration");
					auto Generated = co_await fGenerate(TidyRoot, false, true);
					DMibExpect(Generated.m_Output.f_Find("No changes found"), >=, 0);
				}

				{
					DMibTestPath("IgnoreFileChangeGeneratesAgain");
					Repo.f_Write(".p4ignore", "/Ignored/\n*.gen.h\n/Sub/\n");
					auto Excluded = co_await fBuildExcluded();
					DMibExpectTrue(Excluded.f_FindEqual("Sub/*"));
				}

				{
					DMibTestPath("IgnoreFileCreatedGeneratesAgain");
					Repo.f_Write("Local/.p4ignore", "Local.h\n");
					auto Excluded = co_await fBuildExcluded();
					DMibExpectTrue(Excluded.f_FindEqual("Local/Local.h"));
				}

				{
					DMibTestPath("GitCreatedGeneratesAgain");
					CFile::fs_CreateDirectory(Repo.m_Path / "Local/.git");
					auto Excluded = co_await fBuildExcluded();
					DMibExpectTrue(Excluded.f_FindEqual("Local/*"));
					CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "Local/.git");
				}

				{
					DMibTestPath("IgnoreEnvironmentChangeGeneratesAgain");
					Repo.f_Write("Custom.ignore", "*.cpp\n");
					Repo.m_Environment["P4IGNORE"] = "Custom.ignore";
					auto Excluded = co_await fBuildExcluded();
					DMibExpectTrue(Excluded.f_FindEqual("A.cpp"));
					DMibExpectFalse(Excluded.f_FindEqual("B.gen.h"));
				}

				auto fExcludedOf = [](CStr const &_Rules)
					{
						TCSet<CStr> Excluded;
						for (auto &Wildcard : CEJsonSorted::fs_FromString(_Rules).f_Array()[0]["Tidy"]["ExcludeFiles"].f_Array())
							Excluded[Wildcard.f_String()];
						return Excluded;
					}
				;

				{
					// The Perforce settings come from the environment generation uses, here the cached one with P4IGNORE set to
					// Custom.ignore, not from the process's.
					DMibTestPath("CachedEnvironment");
					Repo.m_Environment["P4IGNORE"] = ".p4ignore";
					auto Generated = co_await fGenerate(TidyRoot, true, false, true);
					DMibExpectTrue(fExcludedOf(Generated.m_Rules).f_FindEqual("A.cpp"));
				}

				{
					DMibTestPath("EnviroFileChangeGeneratesAgain");
					Repo.m_Environment.f_Remove("P4IGNORE");
					auto Excluded = co_await fBuildExcluded();
					DMibExpectFalse(Excluded.f_FindEqual("A.cpp"));

					{
						DMibTestPath("Created");
						CFile::fs_WriteStringToFile(Repo.m_Root / "p4enviro", "P4IGNORE=Custom.ignore\n", false);
						auto Created = co_await fBuildExcluded();
						DMibExpectTrue(Created.f_FindEqual("A.cpp"));
					}
				}

				{
					// P4CONFIG names a path, which p4 looks for from the root as it does a name.
					DMibTestPath("PathConfigChangeGeneratesAgain");
					Repo.f_Write("Config/p4config", "P4CLIENT=Offline\n");
					Repo.m_Environment["P4CONFIG"] = "Config/p4config";
					auto Excluded = co_await fBuildExcluded();
					DMibExpectTrue(Excluded.f_FindEqual("A.cpp"));

					{
						DMibTestPath("Changed");
						Repo.f_Write("Config/p4config", "P4CLIENT=Offline\nP4IGNORE=.p4ignore\n");
						auto Changed = co_await fBuildExcluded();
						DMibExpectFalse(Changed.f_FindEqual("A.cpp"));
						DMibExpectTrue(Changed.f_FindEqual("Sub/*"));
					}
				}

				{
					// A P4CONFIG file created later makes the checkout a Perforce root.
					DMibTestPath("ConfigCreatedGeneratesAgain");
					Repo.m_Environment["P4CONFIG"] = "Later.p4config";
					auto Generated = co_await fGenerate(TidyRoot, false, true);
					DMibExpect(CEJsonSorted::fs_FromString(Generated.m_Rules), ==, CEJsonSorted(_[]));

					{
						DMibTestPath("Created");
						Repo.f_Write("Later.p4config", "P4CLIENT=Offline\n");
						auto Created = co_await fGenerate(TidyRoot, false, true);
						DMibExpect(Created.m_Output.f_Find("No changes found"), <, 0);
						DMibExpect(CEJsonSorted::fs_FromString(Created.m_Rules).f_Array().f_GetLen(), ==, 1u);
					}
				}

#ifndef DPlatformFamily_Windows
				{
					// A P4CONFIG through a symbolic link and .. is the file p4 opens in the link's target, which makes the checkout a
					// Perforce root, though the name lexically resolved names no file.
					DMibTestPath("LinkedConfigMakesRoot");
					CFile::fs_CreateDirectory(Repo.m_Root / "Shared/Sub");
					CFile::fs_WriteStringToFile(Repo.m_Root / "Shared/Linked.p4config", "P4CLIENT=Offline\n", false);
					CFile::fs_CreateSymbolicLink(Repo.m_Root / "Shared/Sub", Repo.m_Path / "Link", EFileAttrib_Directory, ESymbolicLinkFlag_None);
					Repo.m_Environment["P4CONFIG"] = "Link/../Linked.p4config";
					auto Generated = co_await fGenerate(TidyRoot, false, true);
					DMibExpect(CEJsonSorted::fs_FromString(Generated.m_Rules).f_Array().f_GetLen(), ==, 1u);

					CFile::fs_DeleteFile(Repo.m_Path / "Link");
					Repo.m_Environment["P4CONFIG"] = "Later.p4config";
				}

#endif
				{
					// Without P4CHARSET the charset p4 learned for the root's port is among its settings, so a change of it generates
					// again too.
					DMibTestPath("LearnedCharsetChangeGeneratesAgain");
					Repo.m_Environment["P4PORT"] = "127.0.0.1:1";
					Repo.m_Environment["P4_127.0.0.1:1_CHARSET"] = "none";
					auto Excluded = co_await fBuildExcluded();
					DMibExpectTrue(Excluded.f_FindEqual("Later.p4config"));

					{
						DMibTestPath("Changed");
						Repo.m_Environment["P4_127.0.0.1:1_CHARSET"] = "utf8";
						auto Changed = co_await fBuildExcluded();
						DMibExpectTrue(Changed.f_FindEqual("Later.p4config"));
					}

					Repo.m_Environment.f_Remove("P4_127.0.0.1:1_CHARSET");
					Repo.m_Environment.f_Remove("P4PORT");
				}

				{
					DMibTestPath("NoTidy");
					auto Rules = CEJsonSorted::fs_FromString(co_await fRules(""));
					DMibExpect
						(
							Rules
							, ==
							, CEJsonSorted
							(
								_
								[
									CEJsonSorted
									{
										"Location"_= Repo.m_Path
										, "Tidy"_= CEJsonSorted{"Enabled"_= false, "IncludeFiles"_= _[], "ExcludeFiles"_= _[]}
									}
								]
							)
						)
					;
				}

				co_return {};
			};

			DMibTestCategory("PerforceRootChanges") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "PerforceRootChanges");

				if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
				{
					DMibConOut("{}\n", "Skipping the tidy tests of the changed files of a Perforce root: p4d was not found in PATH");
					co_return {};
				}

				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CStr const Password = "TestPass123";
				TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
				co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
				auto ServerCleanup = co_await fg_AsyncDestroy(Server);
				CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
				co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
				co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

				using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
				int64 FirstChangelist = co_await Server
					(
						&NPerforce::CPerforceTestServer::f_SubmitFiles
						, CStr("//Streams/Main")
						, TCVector<CPerforceFile>{{"A.cpp", ""}, {"B.cpp", ""}, {"Shared.h", ""}, {"Lib/C.cpp", ""}}
						, CStr("Add the sources")
					)
				;
				co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"A.cpp", "int a;\n"}}, CStr("Change A"));

				// The server is open until the password is set below.
				co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
				auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Sync(Workspace / "...");
							Client.f_OpenForEdit(Workspace / "B.cpp");
							Client.f_OpenForEdit(Workspace / "Lib/C.cpp");
						}
						% "Failed to set up the test workspace"
					)
				;

				// The user and workspace come from the root's P4CONFIG file, not from the one of the directory mib runs in.
				Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
				Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
				CFile::fs_CreateDirectory(Repo.m_Root / "Elsewhere");
				CFile::fs_WriteStringToFile(Repo.m_Root / "Elsewhere/MToolFixture.p4config", "P4USER=elsewhere\nP4CLIENT=Elsewhere\n", false);

				Repo.f_Write("Lib/C.cpp", "");
				auto fGitInLib = [&](TCVector<CStr> _Params) -> TCUnsafeFuture<CStr>
					{
						auto Result = co_await Repo.f_Run("git", fg_Move(_Params), Repo.m_Path / "Lib");
						if (Result.m_ExitCode)
							co_return DMibErrorInstance("Git failed in Lib: {}"_f << Result.f_GetCombinedOut());

						co_return Result.f_GetStdOut().f_Trim();
					}
				;
				co_await fGitInLib({"init", "-q", "-b", "main"});
				co_await fGitInLib({"add", "-A"});
				co_await fGitInLib({"commit", "-qm", "First"});
				CStr LibFirst = co_await fGitInLib({"rev-parse", "HEAD"});
				Repo.f_Write("Lib/C.cpp", "int c;\n");
				co_await fGitInLib({"commit", "-qam", "Change C"});

				CTidyFixture Tidy(Repo);
				Tidy.m_WorkingDirectory = Repo.m_Root / "Elsewhere";
				Tidy.f_ConfigurePerforceRoot("{Enabled: true}");
				Tidy.f_AddDatabase("Main", 200, "Lib_A", "Debug", 101, {"A.cpp", "B.cpp", "Lib/C.cpp"}, "DEBUG");

				{
					DMibTestPath("OpenServer");
					auto Result = co_await Tidy.f_Run({"--staged"});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"B.cpp"}));
				}

				// mib uses the ticket the fixture's login leaves.
				co_await Server(&NPerforce::CPerforceTestServer::f_SetUserPassword, Password);
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Password]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(Password);
						}
						% "Failed to log in to the test server"
					)
				;

				{
					DMibTestPath("Staged");
					auto Result = co_await Tidy.f_Run({"--staged"});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"B.cpp"}));
				}

				{
					DMibTestPath("Base");
					auto Result = co_await Tidy.f_Run({"--base", "@{}"_f << FirstChangelist});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp"}));
				}

				{
					DMibTestPath("LargeBase");
					auto Large = co_await Tidy.f_Run({"--base", "@4294967296"});
					DMibExpect(Large.f_GetCombinedOut().f_Find("which '@4294967296' is not"), <, 0);
					auto Largest = co_await Tidy.f_Run({"--base", "@9223372036854775807"});
					DMibExpect(Largest.m_ExitCode, !=, 0u);
					DMibExpect(Largest.f_GetCombinedOut().f_Find("which '@9223372036854775807' is not"), >=, 0);
				}

				Tidy.f_ConfigurePerforceRoot("{Enabled: true}", "{Enabled: true}");

				{
					// A file of a repository in the root is that repository's, whose index has no change, although the
					// workspace has it open.
					DMibTestPath("StagedNestedRepository");
					auto Result = co_await Tidy.f_Run({"--staged"});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"B.cpp"}));
				}

				{
					DMibTestPath("MixedBases");
					auto Result = co_await Tidy.f_Run({"--base", LibFirst, "--perforce-base", "@{}"_f << FirstChangelist});
					DMibExpect(Result.m_ExitCode, ==, 1u);
					DMibExpect(Tidy.f_Checked(), ==, (TCVector<CStr>{"A.cpp", "Lib/C.cpp"}));
				}

				// A numeric git reference reads as a changelist too, so the root's base is given on its own.
				for (CStr const &Base : {CStr("@{}"_f << FirstChangelist), CStr::fs_ToStr(FirstChangelist), LibFirst})
				{
					DMibTestPath("OneBaseForBoth" + Base);
					auto Result = co_await Tidy.f_Run({"--base", Base});
					DMibExpect(Result.m_ExitCode, !=, 0u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("cannot be the base of both the git repositories and the Perforce root"), >=, 0);
					DMibExpect(Tidy.f_Checked(), ==, TCVector<CStr>{});
				}

				co_return {};
			};

			co_return {};
		};
	}

	DMibTestRegister(CTidy_Tests, Malterlib::Tool);
}
