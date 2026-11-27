// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Core/OnScopeExitCatch>
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

#ifndef DPlatformFamily_Windows
#	include <sys/stat.h>
#	include <unistd.h>
#endif

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		CStr const g_FormatConfiguration =
			"root = true\n\n[*]\nindent_style = tab\nindent_size = 4\ntab_width = 4\nmax_line_length = 190\n\n"
			"[*.{c,cpp,h,hpp}]\nmalterlib_format = malterlib\n"
		;
		constexpr CStr gc_Unformatted = gc_Str<"void fg_Test()\n{\n\n    int a;   \n\tif(a==2)\n\t\tfg_Other(a,1);\n}\n">;
		constexpr CStr gc_Formatted = gc_Str<"void fg_Test()\n{\n\tint a;\n\tif (a == 2)\n\t\tfg_Other(a, 1);\n}\n">;

		struct CRejectedCase
		{
			CStr m_Name;
			TCVector<CStr> m_Params;
		};
	}

	struct CFormat_Tests : CTest
	{
		void f_DoTests();
	};

	void CFormat_Tests::f_DoTests()
	{
		DMibTestSuite("Selection") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format selection");

			DMibTestCategory("RequiresSelector") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "RequiresSelector");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path});
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("requires at least one --file or --pattern"), >=, 0);

				co_return {};
			};

			DMibTestCategory("NoMatchesIsAnInputError") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "NoMatchesIsAnInputError");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "src/*.cpp"});
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("No files matched"), >=, 0);

#ifndef DPlatformFamily_Windows
				{
					// A match whose name holds a backslash, which the file functions read as a separator, is reported unchecked also
					// where it is the only one.
					DMibTestPath("BackslashOnly");
					CFile::fs_CreateDirectory(Repo.m_Path / "src");
					CStr Backslashed = Repo.m_Path + "/src/Back\\slash.cpp";
					DMibAssertTrue(fg_WriteNativeFile(Backslashed, ""));
					auto RemoveBackslashed = g_OnScopeExit / [&]
						{
							unlink(Backslashed.f_GetStr());
						}
					;

					auto Only = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "src/*.cpp"});
					DMibExpect(Only.m_ExitCode, ==, 2u);
					DMibExpect(Only.f_GetCombinedOut().f_Find("Back\\slash.cpp: not checked"), >=, 0);
				}
#endif

				co_return {};
			};

			DMibTestCategory("ExcludedFilesAreASuccessfulNoOp") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "ExcludedFilesAreASuccessfulNoOp");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("Notes.md", "Prose that is not code.\n");
				Repo.f_Write("Source.cpp", gc_Unformatted);

				// Without the opt-in property every candidate is excluded, including C++.
				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "*", "--recursive"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("0 changed"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Unformatted);

				co_return {};
			};

			DMibTestCategory("RepositoryExcludesPruneDirectories") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "RepositoryExcludesPruneDirectories");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source.cpp", gc_Formatted);
				Repo.f_Write("Generated/Bad.cpp", gc_Unformatted);
				Repo.f_Write("Local/Bad.cpp", gc_Unformatted);
				Repo.f_Write("Ignored/Bad.cpp", gc_Unformatted);
				Repo.f_Write(".gitignore", "/Ignored/\n");
				CFile::fs_WriteStringToFile(Repo.m_Path / ".git/info/exclude", "Local/\n", false);
				CFile::fs_WriteStringToFile(Repo.m_Root / "excludes", "Generated/\n", false);
				CFile::fs_WriteStringToFile
					(
						Repo.m_Root / "gitconfig"
						, CFile::fs_ReadStringFromFile(Repo.m_Root / "gitconfig", true) + "\texcludesFile = " + Repo.m_Root / "excludes" + "\n"
						, false
					)
				;

				// The three ignored trees are never listed, so their violations go unseen; the
				// listing holds the source and the two configuration files.
				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "*", "--recursive", "--check"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Formatted 3 file(s): 1 unchanged, 0 would change"), >=, 0);

				// A pattern starting in an ignored directory meets nothing either.
				auto Started = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Ignored/*.cpp", "--check"});
				DMibExpect(Started.f_GetCombinedOut().f_Find("would change"), <, 0);

				co_return {};
			};

			DMibTestCategory("NestedRepositories") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "NestedRepositories");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source.cpp", gc_Formatted);
				Repo.f_Write("Plain/Bad.cpp", gc_Unformatted);
				Repo.f_Write(".gitignore", "/Nested/\n/Plain/\n");
				co_await Repo.f_Git({"init", "-q", "Nested"});
				Repo.f_Write("Nested/.editorconfig", g_FormatConfiguration);
				Repo.f_Write("Nested/.gitignore", "/Ignored/\n");
				Repo.f_Write("Nested/Bad.cpp", gc_Unformatted);
				Repo.f_Write("Nested/Ignored/Bad.cpp", gc_Unformatted);

				TCVector<CStr> Params = {"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "*.cpp", "--recursive", "--check"};
				auto Outer = co_await Repo.f_Tool(Params);
				DMibExpect(Outer.m_ExitCode, ==, 0u);
				DMibExpect(Outer.f_GetCombinedOut().f_Find("Formatted 1 file(s): 1 unchanged, 0 would change"), >=, 0);

				// An ignored directory that is a repository of its own is entered, under its own ignore rules; an ignored
				// directory that is not stays pruned.
				Params.f_Insert("--nested-repositories");
				auto Nested = co_await Repo.f_Tool(Params);
				DMibExpect(Nested.m_ExitCode, ==, 1u);
				DMibExpect(Nested.f_GetCombinedOut().f_Find("Formatted 2 file(s): 1 unchanged, 1 would change"), >=, 0);
				DMibExpect(Nested.f_GetCombinedOut().f_Find("Nested/Bad.cpp"), >=, 0);

				// So is one the repository around it tracks as a gitlink, whose paths are its own index's, not the outer one's.
				co_await Repo.f_Git({"-C", "Nested", "add", "Bad.cpp"});
				co_await Repo.f_Git({"-C", "Nested", "commit", "-q", "-m", "Nested"});
				co_await Repo.f_Git({"add", "-f", "Nested"});
				auto Linked = co_await Repo.f_Tool(Params);
				DMibExpect(Linked.m_ExitCode, ==, 1u);
				DMibExpect(Linked.f_GetCombinedOut().f_Find("Nested/Bad.cpp"), >=, 0);

				co_return {};
			};

			DMibTestCategory("PatternsAndFilesDeduplicate") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "PatternsAndFilesDeduplicate");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source/Deep/Example.cpp", gc_Unformatted);

				auto Result = co_await Repo.f_Tool
					(
						{
							"Format"
							, "--no-color"
							, "-C"
							, Repo.m_Path
							, "--check"
							, "--file"
							, "Source/Deep/Example.cpp"
							, "--pattern"
							, "Source/*.cpp,Source/*.h"
							, "--recursive"
						}
					)
				;
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Formatted 1 file(s)"), >=, 0);

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Modes") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format modes");

			DMibTestCategory("CheckAndDiffDoNotWrite") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "CheckAndDiffDoNotWrite");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", gc_Unformatted);

				auto fExpectUnchanged = [&](CStr const &_Case)
					{
						DMibTestPath(_Case);
						DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, gc_Unformatted);
					}
				;

				auto Check = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--check"});
				DMibExpect(Check.m_ExitCode, ==, 1u);
				DMibExpect(Check.f_GetCombinedOut().f_Find("indentation must use tabs"), >=, 0);
				fExpectUnchanged("AfterCheck");

				auto Diff = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--diff"});
				DMibExpect(Diff.m_ExitCode, ==, 1u);
				DMibExpect(Diff.f_GetStdOut().f_Find("--- a/Example.cpp"), >=, 0);
				DMibExpect(Diff.f_GetStdOut().f_Find("+\tif (a == 2)"), >=, 0);
				fExpectUnchanged("AfterDiff");

				auto Both = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--check", "--diff"});
				DMibExpect(Both.m_ExitCode, ==, 2u);
				DMibExpect(Both.f_GetCombinedOut().f_Find("--check and --diff cannot be combined"), >=, 0);

				co_return {};
			};

			// The walk starts in no directory through a symbolic link, and a directory it cannot list fails the run rather than
			// reading as empty.
			DMibTestCategory("WalkBoundaries") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "WalkBoundaries");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				CFile::fs_CreateDirectory(Repo.m_Root / "Plain");
				CFile::fs_WriteStringToFile(Repo.m_Root / "Plain/.editorconfig", g_FormatConfiguration, false);
				CFile::fs_WriteStringToFile(Repo.m_Root / "Plain/Example.cpp", gc_Unformatted, false);
				CFile::fs_CreateSymbolicLink(Repo.m_Root / "Plain", Repo.m_Root / "PlainLink", EFileAttrib_Directory, ESymbolicLinkFlag_None);

				auto Linked = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Root / "PlainLink", "--pattern", "*.cpp"});
				DMibExpect(Linked.m_ExitCode, !=, 0u);
				DMibExpect(Linked.f_GetCombinedOut().f_Find("Refusing to walk the symbolic link"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Root / "Plain/Example.cpp", true), ==, gc_Unformatted);

				CFile::fs_CreateDirectory(Repo.m_Path / "Sub");
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Sub/Example.cpp", gc_Unformatted);
				CFile::fs_CreateSymbolicLink(Repo.m_Path, Repo.m_Root / "RepoLink", EFileAttrib_Directory, ESymbolicLinkFlag_None);
				auto Below = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Root, "--pattern", "RepoLink/Sub/*.cpp"});
				DMibExpect(Below.m_ExitCode, !=, 0u);
				DMibExpect(Below.f_GetCombinedOut().f_Find("Refusing to walk the symbolic link"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Sub/Example.cpp", true), ==, gc_Unformatted);

				// A file named outright is not reached through a linked directory either.
				CFile::fs_CreateSymbolicLink(Repo.m_Root / "Plain", Repo.m_Path / "Linked", EFileAttrib_Directory, ESymbolicLinkFlag_None);
				auto Named = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Linked/Example.cpp"});
				{
					DMibTestPath("NamedFile");
					DMibExpect(Named.m_ExitCode, !=, 0u);
					DMibExpect(Named.f_GetCombinedOut().f_Find("through the symbolic link"), >=, 0);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Root / "Plain/Example.cpp", true), ==, gc_Unformatted);
				}

				// Only the Unix permission bits can take a directory's listing away here.
#ifndef DPlatformFamily_Windows
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Locked/Example.cpp", gc_Unformatted);
				auto LockedAttributes = CFile::fs_GetAttributes(Repo.m_Path / "Locked");
				CFile::fs_SetAttributes(Repo.m_Path / "Locked", EFileAttrib_UnixAttributesValid | EFileAttrib_UserWrite);
				auto Locked = co_await Repo.f_Tool({"Format", "--no-color", "--check", "-C", Repo.m_Path, "--recursive", "--pattern", "*.cpp"});
				CFile::fs_SetAttributes(Repo.m_Path / "Locked", LockedAttributes | EFileAttrib_UnixAttributesValid);
				DMibExpect(Locked.m_ExitCode, !=, 0u);
				DMibExpect(Locked.f_GetCombinedOut().f_Find("cannot be listed"), >=, 0);
#endif

				co_return {};
			};

			// Repositories side by side under the working directory name their files from it, so their patches stay apart.
			DMibTestCategory("DiffAcrossRepositories") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "DiffAcrossRepositories");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", gc_Unformatted);
				co_await Repo.f_Git({"-C", Repo.m_Root, "init", "-q", "Other"});
				CFile::fs_WriteStringToFile(Repo.m_Root / "Other/.editorconfig", g_FormatConfiguration, false);
				CFile::fs_WriteStringToFile(Repo.m_Root / "Other/Example.cpp", gc_Unformatted, false);

				auto RepoName = CFile::fs_GetFile(Repo.m_Path);
				auto Diff = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Root, "--file", RepoName + "/Example.cpp,Other/Example.cpp", "--diff"});
				DMibExpect(Diff.f_GetStdOut().f_Find("--- a/" + RepoName + "/Example.cpp"), >=, 0);
				DMibExpect(Diff.f_GetStdOut().f_Find("--- a/Other/Example.cpp"), >=, 0);

				// From inside a repository its files are named from its root, as git names them.
				auto Inside = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--diff"});
				DMibExpect(Inside.f_GetStdOut().f_Find("--- a/Example.cpp"), >=, 0);

				// So are the files of a repository beside the working directory.
				auto Beside = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "../Other/Example.cpp", "--diff"});
				DMibExpect(Beside.f_GetStdOut().f_Find("--- a/Example.cpp"), >=, 0);
				DMibExpect(Beside.f_GetStdOut().f_Find("../"), ==, -1);

				co_return {};
			};

			// A selection failing behind queued work stops the run with the selection's error, the queued files never started, and
			// names the files it changed before.
			DMibTestCategory("SelectionFailsBehindQueuedWork") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "SelectionFailsBehindQueuedWork");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				CStr Files;
				for (umint iFile = 0; iFile < 100; ++iFile)
				{
					Repo.f_Write("File{}.cpp"_f << iFile, gc_Unformatted);
					Files += "File{}.cpp,"_f << iFile;
				}

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "--check", "--jobs", "1", "-C", Repo.m_Path, "--file", Files + "Missing.cpp"});
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("is not an existing regular file"), >=, 0);

				auto Written = co_await Repo.f_Tool({"Format", "--no-color", "--jobs", "1", "-C", Repo.m_Path, "--file", Files + "Missing.cpp"});
				DMibExpect(Written.m_ExitCode, ==, 2u);
				for (umint iFile = 0; iFile < 100; ++iFile)
				{
					if (CFile::fs_ReadStringFromFile(Repo.m_Path / ("File{}.cpp"_f << iFile), true) == gc_Unformatted)
						continue;

					DMibTestPath("File{}"_f << iFile);
					DMibExpect(Written.f_GetCombinedOut().f_Find(CStr("/File{}.cpp' was changed before the selection failed"_f << iFile)), >=, 0);
				}

				co_return {};
			};

			// A changed line starting with a NUL is diffed as the bytes it holds.
			DMibTestCategory("DiffLineStartingWithNul") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "DiffLineStartingWithNul");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				CStr Text = "/*\n@*/    \n";
				NContainer::CByteVector Bytes;
				Bytes.f_Insert((uint8 const *)Text.f_GetStr(), Text.f_GetLen());
				Bytes[3] = 0;
				CFile::fs_WriteFile(Repo.m_Path / "Example.cpp", Bytes);

				auto Diff = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--diff"});
				DMibExpect(Diff.m_ExitCode, ==, 1u);
				// The hunk holds the changed line, whose NUL the captured output stops at.
				DMibExpect(Diff.f_GetStdOut().f_Find("@@ -1,2 +1,2 @@\n /*\n-"), >=, 0);

				co_return {};
			};

			DMibTestCategory("DiffJoinsLines") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "DiffJoinsLines");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "int X =\n1;\nint Y;\n");

				auto Diff = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--diff"});
				DMibExpect(Diff.m_ExitCode, ==, 1u);
				DMibExpect(Diff.f_GetStdOut().f_Find("@@ -1,3 +1,2 @@\n-int X =\n-1;\n+int X = 1;\n int Y;\n"), >=, 0);

				co_return {};
			};

			DMibTestCategory("WriteIsIdempotent") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "WriteIsIdempotent");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", gc_Unformatted);
				// A file beside the one formatted that a user keeps is no staging file of the run.
				Repo.f_Write("Example.cpp.mtool-format", "Kept\n");

				auto fExpectFormatted = [&](CStr const &_Case)
					{
						DMibTestPath(_Case);
						DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, gc_Formatted);
					}
				;

				auto First = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp"});
				DMibExpect(First.m_ExitCode, ==, 0u);
				fExpectFormatted("AfterFirstPass");

				auto Second = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp"});
				DMibExpect(Second.m_ExitCode, ==, 0u);
				DMibExpect(Second.f_GetCombinedOut().f_Find("1 unchanged"), >=, 0);
				fExpectFormatted("AfterSecondPass");
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp.mtool-format", true), ==, "Kept\n");
				DMibExpect(CFile::fs_FindFiles(Repo.m_Path / "Example.cpp.mtool-format-*").f_GetLen(), ==, 0u);

				co_return {};
			};

			DMibTestCategory("UnresolvedViolationsRemainVisible") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "UnresolvedViolationsRemainVisible");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "auto g_Text = \"" + fg_Repeat("x", 200) + "\";\n");

				// An indivisible overlong literal cannot be fixed, so it stays reported.
				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("line-length: line length"), >=, 0);

				co_return {};
			};

			DMibTestCategory("UnparsableSourceIsAReportedFailure") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "UnparsableSourceIsAReportedFailure");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "/* unterminated comment\n");

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp"});
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("ends inside a comment or literal"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, "/* unterminated comment\n");

				co_return {};
			};

			DMibTestCategory("ParallelJobsMatchASingleJob") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "ParallelJobsMatchASingleJob");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				for (umint iFile = 0; iFile < 12; ++iFile)
					Repo.f_Write("Source/Example{}.cpp"_f << iFile, gc_Unformatted);

				auto Single = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check", "--jobs", "1"});
				auto Parallel = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check", "--jobs", "8"});
				// The summary carries an elapsed time, so only the diagnostics are compared.
				auto fDiagnostics = [](CStr const &_Output)
					{
						return _Output.f_Left(_Output.f_Find("Formatted 12 file(s)"));
					}
				;

				DMibExpect(Single.m_ExitCode, ==, 1u);
				DMibExpect(Parallel.m_ExitCode, ==, Single.m_ExitCode);
				DMibExpect(Single.f_GetCombinedOut().f_Find("Formatted 12 file(s)"), >=, 0);
				DMibExpect(fDiagnostics(Parallel.f_GetCombinedOut()), ==, fDiagnostics(Single.f_GetCombinedOut()));

				co_return {};
			};
			DMibTestCategory("SlowestFiles") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "SlowestFiles");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				for (umint iFile = 0; iFile < 3; ++iFile)
					Repo.f_Write("Source/Example{}.cpp"_f << iFile, gc_Unformatted);

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check", "--slowest", "2"});
				auto Output = Result.f_GetCombinedOut();
				auto iList = Output.f_Find("The files that took the longest:\n");
				DMibExpect(iList, >=, 0);

				umint nListed = 0;
				for (umint iFile = 0; iFile < 3; ++iFile)
				{
					CStr Line = " s  {}"_f << CFile::fs_AppendPath(Repo.m_Path, CStr("Source/Example{}.cpp"_f << iFile));
					nListed += Output.f_Find(Line) >= 0;
				}

				DMibExpect(nListed, ==, 2u);

				auto Plain = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check"});
				DMibExpect(Plain.f_GetCombinedOut().f_Find("took the longest"), <, 0);

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Ranges") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format ranges");

			DMibTestCategory("LineRange") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "LineRange");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "void fg_Test()\n{\n    int a;\n    int b;\n}\n");

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--lines", "3:3"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, "void fg_Test()\n{\n\tint a;\n    int b;\n}\n");

				co_return {};
			};

			DMibTestCategory("ByteRangeAndStrict") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "ByteRangeAndStrict");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				CStr Source = "void fg_Test()\n{\n    int a;\n    int b;\n}\n";
				Repo.f_Write("Example.cpp", Source);

				// A partial selection expands to the line it touches by default.
				auto Expanded = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--offset", "20", "--length", "2"});
				DMibExpect(Expanded.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, CStr("void fg_Test()\n{\n\tint a;\n    int b;\n}\n"));

				Repo.f_Write("Example.cpp", Source);
				auto Strict = co_await Repo.f_Tool
					(
						{
							"Format"
							, "--no-color"
							, "-C"
							, Repo.m_Path
							, "--file"
							, "Example.cpp"
							, "--offset"
							, "20"
							, "--length"
							, "2"
							, "--strict-range"
						}
					)
				;
				DMibExpect(Strict.m_ExitCode, ==, 1u);
				DMibExpect(Strict.f_GetCombinedOut().f_Find("range-boundary"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, Source);

				co_return {};
			};

			DMibTestCategory("RejectedCombinations") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "RejectedCombinations");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", gc_Unformatted);
				Repo.f_Write("Other.cpp", gc_Unformatted);

				TCVector<CRejectedCase> Cases =
					{
						{"LinesWithOffset", {"--file", "Example.cpp", "--lines", "1:2", "--offset", "0", "--length", "1"}}
						, {"OffsetWithoutLength", {"--file", "Example.cpp", "--offset", "0"}}
						, {"RangeWithSeveralFiles", {"--file", "Example.cpp,Other.cpp", "--lines", "1:2"}}
						, {"RangeWithPattern", {"--pattern", "*.cpp", "--lines", "1:2"}}
						, {"ReversedLines", {"--file", "Example.cpp", "--lines", "3:1"}}
						, {"LinesPastEnd", {"--file", "Example.cpp", "--lines", "1:900"}}
					}
				;
				for (auto const &Case : Cases)
				{
					DMibTestPath(Case.m_Name);
					TCVector<CStr> Params = {"Format", "--no-color", "-C", Repo.m_Path};
					Params.f_Insert(Case.m_Params);

					auto Result = co_await Repo.f_Tool(fg_Move(Params));
					DMibExpect(Result.m_ExitCode, ==, 2u);
				}

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Server") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format --server");

			CStr const LongType = "NMib::NCryptography::TCHashImpl<TCHashImpl_BoringSSL<EDigestType_SHA256, 32>>";
			CStr const SplitType = "NMib::NCryptography::TCHashImpl\n\t<\n\t\tTCHashImpl_BoringSSL<EDigestType_SHA256, 32>\n\t>";

			// The lines as requests: an object that names no request is a fragment, with the columns when it has none.
			auto fFragments = [](CStr const &_Lines, umint _nMaxColumns) -> CStr
				{
					CStr Requests;
					bool bFirst = true;
					for (auto const &Line : _Lines.f_Split("\n"))
					{
						if (!bFirst)
							Requests += "\n";

						bFirst = false;
						if (!Line.f_StartsWith("{"))
						{
							Requests += Line;

							continue;
						}

						auto Json = CEJsonSorted::fs_FromString(Line);
						if (!Json.f_GetMember("request"))
						{
							Json["request"] = "fragment";
							if (_nMaxColumns && !Json.f_GetMember("maxColumns"))
								Json["maxColumns"] = _nMaxColumns;
						}

						Requests += Json.f_ToString(nullptr).f_Trim();
					}

					return Requests;
				}
			;

			DMibTestCategory("KindsAndColumns") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "KindsAndColumns");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// A name of 60 columns, and one of 61.
				CStr Fits = "TC" + fg_Repeat("A", 51) + "<int32>";
				CStr TooLong = "TC" + fg_Repeat("A", 52) + "<int32>";

				TCVector<CStr> Fragments =
					{
						"TCMap<CStr, TCVector<int32>> const &"
						, LongType
						, "void NMib::NFoo::CBar::f_Baz(CStr const &_Name, TCMap<CStr, TCVector<int32>> const &_Map, int32 _Other)"
						, "fg_Function(Argument1, fg_Other(Argument2, Argument3), TCVector<int32>{1, 2, 3}.f_GetLen())"
						, Fits
						, TooLong
						, "(some_object.long_member_name + another_object.long_member_name) * long_multiplier"
						, "SomeReallyLongNameThatIsLong += SomeOtherVeryLongNameThatIsAlsoLong"
						, "fg_Function(Argument1, Argument2) // note"
						, "int a; int b"
						, "fg_Function(R\"(a \" // b; c)\", 1'000)"
						, "int a; int b;"
						, "fg_Function(); // note"
						, "const int a;"
						, "int a; /* note */"
						, "fg_Function(ArgumentOne, ArgumentTwo, ArgumentThree, ArgumentFour);"
					}
				;
				TCVector<CStr> Expected =
					{
						"TCMap<CStr, TCVector<int32>> const &"
						, SplitType
						, "void NMib::NFoo::CBar::f_Baz\n\t(\n\t\tCStr const &_Name\n\t\t, TCMap<CStr, TCVector<int32>> const &_Map\n\t\t, int32 _Other\n\t)"
						, "fg_Function\n\t(\n\t\tArgument1\n\t\t, fg_Other(Argument2, Argument3)\n\t\t, TCVector<int32>{1, 2, 3}.f_GetLen()\n\t)"
						, Fits
						, "TC" + fg_Repeat("A", 52) + "\n\t<\n\t\tint32\n\t>"
						, "(\n\tsome_object.long_member_name\n\t+ another_object.long_member_name\n)\n* long_multiplier"
						, "SomeReallyLongNameThatIsLong\n\t+= SomeOtherVeryLongNameThatIsAlsoLong"
						, "fg_Function\n\t(\n\t\tArgument1\n\t\t, Argument2\n\t) // note"
						, "int a;\nint b"
						, "fg_Function(R\"(a \" // b; c)\", 1'000)"
						, "int a;\nint b;"
						, "fg_Function(); // note"
						, "int const a;"
						, "int a; // note"
						, "fg_Function\n\t(\n\t\tArgumentOne\n\t\t, ArgumentTwo\n\t\t, ArgumentThree\n\t\t, ArgumentFour\n\t)\n;"
					}
				;

				CStr Input;
				for (auto const &Fragment : Fragments)
				{
					CEJsonSorted Json;
					Json["text"] = Fragment;
					Input += Json.f_ToString(nullptr).f_Trim();
					Input += "\n";
				}

				auto Result = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--server"}, fFragments(Input, 60));
				DMibExpect(Result.m_ExitCode, ==, 0u);

				TCVector<CStr> Formatted;
				auto StdOut = Result.f_GetStdOut();
				for (auto const &Line : StdOut.f_SplitLine())
				{
					if (Line)
						Formatted.f_Insert(CEJsonSorted::fs_FromString(Line)["text"].f_String());
				}

				DMibExpect(Formatted, ==, Expected);

				co_return {};
			};

			DMibTestCategory("MembersAndErrors") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "MembersAndErrors");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CEJsonSorted Wide;
				Wide["text"] = LongType;
				Wide["maxColumns"] = 100;
				CEJsonSorted Typed;
				Typed["text"] = LongType;
				Typed["kind"] = "type";

				CStr Input = Wide.f_ToString(nullptr).f_Trim();
				Input += "\n";
				Input += Typed.f_ToString(nullptr).f_Trim();
				Input += "\nnot json\n\n{\"kind\": \"type\"}\n{\"text\": \"int\", \"kind\": \"statement\"}\n";
				Input += "{\"text\": \"TCVector<\\r\\nint, double>\"}\n{\"text\": \"  \"}\n{\"text\": \"int\\n\"}\n";
				Input += "{\"text\": \"again: fg_First(); fg_Second();\"}\n{\"text\": \"#define DTest 1\"}\n{\"text\": \"fg_Function(\\\"unterminated)\"}\n";
				Input += "{\"text\": \"fg_Function(); // note\", \"maxColumns\": 20}\n";
				Input += "{\"text\": \"int a; /* A long comment ending in a semicolon; */\", \"maxColumns\": 20}\n";
				Input += "{\"text\": \"fg_Function() // note\", \"maxColumns\": 20}\n";
				Input += "{\"text\": \"fg_First(); } { fg_Second();\"}\n{\"text\": \"fg_First(a[1)]\"}\n";

				auto Result = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--server"}, fFragments(Input, 60));
				DMibExpect(Result.m_ExitCode, ==, 0u);

				TCVector<CEJsonSorted> Lines;
				auto StdOut = Result.f_GetStdOut();
				for (auto const &Line : StdOut.f_SplitLine())
				{
					if (Line)
						Lines.f_Insert(CEJsonSorted::fs_FromString(Line));
				}

				// A blank line gets no answer, and a line that is no fragment gets its error where its text would be.
				DMibExpect(Lines.f_GetLen(), ==, 16u);
				if (Lines.f_GetLen() == 16)
				{
					// Brackets that don't pair would end the block the fragment is formatted in.
					DMibExpect(Lines[14]["error"].f_String().f_Find("brackets of a fragment pair"), >=, 0);
					DMibExpect(Lines[15]["error"].f_String().f_Find("brackets of a fragment pair"), >=, 0);
					// A label stands out of the statement's lines, and a directive or an open literal is no statement.
					DMibExpectTrue(Lines[8].f_GetMember("error"));
					DMibExpect(Lines[9]["error"].f_String().f_Find("preprocessor directive"), >=, 0);
					DMibExpect(Lines[10]["error"].f_String().f_Find("inside a comment or a literal"), >=, 0);

					// A fragment keeps the ';' it ends in, also in front of a comment, and is split within its columns.
					DMibExpect(Lines[11]["text"].f_String().f_Find("fg_Function();"), >=, 0);
					DMibExpect(Lines[12]["text"].f_String().f_Find("int a;"), >=, 0);
					DMibExpect(Lines[13]["text"].f_String().f_Find("\n"), >=, 0);
					// A fragment on several lines is none, also when the line break ends it, and an empty one stays empty.
					DMibExpect(Lines[5]["error"].f_String().f_Find("one line of code"), >=, 0);
					DMibExpect(Lines[6]["text"].f_String(), ==, CStr());
					DMibExpect(Lines[7]["error"].f_String().f_Find("one line of code"), >=, 0);
					DMibExpect(Lines[0]["text"].f_String(), ==, LongType);
					DMibExpect(Lines[1]["text"].f_String(), ==, SplitType);
					DMibExpectTrue(Lines[2].f_GetMember("error"));
					DMibExpect(Lines[3]["error"].f_String().f_Find("string member 'text'"), >=, 0);
					DMibExpect(Lines[4]["error"].f_String().f_Find("Unknown kind 'statement'"), >=, 0);
				}

				// The indentation of a split fragment is the one asked for.
				auto Spaced = Typed;
				Spaced["indentStyle"] = "space";
				Spaced["indentSize"] = 2;
				auto Spaces = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--server"}, fFragments(Spaced.f_ToString(nullptr).f_Trim() + "\n", 60));
				DMibExpect(Spaces.m_ExitCode, ==, 0u);
				DMibExpect
					(
						CEJsonSorted::fs_FromString(Spaces.f_GetStdOut())["text"].f_String()
						, ==
						, CStr("NMib::NCryptography::TCHashImpl\n  <\n    TCHashImpl_BoringSSL<EDigestType_SHA256, 32>\n  >")
					)
				;

				// A line gives the directory and indentation of its own fragment, asks for the program's path, and is
				// answered at the end of stdin without a line end. The answer has the request's id.
				CEJsonSorted Indented;
				Indented["text"] = LongType;
				Indented["indentStyle"] = "space";
				Indented["indentSize"] = 3;
				Indented["directory"] = Repo.m_Path;
				auto Own = co_await Repo.f_ToolWithStdIn
					(
						{"Format", "--no-color", "-C", Repo.m_Path / "Missing", "--server"}
						, fFragments
						(
							"{\"request\": \"executable\", \"id\": \"first\"}\n"
							+ Indented.f_ToString(nullptr).f_Trim()
							+ "\n{\"text\": \"int a\", \"indentSize\": 0, \"id\": 3}\n{\"text\": \"int  b\"}\n{\"request\": \"other\"}\n{\"text\": \"int\", \"id\": []}"
							, 60
						)
					)
				;
				DMibExpect(Own.m_ExitCode, ==, 0u);

				TCVector<CEJsonSorted> OwnLines;
				auto OwnOut = Own.f_GetStdOut();
				for (auto const &Line : OwnOut.f_SplitLine())
				{
					if (Line)
						OwnLines.f_Insert(CEJsonSorted::fs_FromString(Line));
				}

				DMibExpect(OwnLines.f_GetLen(), ==, 6u);
				if (OwnLines.f_GetLen() == 6)
				{
					DMibExpect(OwnLines[0]["id"].f_String(), ==, CStr("first"));
					DMibExpect(OwnLines[2]["id"].f_Integer(), ==, 3);
					DMibExpectFalse(OwnLines[3].f_GetMember("id"));
					DMibExpect(OwnLines[4]["error"].f_String().f_Find("Unknown request 'other'"), >=, 0);
					DMibExpect(OwnLines[5]["error"].f_String().f_Find("'id' is a string or an integer"), >=, 0);
					DMibExpect(OwnLines[0]["executable"].f_String().f_Find("MTool"), >=, 0);
					DMibExpect(OwnLines[1]["text"].f_String(), ==, CStr("NMib::NCryptography::TCHashImpl\n   <\n      TCHashImpl_BoringSSL<EDigestType_SHA256, 32>\n   >"));
					DMibExpect(OwnLines[2]["error"].f_String().f_Find("'indentSize' is a number from 1 to 1000"), >=, 0);
					DMibExpect(OwnLines[3]["text"].f_String(), ==, CStr("int b"));
				}

				// A style that can't be read is the error of the fragments of its directory, which is one of the working
				// directory, and the others are answered.
				Repo.f_Write("Broken/.malterlib-format", "Version: 100\n");
				CEJsonSorted Broken;
				Broken["text"] = "int  a";
				Broken["directory"] = "Broken";
				auto Styles = co_await Repo.f_ToolWithStdIn
					(
						{"Format", "--no-color", "-C", Repo.m_Path, "--server"}
						, fFragments("{\"text\": \"int  b\"}\n" + Broken.f_ToString(nullptr).f_Trim() + "\n{\"text\": \"int  c\"}\n", 0)
					)
				;
				DMibExpect(Styles.m_ExitCode, ==, 0u);

				TCVector<CEJsonSorted> StyleLines;
				auto StylesOut = Styles.f_GetStdOut();
				for (auto const &Line : StylesOut.f_SplitLine())
				{
					if (Line)
						StyleLines.f_Insert(CEJsonSorted::fs_FromString(Line));
				}

				DMibExpect(StyleLines.f_GetLen(), ==, 3u);
				if (StyleLines.f_GetLen() == 3)
				{
					DMibExpect(StyleLines[0]["text"].f_String(), ==, CStr("int b"));
					DMibExpect(StyleLines[1].f_GetMember("error"), !=, nullptr);
					DMibExpect(StyleLines[2]["text"].f_String(), ==, CStr("int c"));
				}

				// Without columns every fragment stays on one line, and the other selections don't combine.
				auto Unlimited = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--server"}, fFragments(Typed.f_ToString(nullptr).f_Trim() + "\n", 0));
				DMibExpect(Unlimited.m_ExitCode, ==, 0u);
				DMibExpect(CEJsonSorted::fs_FromString(Unlimited.f_GetStdOut())["text"].f_String(), ==, LongType);

				auto Combined = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--server", "--file", "Source.cpp"}, "");
				DMibExpect(Combined.m_ExitCode, ==, 2u);
				DMibExpect(Combined.f_GetCombinedOut().f_Find("--server only combines with"), >=, 0);

				co_return {};
			};

			DMibTestCategory("Files") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Files");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source.cpp", gc_Formatted);

				CStr Long = "char const *g_p = \"";
				for (umint iChar = 0; iChar < 200; ++iChar)
					Long += "x";

				Long += "\";\n";

				auto fFile = [](CStr const &_Path, CStr const &_Source) -> CEJsonSorted
					{
						CEJsonSorted Json;
						Json["request"] = "file";
						Json["path"] = _Path;
						Json["source"] = _Source;

						return Json;
					}
				;

				// A source is formatted as its file would be, which is neither read nor written; the path is one of the
				// working directory or a whole one, the file needn't exist, and a range is in the source's bytes.
				auto Ranged = fFile("Source.cpp", gc_Unformatted);
				Ranged["offset"] = CStr(gc_Unformatted).f_Find("if(a");
				Ranged["length"] = 0;
				Ranged["id"] = 7;
				auto NoSource = fFile("Source.cpp", "");
				NoSource.f_RemoveMember("source");
				auto HalfRange = fFile("Source.cpp", gc_Unformatted);
				HalfRange["offset"] = 1;

				TCVector<CEJsonSorted> Requests =
					{
						fFile("Source.cpp", gc_Unformatted)
						, fFile(Repo.m_Path / "New.cpp", gc_Formatted)
						, Ranged
						, fFile("Long.cpp", Long)
						, fFile("Notes.md", "Prose\n")
						, NoSource
						, HalfRange
					}
				;
				CStr Input;
				for (auto const &Request : Requests)
				{
					Input += Request.f_ToString(nullptr).f_Trim();
					Input += "\n";
				}

				// A fragment between files is answered in its place.
				Input += "{\"request\": \"fragment\", \"text\": \"int  a\"}\n";

				auto Result = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--server"}, Input);
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Formatted);
				DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / "New.cpp"));

				TCVector<CEJsonSorted> Lines;
				auto StdOut = Result.f_GetStdOut();
				for (auto const &Line : StdOut.f_SplitLine())
				{
					if (Line)
						Lines.f_Insert(CEJsonSorted::fs_FromString(Line));
				}

				DMibExpect(Lines.f_GetLen(), ==, 8u);
				if (Lines.f_GetLen() == 8)
				{
					DMibExpect(Lines[0]["text"].f_String(), ==, CStr(gc_Formatted));
					DMibExpectFalse(Lines[0].f_GetMember("unresolved"));
					DMibExpectTrue(Lines[1]["unchanged"].f_Boolean());
					DMibExpectFalse(Lines[1].f_GetMember("text"));
					DMibExpect(Lines[2]["text"].f_String(), ==, CStr("void fg_Test()\n{\n\n    int a;   \n\tif (a == 2)\n\t\tfg_Other(a,1);\n}\n"));
					DMibExpect(Lines[2]["id"].f_Integer(), ==, 7);
					// Violations without an automatic fix are told with the source, and reported.
					DMibExpectTrue(Lines[3]["unchanged"].f_Boolean());
					DMibExpectTrue(Lines[3]["unresolved"].f_Boolean());
					DMibExpect(Lines[3]["report"].f_String().f_Find("Long.cpp:1:1: line-length:"), >=, 0);
					// A file that does not opt in is told apart.
					DMibExpectTrue(Lines[4]["excluded"].f_Boolean());
					DMibExpect(Lines[5]["error"].f_String().f_Find("string member 'source'"), >=, 0);
					DMibExpect(Lines[6]["error"].f_String().f_Find("'offset' and 'length' are given together"), >=, 0);
					DMibExpect(Lines[7]["text"].f_String(), ==, CStr("int a"));
				}

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Validation") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing formatting checks in Validate");

			DMibTestCategory("StagedReportsOnlyChangedLines") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "StagedReportsOnlyChangedLines");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "void fg_Test()\n{\n    int a;\n}\n");
				co_await Repo.f_Commit();

				// The committed indentation violation is unchanged, so only the new line is reported.
				Repo.f_Write("Example.cpp", "void fg_Test()\n{\n    int a;\n    int b;\n}\n");
				co_await Repo.f_Stage();

				auto Output = co_await Repo.f_Validate("Staged", 1);
				DMibExpect(Output.f_Find("Example.cpp:4:1: indentation"), >=, 0);
				DMibExpect(Output.f_Find("Example.cpp:3:1: indentation"), ==, -1);
				DMibExpect(Output.f_Find("formatting violation(s)"), >=, 0);

				co_return {};
			};

			// A violation is reported where a changed line is any line of its span, the lines ending in a lone CR as well.
			DMibTestCategory("StagedSpanOverCarriageReturns") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "StagedSpanOverCarriageReturns");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "void fg_Test()\r{\r\tif (a)\r\t{\r\t\tg();\r\t}\r}\r");
				co_await Repo.f_Commit();

				Repo.f_Write("Example.cpp", "void fg_Test()\r{\r\tif (a)\r\t{\r\t\th();\r\t}\r}\r");
				co_await Repo.f_Stage();

				auto Output = co_await Repo.f_Validate("Staged", 1);
				DMibExpect(Output.f_Find("Example.cpp:3:11: braces"), >=, 0);

				co_return {};
			};

			DMibTestCategory("AuditReportsEveryLine") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "AuditReportsEveryLine");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "void fg_Test()\n{\n    int a;\n    int b;\n}\n");
				co_await Repo.f_Commit();

				auto Output = co_await Repo.f_Validate("Audit", 1, false);
				DMibExpect(Output.f_Find("Example.cpp:3:1: indentation"), >=, 0);
				DMibExpect(Output.f_Find("Example.cpp:4:1: indentation"), >=, 0);

				co_return {};
			};

			DMibTestCategory("DeletionOnlyChangeReportsNothing") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "DeletionOnlyChangeReportsNothing");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Example.cpp", "void fg_Test()\n{\n    int a;\n    int b;\n}\n");
				co_await Repo.f_Commit();

				// Removing a line adds none, so the remaining violations stay suppressed.
				Repo.f_Write("Example.cpp", "void fg_Test()\n{\n    int a;\n}\n");
				co_await Repo.f_Stage();

				auto Output = co_await Repo.f_Validate("Staged");
				DMibExpect(Output.f_Find("indentation"), ==, -1);

				co_return {};
			};

			DMibTestCategory("LineLengthOnlyKeepsExistingOutput") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "LineLengthOnlyKeepsExistingOutput");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write("Example.cpp", "        int a = " + fg_Repeat("9", 190) + ";\n");
				co_await Repo.f_Stage();

				// Without the opt-in property the generic validator and its summary are unchanged.
				auto Output = co_await Repo.f_Validate("Staged", 1);
				DMibExpect(Output.f_Find("Example.cpp:1: line length"), >=, 0);
				DMibExpect(Output.f_Find("formatting violation(s)"), ==, -1);
				DMibExpect(Output.f_Find("indentation"), ==, -1);

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("BuildSystem") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format of build system files");

			DMibTestCategory("ProfileSelectsTheLanguage") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "ProfileSelectsTheLanguage");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write
					(
						".editorconfig"
						, "root = true\n\n[*]\nindent_style = tab\ntab_width = 4\nmax_line_length = 190\n\n"
						"[*.{MHeader,Imported}]\nmalterlib_format = malterlib-buildsystem\n"
					)
				;
				Repo.f_Write("Build.MHeader", "Property\n{\n    A true\n\n\n\tB [\n\"x\"\n\t]\n}\n");
				Repo.f_Write("Build.MRepo", "{\n    \"A\": 1\n}\n");
				Repo.f_Write("Notes.txt", "  Indented with spaces\n");
				Repo.f_Write("Build.Imported", "A\n{\n    B 1\n}\n");

				// The build system profile formats its files whatever they are named, and what it does not opt in keeps its bytes.
				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "*", "--recursive"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("2 changed"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Build.Imported", true), ==, "A\n{\n\tB 1\n}\n");
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Build.MHeader", true), ==, "Property\n{\n\tA true\n\n\tB [\n\t\t\"x\"\n\t]\n}\n");
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Build.MRepo", true), ==, "{\n    \"A\": 1\n}\n");
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Notes.txt", true), ==, "  Indented with spaces\n");

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Naming") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format naming");

			// Only a naming that lists 'inline_small' as a specifier moves 'static' in front of it.
			CStr const Source = "constexpr inline_small static int gc_Value = 1;\n";
			CStr const Reordered = "static constexpr inline_small int gc_Value = 1;\n";
			CStr const Style = "Version: 1\nSpecifierMacros: ['inline_small']\n";

			DMibTestCategory("NoNamingReadsOnlyCpp") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "NoNamingReadsOnlyCpp");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source.cpp", Source);

				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Source.cpp"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, Source);

				co_return {};
			};

			DMibTestCategory("NearestDocument") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "NearestDocument");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write(".malterlib-format", Style);
				Repo.f_Write("Module/.malterlib-format", "Version: 1\n");
				Repo.f_Write("Source/Deep/Source.cpp", Source);
				Repo.f_Write("Module/Source.cpp", Source);

				auto Named = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Source/Deep/Source.cpp"});
				DMibExpect(Named.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source/Deep/Source.cpp", true), ==, Reordered);

				// The nearest document wins, even one that names nothing.
				auto Empty = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Module/Source.cpp"});
				DMibExpect(Empty.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Module/Source.cpp", true), ==, Source);

				co_return {};
			};

			DMibTestCategory("ExtendsAndFallback") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "ExtendsAndFallback");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Malterlib/Core/.malterlib-format", Style);
				Repo.f_Write("Malterlib/Module/.malterlib-format", "Version: 1\nExtends: ../Core/.malterlib-format\nSpecifierMacros: {Append: ['inline_medium']}\n");
				Repo.f_Write("Malterlib/Module/Source/Source.cpp", Source + "constexpr inline_medium static int gc_Other = 2;\n");
				Repo.f_Write("Other/Source.cpp", Source);

				auto Extended = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Malterlib/Module/Source/Source.cpp"});
				DMibExpect(Extended.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Malterlib/Module/Source/Source.cpp", true), ==, Reordered + "static constexpr inline_medium int gc_Other = 2;\n");

				// Without a document of its own, a file reads Malterlib/Core's.
				auto Fallback = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Other/Source.cpp"});
				DMibExpect(Fallback.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Other/Source.cpp", true), ==, Reordered);

				co_return {};
			};

			// A staged run validates the root's index, which holds no file of a repository checked out below the root.
			DMibTestCategory("StagedReadsNestedRepository") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "StagedReadsNestedRepository");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write(".gitignore", "/Malterlib/Core\n");
				co_await Repo.f_Git({"init", "-q", "Malterlib/Core"});
				Repo.f_Write("Malterlib/Core/.malterlib-format", Style);
				Repo.f_Write("Source.cpp", Source);
				co_await Repo.f_Stage();

				// Without a document of its own the root reads Malterlib/Core's, as a run of the working tree does.
				auto Fallback = co_await Repo.f_Validate("Fallback", 1);
				DMibExpect(Fallback.f_Find("Source.cpp:1:"), >=, 0);

				// A document of the root that extends Malterlib/Core's reads it from there.
				Repo.f_Write(".malterlib-format", "Version: 1\nExtends: Malterlib/Core/.malterlib-format\n");
				co_await Repo.f_Stage();

				auto Extended = co_await Repo.f_Validate("Extended", 1);
				DMibExpect(Extended.f_Find("is not in the validated snapshot"), ==, -1);
				DMibExpect(Extended.f_Find("Source.cpp:1:"), >=, 0);

				// A style the root tracks stays the staged one when a repository is checked out around it afterwards.
				Repo.f_Write("Vendor/Style.yaml", Style);
				Repo.f_Write(".malterlib-format", "Version: 1\nExtends: Vendor/Style.yaml\n");
				co_await Repo.f_Stage();
				co_await Repo.f_Git({"init", "-q", "Vendor"});
				Repo.f_Write("Vendor/Style.yaml", "Version: 1\n");

				auto Tracked = co_await Repo.f_Validate("Tracked", 1);
				DMibExpect(Tracked.f_Find("Source.cpp:1:"), >=, 0);

#ifndef DPlatformFamily_Windows
				// A symbolic link the root tracks there is no style of the snapshot, and isn't followed on disk either.
				Repo.f_Write("Vendor/Original.yaml", Style);
				CFile::fs_CreateSymbolicLink("Original.yaml", Repo.m_Path / "Vendor/Link.yaml", EFileAttrib_File, ESymbolicLinkFlag_Relative);
				Repo.f_Write(".malterlib-format", "Version: 1\nExtends: Vendor/Link.yaml\n");
				co_await Repo.f_Git({"-c", "core.hooksPath=/dev/null", "--git-dir", ".git", "add", "-f", "Vendor/Link.yaml", ".malterlib-format"});

				auto Linked = co_await Repo.f_Tool({"Validate", "-C", Repo.m_Path, "--staged", "--no-color"});
				DMibExpect(Linked.m_ExitCode, !=, 0u);
				DMibExpect(Linked.f_GetCombinedOut().f_Find("is not a regular file in the validated snapshot"), >=, 0);
#endif

				// Whatever its name, a file the root tracks there is read from the snapshot.
				Repo.f_Write("Vendor/.gitattributes", Style);
				Repo.f_Write(".malterlib-format", "Version: 1\nExtends: Vendor/.gitattributes\n");
				co_await Repo.f_Git({"--git-dir", ".git", "add", "-f", "Vendor/.gitattributes", ".malterlib-format"});
				Repo.f_Write("Vendor/.gitattributes", "Version: 1\n");

				auto Named = co_await Repo.f_Validate("Named", 1);
				DMibExpect(Named.f_Find("Source.cpp:1:"), >=, 0);

				// The snapshot's document is read however a path in the root is spelled: through a doubled separator, and outside
				// Windows climbing above the file system's root, which stays at the root, with a leading // and with a name that
				// only Windows takes for a drive.
				struct CSpelling
				{
					CStr m_Name;
					CStr m_File;
					CStr m_Extends;
				};

				TCVector<CSpelling> Spellings = {{"DoubledSeparator", "Deep.yaml", "Styles//../Deep.yaml"}};
#ifndef DPlatformFamily_Windows
				// The absolute spellings start from the root as Git spells it, which validation takes the snapshot's root from: the
				// fixture's path can reach it through a symbolic link, as /tmp is to /private/tmp on macOS.
				CStr GitRoot = (co_await Repo.f_Git({"rev-parse", "--show-toplevel"})).f_RemoveSuffix("\n");
				CStr Climb;
				for (umint iComponent = 0; iComponent < GitRoot.f_Split<true>("/").f_GetLen() + 2; ++iComponent)
					Climb += "../";
				Spellings.f_Insert({"Climbing", "Deep.yaml", Climb + GitRoot.f_Extract(1) + "/Deep.yaml"});
				Spellings.f_Insert({"LeadingSlashes", "Deep.yaml", "/" + GitRoot + "/Deep.yaml"});
				Spellings.f_Insert({"DriveLike", "v:Deep.yaml", "v:Deep.yaml"});
#endif
				Repo.f_Write("Styles/Placeholder", "");
				for (auto const &Spelling : Spellings)
				{
					DMibTestPath(Spelling.m_Name);
					Repo.f_Write(Spelling.m_File, Style);
					Repo.f_Write(".malterlib-format", "Version: 1\nExtends: {}\n"_f << Spelling.m_Extends);
					co_await Repo.f_Git({"--git-dir", ".git", "add", "-f", Spelling.m_File, ".malterlib-format"});
					Repo.f_Write(Spelling.m_File, "Version: 1\nFunctionName: ['f_.*']\n");

					auto Spelled = co_await Repo.f_Validate(Spelling.m_Name, 1);
					DMibExpect(Spelled.f_Find("Unknown key"), ==, -1);
					DMibExpect(Spelled.f_Find("Source.cpp:1:"), >=, 0);
				}

				// A path below a file the root tracks is none of the snapshot, whatever is checked out in the file's place.
				Repo.f_Write("Owned", "A file\n");
				Repo.f_Write(".malterlib-format", "Version: 1\nExtends: Owned/Style.yaml\n");
				co_await Repo.f_Git({"--git-dir", ".git", "add", "-f", "Owned", ".malterlib-format"});
				CFile::fs_DeleteFile(Repo.m_Path / "Owned");
				co_await Repo.f_Git({"init", "-q", "Owned"});
				Repo.f_Write("Owned/Style.yaml", Style);

				auto Below = co_await Repo.f_Tool({"Validate", "-C", Repo.m_Path, "--staged", "--no-color"});
				DMibExpect(Below.m_ExitCode, !=, 0u);
				DMibExpect(Below.f_GetCombinedOut().f_Find("is not in the validated snapshot"), >=, 0);

				co_return {};
			};

			DMibTestCategory("InvalidDocumentsFail") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "InvalidDocumentsFail");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Unknown/.malterlib-format", "Version: 1\nFunctionName: ['f_.*']\n");
				Repo.f_Write("Unknown/Source.cpp", Source);
				Repo.f_Write("Missing/.malterlib-format", "Version: 1\nExtends: Absent/.malterlib-format\n");
				Repo.f_Write("Missing/Source.cpp", Source);

				auto Unknown = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Unknown/Source.cpp"});
				DMibExpect(Unknown.m_ExitCode, !=, 0u);
				DMibExpect(Unknown.f_GetCombinedOut().f_Find("Unknown key 'FunctionName'"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Unknown/Source.cpp", true), ==, Source);

				auto Missing = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Missing/Source.cpp"});
				DMibExpect(Missing.m_ExitCode, !=, 0u);
				DMibExpect(Missing.f_GetCombinedOut().f_Find("Absent/.malterlib-format"), >=, 0);

				co_return {};
			};

			DMibTestCategory("ExtendsCycleFails") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "ExtendsCycleFails");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("A/.malterlib-format", "Version: 1\nExtends: ../B/.malterlib-format\n");
				Repo.f_Write("B/.malterlib-format", "Version: 1\nExtends: ../A/.malterlib-format\n");
				Repo.f_Write("A/Source.cpp", Source);
				Repo.f_Write("B/Source.cpp", Source);

				// Both files resolve at once, from the two ends of the cycle.
				auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "*.cpp", "--recursive"});
				DMibExpect(Result.m_ExitCode, !=, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("extends itself"), >=, 0);

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Stdin") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format --stdin");

			DMibTestCategory("FormatsTheBuffer") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "FormatsTheBuffer");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source.cpp", gc_Formatted);

				// The buffer is formatted as the file would be, which is neither read nor written.
				auto Changed = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--stdin", "--file", "Source.cpp"}, gc_Unformatted);
				DMibExpect(Changed.m_ExitCode, ==, 0u);
				DMibExpect(Changed.f_GetStdOut(), ==, gc_Formatted);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Formatted);

				// A file that does not exist yet formats as one would, and an unchanged buffer comes back as it is.
				auto Unchanged = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--stdin", "--file", "New.cpp"}, gc_Formatted);
				DMibExpect(Unchanged.m_ExitCode, ==, 0u);
				DMibExpect(Unchanged.f_GetStdOut(), ==, gc_Formatted);
				DMibExpectFalse(CFile::fs_FileExists(Repo.m_Path / "New.cpp"));

				// The range refers to the buffer's lines.
				auto Ranged = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--stdin", "--file", "Source.cpp", "--lines", "5:5"}, gc_Unformatted);
				DMibExpect(Ranged.m_ExitCode, ==, 0u);
				DMibExpect(Ranged.f_GetStdOut(), ==, "void fg_Test()\n{\n\n    int a;   \n\tif (a == 2)\n\t\tfg_Other(a,1);\n}\n");

				co_return {};
			};

			DMibTestCategory("UnresolvedAndExcluded") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "UnresolvedAndExcluded");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);

				CStr Long = "char const *g_p = \"";
				for (umint iChar = 0; iChar < 200; ++iChar)
					Long += "x";

				Long += "\";\n";
				auto Unresolved = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--stdin", "--file", "Long.cpp"}, Long);
				DMibExpect(Unresolved.m_ExitCode, ==, 1u);
				DMibExpect(Unresolved.f_GetStdOut(), ==, Long);
				DMibExpect(Unresolved.f_GetStdErr().f_Find("Long.cpp:1:1: line-length:"), >=, 0);

				// A file that does not opt in is told apart, and gives nothing back.
				auto Excluded = co_await Repo.f_ToolWithStdIn({"Format", "--no-color", "-C", Repo.m_Path, "--stdin", "--file", "Notes.md"}, "Prose\n");
				DMibExpect(Excluded.m_ExitCode, ==, 3u);
				DMibExpect(Excluded.f_GetStdOut(), ==, "");

				co_return {};
			};

			DMibTestCategory("RejectedCombinations") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "RejectedCombinations");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				TCVector<CRejectedCase> Cases =
					{
						{"Check", {"--stdin", "--file", "A.cpp", "--check"}}
						, {"Diff", {"--stdin", "--file", "A.cpp", "--diff"}}
						, {"Pattern", {"--stdin", "--pattern", "*.cpp"}}
						, {"TwoFiles", {"--stdin", "--file", "A.cpp,B.cpp"}}
						, {"MissingDirectory", {"--stdin", "--file", "Missing/A.cpp"}}
					}
				;
				for (auto const &Case : Cases)
				{
					DMibTestPath(Case.m_Name);
					TCVector<CStr> Params = {"Format", "--no-color", "-C", Repo.m_Path};
					Params.f_Insert(Case.m_Params);
					auto Result = co_await Repo.f_ToolWithStdIn(fg_Move(Params), "int a;\n");
					DMibExpect(Result.m_ExitCode, ==, 2u);
					DMibExpect(Result.f_GetStdOut(), ==, "");
				}

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("Workspace") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing mib format over a workspace");

			DMibTestCategory("FormatsRepositoriesThatOptIn") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "FormatsRepositoriesThatOptIn");

				CRepositoryFixture Repo;
				co_await Repo.f_Init();
				Repo.f_Write(".editorconfig", g_FormatConfiguration);
				Repo.f_Write("Source.cpp", gc_Unformatted);
				auto fConfigure = [&](bool _bFormat)
					{
						CStr Configuration = "%Repository \".\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n"
							"\t\tURL \"https://example.invalid/format-fixture.git\"\n"
						;
						Configuration += "\t\tLocation " + CEJsonSorted(Repo.m_Path).f_ToString() + "\n";
						Configuration += _bFormat ? "\t\tFormat true\n\t}\n}\n" : "\t}\n}\n";
						Repo.f_Write("Test.MBuildSystem", Configuration);
					}
				;
				auto fRun = [&](TCVector<CStr> _Extra)
					{
						TCVector<CStr> Params =
							{
								"format"
								, "--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						;
						Params.f_Insert(_Extra);

						return Repo.f_Tool(fg_Move(Params), true);
					}
				;

				fConfigure(true);
				auto Checked = co_await fRun({"--check"});
				DMibExpect(Checked.m_ExitCode, ==, 1u);
				DMibExpect(Checked.f_GetCombinedOut().f_Find("Formatted 3 file(s): 0 unchanged, 1 would change"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Unformatted);

				auto Written = co_await fRun({});
				DMibExpect(Written.m_ExitCode, ==, 0u);
				DMibExpect(Written.f_GetCombinedOut().f_Find("1 changed"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Formatted);

				// A directory the walk cannot list is an operational failure, told apart from a violation.
#ifndef DPlatformFamily_Windows
				Repo.f_Write("Locked/Example.cpp", gc_Unformatted);
				auto LockedAttributes = CFile::fs_GetAttributes(Repo.m_Path / "Locked");
				CFile::fs_SetAttributes(Repo.m_Path / "Locked", EFileAttrib_UnixAttributesValid | EFileAttrib_UserWrite);
				auto Locked = co_await fRun({"--check"});
				CFile::fs_SetAttributes(Repo.m_Path / "Locked", LockedAttributes | EFileAttrib_UnixAttributesValid);
				{
					DMibTestPath("Locked");
					DMibExpect(Locked.m_ExitCode, ==, 2u);
					DMibExpect(Locked.f_GetCombinedOut().f_Find("cannot be listed"), >=, 0);
				}
				CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "Locked");
#endif

				// A repository that does not opt in is not visited, and none is no error.
				Repo.f_Write("Source.cpp", gc_Unformatted);
				fConfigure(false);
				auto Skipped = co_await fRun({"--check"});
				DMibExpect(Skipped.m_ExitCode, ==, 0u);
				DMibExpect(Skipped.f_GetCombinedOut().f_Find("Formatted"), <, 0);

				co_return {};
			};

			co_return {};
		};

		DMibTestSuite("PerforceRoot") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing mib format in a Perforce root");

			CRepositoryFixture Repo;
			co_await Repo.f_Init();
			Repo.f_MakePerforceRoot("P4CLIENT=Offline\n");
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write(".p4ignore", "/Ignored/\n*.generated.cpp\n.gitignore\n.gitattributes\n");
#ifndef DPlatformFamily_Windows
			// Git's rule files mean nothing in a Perforce root and aren't read: unreadable ones don't fail the format.
			for (auto pName : {".gitignore", ".gitattributes"})
			{
				Repo.f_Write(pName, "*.cpp\n");
				CFile::fs_SetAttributes(Repo.m_Path / pName, EFileAttrib_UnixAttributesValid | EFileAttrib_UserWrite);
			}
#endif
			Repo.f_Write("Source.cpp", gc_Unformatted);
			Repo.f_Write("Formatted.cpp", gc_Formatted);
			Repo.f_Write("Source.generated.cpp", gc_Unformatted);
			Repo.f_Write("Ignored/Ignored.cpp", gc_Unformatted);
			Repo.f_Write("Lib/Lib.cpp", gc_Unformatted);
			Repo.f_Write("Unknown/Unknown.cpp", gc_Unformatted);
			for (auto pRepository : {"Lib", "Unknown"})
			{
				DMibTestPath(pRepository);
				auto Init = co_await Repo.f_Run("git", {"init", "-q", "-b", "main", Repo.m_Path / pRepository}, Repo.m_Path);
				DMibAssert(Init.m_ExitCode, ==, 0u);
			}

			auto fConfigure = [&](CStr const &_PerforceRoot)
				{
					CStr Configuration;
					{
						CStr::CAppender Appender(Configuration);
						Appender += "Property\n{\n\tPerforceRoot ";
						Appender += _PerforceRoot;
						Appender += "\n}\n%Repository \"Lib\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n";
						Appender += "\t\tURL \"https://example.invalid/format-fixture.git\"\n\t\tLocation ";
						Appender += CEJsonSorted(Repo.m_Path / "Lib").f_ToString();
						Appender += "\n\t}\n}\n";
					}
					Repo.f_Write("Test.MBuildSystem", Configuration);
				}
			;

			auto fFormat = [&](TCVector<CStr> const &_Extra)
				{
					TCVector<CStr> Params =
						{
							"format"
							, "--skip-update"
							, "--no-color"
							, "--build-system"
							, Repo.m_Path / "Test.MBuildSystem"
							, "--output-directory"
							, Repo.m_Root / "output"
							, "--no-use-user-settings"
							, "--no-use-cached-environment"
						}
					;
					Params.f_Insert(_Extra);

					return Repo.f_Tool(fg_Move(Params), true);
				}
			;

			fConfigure("{Format: true}");

			{
				DMibTestPath("Check");
				auto Result = co_await fFormat({"--check"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find(" 1 unchanged, 1 would change"), >=, 0);
				DMibExpect(Output.f_Find("Source.cpp"), >=, 0);
				DMibExpect(Output.f_Find("generated"), <, 0);
				DMibExpect(Output.f_Find("Ignored"), <, 0);
				DMibExpect(Output.f_Find("Lib"), <, 0);
				DMibExpect(Output.f_Find("Unknown"), <, 0);
			}

			{
				// The Perforce root is '.' to the name filter, also among several wildcards.
				DMibTestPath("NameFilterList");
				auto Result = co_await fFormat({"--check", "-n", "[\"Lib\", \".\"]"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find(" 1 unchanged, 1 would change"), >=, 0);
				DMibExpect(Output.f_Find("Lib"), <, 0);
			}

			{
				DMibTestPath("Diff");
				auto Result = co_await fFormat({"--diff"});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("+++ b/Source.cpp"), >=, 0);
				DMibExpect(Output.f_Find("Ignored"), <, 0);
				DMibExpect(Output.f_Find("Unknown"), <, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Unformatted);
			}

#ifndef DPlatformFamily_Windows
			// A FIFO, which the listing gives as a file, isn't read: reading it would wait for a writer.
			DMibAssert(mkfifo((Repo.m_Path / "Pipe.cpp").f_GetStr(), 0600), ==, 0);
#endif

			{
				// mib validate audits the root as mib format checks it, without what its P4IGNORE files and its repositories hold.
				DMibTestPath("Validate");
				auto Result = co_await Repo.f_Tool
					(
						{
							"validate"
							, "--skip-update"
							, "--no-color"
							, "--build-system"
							, Repo.m_Path / "Test.MBuildSystem"
							, "--output-directory"
							, Repo.m_Root / "output"
							, "--no-use-user-settings"
							, "--no-use-cached-environment"
						}
						, true
					)
				;
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("No repository sets Repository.Format"), <, 0);
				DMibExpect(Output.f_Find("Source.cpp:"), >=, 0);
				DMibExpect(Output.f_Find("in 2 formatted file(s)"), >=, 0);
				DMibExpect(Output.f_Find("generated"), <, 0);
				DMibExpect(Output.f_Find("Ignored"), <, 0);
				DMibExpect(Output.f_Find("Lib"), <, 0);
				DMibExpect(Output.f_Find("Unknown"), <, 0);
			}

			{
				DMibTestPath("Format");
				auto Result = co_await fFormat({});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Formatted);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Ignored/Ignored.cpp", true), ==, gc_Unformatted);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Unknown/Unknown.cpp", true), ==, gc_Unformatted);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Lib/Lib.cpp", true), ==, gc_Unformatted);
			}

			{
				// A repository of the build system is not the root's before it is cloned either.
				DMibTestPath("RepositoryWithoutGit");
				CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / "Lib/.git");
				auto Result = co_await fFormat({});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Lib/Lib.cpp", true), ==, gc_Unformatted);
			}

			{
				DMibTestPath("NotOptedIn");
				fConfigure("{Format: false}");
				auto Result = co_await fFormat({"--check"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("No repository sets Repository.Format"), >=, 0);
			}

			{
				// A Perforce base without a Perforce root to validate is an error, also where no repository is validated at all.
				DMibTestPath("PerforceBaseWithoutRoot");
				auto Result = co_await Repo.f_Tool
					(
						{
							"validate"
							, "--perforce-base"
							, "@1"
							, "--skip-update"
							, "--no-color"
							, "--build-system"
							, Repo.m_Path / "Test.MBuildSystem"
							, "--output-directory"
							, Repo.m_Root / "output"
							, "--no-use-user-settings"
							, "--no-use-cached-environment"
						}
						, true
					)
				;
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("--perforce-base gives the base of a Perforce root, and none is validated"), >=, 0);
			}

			co_return {};
		};

		// A Perforce root's workspace on a server: mib validate's changed lines are those of the files opened in it, or submitted
		// after a changelist, and mib format opens a read-only file it changes for edit.
		DMibTestSuite("PerforceRootWorkspace") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing mib format and validate in the workspace of a Perforce root");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's workspace: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			TCVector<CPerforceFile> FirstFiles{{"Opened.cpp", gc_Formatted}, {"Changed.cpp", gc_Formatted}, {"Unopened.cpp", gc_Formatted}};
			TCVector<CPerforceFile> SecondFiles{{"Changed.cpp", gc_Formatted + "\nint  g_Changed;\n"}};
			TCVector<CStr> OpenedFiles{"Opened.cpp"};

			// A path holding what p4 reads as a file specification's syntax.
			FirstFiles.f_Insert({"With@At/Special%Name.cpp", gc_Formatted});

			// Opened with a UTF-8 signature, as the revision has it, and with CRLF, which p4 diff reads as a change of each line where
			// the client's lines end in LF, as they do outside Windows.
			FirstFiles.f_Insert({"Signed.cpp", "\xEF\xBB\xBFint  g_Old;\n"});
			FirstFiles.f_Insert({"Crlf.cpp", "int  g_Old;\n"});
			OpenedFiles.f_Insert({"Signed.cpp", "Crlf.cpp"});

			// A file the configuration selects no validator for, which is never read.
			FirstFiles.f_Insert({"Excluded/Large.dat", "old\n"});
			SecondFiles.f_Insert({"Excluded/Large.dat", "new\n"});
			OpenedFiles.f_Insert("Excluded/Large.dat");

			// Files that a name holding '...' matches as a wildcard, and one to open for delete.
			FirstFiles.f_Insert({"Sample-One.cpp", gc_Formatted});
			FirstFiles.f_Insert({"Sample-Two.cpp", gc_Formatted});
			FirstFiles.f_Insert({"Deleted.cpp", gc_Formatted});

			// A blank line of context, which the diff's hunk ranges count.
			FirstFiles.f_Insert({"Blank.cpp", "int g_A;\n\nint g_B;\n"});
			OpenedFiles.f_Insert("Blank.cpp");

			int64 FirstChangelist = co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), FirstFiles, CStr("Add the sources"));
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), SecondFiles, CStr("Change the sources"));

			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Workspace = Repo.m_Path, OpenedFiles]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						for (auto const &File : OpenedFiles)
							Client.f_OpenForEdit(NPerforce::CPerforceClient::fs_EscapeFileName(Workspace / File));
					}
					% "Failed to set up the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration + "\n[Excluded/**]\nmax_line_length = off\n");
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			Repo.f_WriteWorkspaceText("Opened.cpp", gc_Formatted + "\nint  g_Opened;\n");
			Repo.f_WriteWorkspaceText("Signed.cpp", "\xEF\xBB\xBFint  g_Old;\nint  g_New;\n");
			Repo.f_Write("Crlf.cpp", "int  g_Old;\r\nint  g_New;\r\n");
			Repo.f_WriteWorkspaceText("Excluded/Large.dat", "changed\n");
			Repo.f_WriteWorkspaceText("Blank.cpp", "int g_A;\n\nint g_B;\nint  g_C;\n");

			// Changed in the workspace without being opened, and read-only as a file the workspace has not opened is.
			Repo.f_Write("Unopened.cpp", gc_Unformatted);
			Repo.f_Write("With@At/Special%Name.cpp", gc_Unformatted);
			auto fMakeReadOnly = [&](CStr const &_Name)
				{
					auto Path = Repo.m_Path / _Name;
#ifdef DPlatformFamily_Windows
					CFile::fs_SetAttributes(Path, CFile::fs_GetAttributes(Path) | EFileAttrib_ReadOnly);
#else
					CFile::fs_SetAttributes(Path, EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_GroupRead | EFileAttrib_EveryoneRead);
#endif
				}
			;
			for (auto pName : {"Changed.cpp", "Unopened.cpp", "With@At/Special%Name.cpp"})
				fMakeReadOnly(pName);

			// A command trigger refusing the p4 commands whose arguments hold a text, which shows what a run asks the server. The
			// shell runs the script, which Windows can't launch by itself.
			auto fRefuse = [&](CStr _Command, CStr _Text) -> TCUnsafeFuture<void>
				{
					CStr Script = Repo.m_Root / ("Refuse-{}.sh"_f << _Command);
					CFile::fs_WriteStringToFile(Script, "#!/bin/sh\ncase \"$*\" in\n*{}*) echo \"Refused {}\"; exit 1;;\nesac\nexit 0\n"_f << _Text << _Command, false);
					CFile::fs_SetAttributes(Script, EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute);

					CStr Triggers = "Triggers:\n";
					if (_Text)
						Triggers += "\tRefuse command pre-user-{} \"%quote%{}%quote% %quote%{}%quote% %args%\"\n"_f << _Command << Repo.m_Shell << Script;

					co_await
						(
							g_Dispatch(fg_BlockingActor())
							/ [Connection, Triggers]()
							{
								NPerforce::CPerforceClientThrow Client(Connection);
								Client.f_Login(CStr());
								Client.f_SetTriggers(Triggers);
							}
							% "Failed to set the triggers"
						)
					;

					co_return {};
				}
			;

			auto fRun = [&](TCVector<CStr> _Params)
				{
					_Params.f_Insert
						(
							{
								"--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						)
					;

					return Repo.f_Tool(fg_Move(_Params), true);
				}
			;

			auto fOpened = [&]() -> TCUnsafeFuture<TCVector<CStr>>
				{
					co_return co_await
						(
							g_Dispatch(fg_BlockingActor()) / [Connection, Workspace = Repo.m_Path]() -> TCVector<CStr>
							{
								NPerforce::CPerforceClientThrow Client(Connection);
								Client.f_Login(CStr());
								TCVector<CStr> Opened;
								Client.f_NoThrow().f_GetOpened(Workspace / "...", CStr(), Opened);
								Opened.f_Sort();

								return Opened;
							}
						)
					;
				}
			;

			{
				// Only the lines the opened file changes are reported: not the file changed without being opened, nor the lines of
				// the changelist submitted before.
				DMibTestPath("ValidateStaged");
				auto Result = co_await fRun({"validate", "--staged"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Opened.cpp:8:"), >=, 0);
				DMibExpect(Output.f_Find("Changed.cpp"), <, 0);
				DMibExpect(Output.f_Find("Unopened.cpp"), <, 0);
				DMibExpect(Output.f_Find("Signed.cpp:2:"), >=, 0);
				DMibExpect(Output.f_Find("Signed.cpp:1:"), <, 0);
				DMibExpect(Output.f_Find("Crlf.cpp:2:"), >=, 0);
#ifdef DPlatformFamily_Windows
				DMibExpect(Output.f_Find("Crlf.cpp:1:"), <, 0);
#else
				DMibExpect(Output.f_Find("Crlf.cpp:1:"), >=, 0);
#endif
			}

			{
				// The lines submitted after the changelist, of the revisions the workspace has.
				DMibTestPath("ValidateBase");
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << FirstChangelist)});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Changed.cpp:8:"), >=, 0);
				DMibExpect(Output.f_Find("Opened.cpp"), <, 0);
				DMibExpect(Output.f_Find("Unopened.cpp"), <, 0);
			}

			{
				// A file no validator applies to is not compared: comparing it fails here.
				DMibTestPath("ValidateExcludedNotRead");
				co_await fRefuse("diff", "Excluded");
				auto Staged = co_await fRun({"validate", "--staged"});
				co_await fRefuse("diff2", "Excluded");
				auto Committed = co_await fRun({"validate", "--base", CStr("@{}"_f << FirstChangelist)});
				co_await fRefuse("diff2", "");
				DMibExpect(Staged.m_ExitCode, ==, 1u);
				DMibExpect(Staged.f_GetCombinedOut().f_Find("Refused"), <, 0);
				DMibExpect(Committed.m_ExitCode, ==, 1u);
				DMibExpect(Committed.f_GetCombinedOut().f_Find("Refused"), <, 0);
			}

			{
				// A revision at the changelist that the server fails to look up is an error, not a file new since.
				DMibTestPath("ValidateBaseLookupFails");
				co_await fRefuse("diff2", "@");
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << FirstChangelist)});
				co_await fRefuse("diff2", "");
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Refused diff2"), >=, 0);
			}

			{
				DMibTestPath("ValidateBaseNotAChangelist");
				auto Result = co_await fRun({"validate", "--base", "main"});
				DMibExpect(Result.m_ExitCode, ==, 2u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("whose base is a changelist"), >=, 0);
			}

			{
				// Beyond 64 bits, of more digits than the largest or as many, is no changelist either.
				DMibTestPath("ValidateBaseBeyond64Bits");
				for (CStr Base : {"@99999999999999999999", "@9223372036854775808"})
				{
					DMibTestPath(Base.f_GetLen() > 20 ? "MoreDigits" : "AsManyDigits");
					auto Result = co_await fRun({"validate", "--base", Base});
					DMibExpect(Result.m_ExitCode, ==, 2u);
					DMibExpect(Result.f_GetCombinedOut().f_Find(CStr("which '{}' is not"_f << Base)), >=, 0);
				}
			}

			{
				DMibTestPath("Check");
				auto Result = co_await fRun({"format", "--check"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				auto Opened = co_await fOpened();
				for (auto pDepotFile : {"//Streams/Main/Changed.cpp", "//Streams/Main/Unopened.cpp", "//Streams/Main/With%40At/Special%25Name.cpp"})
				{
					DMibTestPath(pDepotFile);
					DMibExpect(Opened.f_Contains(pDepotFile), <, 0);
				}
			}

			{
				// A read-only file is opened for edit before it is replaced; one already opened is writable and needs nothing.
				DMibTestPath("Format");
				auto Result = co_await fRun({"format"});
				DMibExpect(Result.m_ExitCode, ==, 0u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Changed.cpp: opened for edit in Perforce"), >=, 0);
				DMibExpect(Output.f_Find("Unopened.cpp: opened for edit in Perforce"), >=, 0);
				DMibExpect(Output.f_Find("Special%Name.cpp: opened for edit in Perforce"), >=, 0);
				DMibExpect(Output.f_Find("/Opened.cpp: opened for edit in Perforce"), <, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Unopened.cpp", true), ==, gc_Formatted);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "With@At/Special%Name.cpp", true), ==, gc_Formatted);
				auto Opened = co_await fOpened();
				for (auto pDepotFile : {"//Streams/Main/Changed.cpp", "//Streams/Main/Opened.cpp", "//Streams/Main/Unopened.cpp", "//Streams/Main/With%40At/Special%25Name.cpp"})
				{
					DMibTestPath(pDepotFile);
					DMibExpect(Opened.f_Contains(pDepotFile), >=, 0);
				}
			}

			{
				// A read-only file that can't be opened for edit isn't written: a name holding '...', which p4 can't escape and would
				// read as a wildcard of other files, and a file opened for delete, which p4 edit only tells it leaves as it is.
				DMibTestPath("FormatNotOpenable");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Delete(Workspace / "Deleted.cpp");
						}
						% "Failed to open the file for delete"
					)
				;
				Repo.f_Write("Deleted.cpp", gc_Unformatted);
				Repo.f_Write("Sample...cpp", gc_Unformatted);
				fMakeReadOnly("Deleted.cpp");
				fMakeReadOnly("Sample...cpp");

				auto Result = co_await fRun({"format"});
				DMibExpect(Result.m_ExitCode, !=, 0u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Sample...cpp: is read-only and could not be opened for edit in Perforce"), >=, 0);
				DMibExpect(Output.f_Find("Deleted.cpp: is read-only and could not be opened for edit in Perforce"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Sample...cpp", true), ==, gc_Unformatted);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Deleted.cpp", true), ==, gc_Unformatted);
				auto Opened = co_await fOpened();
				DMibExpect(Opened.f_Contains("//Streams/Main/Sample-One.cpp"), <, 0);
				DMibExpect(Opened.f_Contains("//Streams/Main/Sample-Two.cpp"), <, 0);
			}

			co_return {};
		};

		// The git checkout around a Perforce root neither bounds its walk nor ignores it.
		DMibTestSuite("PerforceRootInGitCheckout") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing mib format in a Perforce root inside a git checkout");

			CRepositoryFixture Repo;
			co_await Repo.f_Init();
			Repo.f_SetPerforceEnvironment();
			Repo.f_Write(".gitignore", "/Project/\n");
			Repo.f_Write("Project/.p4config", "P4CLIENT=Offline\n");
			Repo.f_Write("Project/.editorconfig", g_FormatConfiguration);
			Repo.f_Write("Project/.p4ignore", "/Ignored/\n");
			Repo.f_Write("Project/Source.cpp", gc_Unformatted);
			Repo.f_Write("Project/Ignored/Ignored.cpp", gc_Unformatted);
			Repo.f_Write("Project/Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"format"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Project/Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
						, "--check"
					}
					, true
				)
			;
			DMibExpect(Result.m_ExitCode, ==, 1u);
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Output.f_Find("Source.cpp"), >=, 0);
			DMibExpect(Output.f_Find("Ignored"), <, 0);
			DMibExpect(Output.f_Find("0 unchanged, 1 would change"), >=, 0);

			co_return {};
		};

		// The revisions a Perforce root's validation compares: an opened file's with the revision it was opened from, each read
		// as its own type says and the workspace's in the text its encoding spells.
		DMibTestSuite("PerforceRootRevisions") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing the revisions mib validate compares in a Perforce root");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of the revisions of a Perforce root: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			CStr LongLine = fg_Repeat("x", 200);

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			TCVector<CPerforceFile> FirstFiles;

			// Changed in the workspace as UTF-16.
			FirstFiles.f_Insert({"Wide.txt", "a\n"});

			// Retyped to binary after the revision the workspace has.
			FirstFiles.f_Insert({"Retyped.cpp", "int  g_Retyped;\n"});

			FirstFiles.f_Insert({"Blank.cpp", "int g_A;\n\nint g_B;\n"});

			// Opened from its first revision, while the workspace is synced to its second.
			FirstFiles.f_Insert({"Synced.cpp", "int  g_Old;\n"});

			// Opened for delete, with a copy left in the workspace.
			FirstFiles.f_Insert({"Deleted.cpp", "int g_Deleted;\n"});

			// A formatted file and one whose line lengths alone are checked, in that order.
			FirstFiles.f_Insert({"Ordered.cpp", "int g_A;\n"});
			FirstFiles.f_Insert({"Ordered.txt", "a\n"});

			// UTF-8 without a signature, in the workspace as in the depot.
			FirstFiles.f_Insert({"Accented.cpp", "int  g_A; // caf\u00e9\n"});

			// Moved to another path in the workspace.
			FirstFiles.f_Insert({"Renamed.cpp", "int  g_Old;\n"});

			// Its keywords expanded, which differ between its revisions.
			FirstFiles.f_Insert({"Keyword.cpp", "int  g_Id; // $Revision$\n"});

			// Its keywords not expanded in the workspace, as 'p4 sync -K' leaves them.
			FirstFiles.f_Insert({"Unexpanded.cpp", "int  g_Id; // $Revision$\n"});

			// Changed at and after the base, while the workspace has the revision before it.
			FirstFiles.f_Insert({"Behind.cpp", "int  g_Old;\n"});

			// Lines that end in a lone CR, which p4 numbers by their LFs alone: opened, and changed after the base.
			FirstFiles.f_Insert({"MacLines.cpp", "int g_A;\rint g_B;\r"});
			FirstFiles.f_Insert({"MacBase.cpp", "int g_A;\r"});

			// Its lone CRs turned into LFs, a change of each line to p4.
			FirstFiles.f_Insert({"CrToLf.cpp", "int  g_A;\rint g_B;\r"});

			// Its last line, too long already, loses its final newline.
			FirstFiles.f_Insert({"NoNewline.txt", "a\n" + LongLine + "\n"});

			// Changed after the base, and changed in the workspace without being opened, which the base doesn't validate.
			FirstFiles.f_Insert({"LocalChange.cpp", "int g_A;\n"});

			// Its keywords not expanded, and after the base text with what reads as an expanded one.
			FirstFiles.f_Insert({"Rekeyed.cpp", "int g_A; // $Id$\n"});
			TCVector<CStr> TypeMap =
				{
					"text+k //Streams/Main/Keyword.cpp"
					, "text+k //Streams/Main/Unexpanded.cpp"
					, "utf16 //Streams/Main/WideType.txt"
				}
			;
			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, TypeMap]()
					{
						NPerforce::CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());
						auto FirstTypeMap = TypeMap;
						FirstTypeMap.f_Insert("text+k //Streams/Main/Rekeyed.cpp");
						Client.f_SetTypeMap(FirstTypeMap);
					}
					% "Failed to set the type map"
				)
			;

			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), FirstFiles, CStr("Add the sources"));
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Behind.cpp", "int g_Old;\n"}}, CStr("Fix a line"));

			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			NContainer::CByteVector BinaryContents;
			for (uint8 Byte : {uint8(0), uint8(1), uint8(2), uint8(0)})
				BinaryContents.f_Insert(Byte);

			int64 BinaryChangelist = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, Workspace = Repo.m_Path, BinaryContents, TypeMap, LongLine]() -> int64
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						int64 BinaryChangelist = 0;

						// Binary at a changelist and text after it, and text where the workspace stays while binary after it.
						{
							CFile::fs_WriteFile(Workspace / "Became.cpp", BinaryContents);
							Client.f_Add(Workspace / "Became.cpp");
							Client.f_Submit(Workspace / "Became.cpp", "Add a binary file");
							BinaryChangelist = Client.f_GetHeadChangelist(Workspace / "...");
							Client.f_Delete(Workspace / "Became.cpp");
							Client.f_Submit(Workspace / "Became.cpp", "Delete the binary file");
							CFile::fs_WriteStringToFile(Workspace / "Became.cpp", "int  g_Became;\n", false);
							Client.f_Add(Workspace / "Became.cpp");
							Client.f_Submit(Workspace / "Became.cpp", "Add a text file in its place");

							Client.f_Delete(Workspace / "Retyped.cpp");
							Client.f_Submit(Workspace / "Retyped.cpp", "Delete the text file");
							CFile::fs_WriteFile(Workspace / "Retyped.cpp", BinaryContents);
							Client.f_Add(Workspace / "Retyped.cpp");
							Client.f_Submit(Workspace / "Retyped.cpp", "Add a binary file in its place");

							CFile::fs_WriteFile(Workspace / "Added.bin", BinaryContents);
							Client.f_Add(Workspace / "Added.bin");
						}

						// Typed 'utf16', which p4 reads as UTF-16 in the platform's byte order without a signature.
						{
							CFile::fs_WriteFile(Workspace / "WideType.txt", fg_EncodeUTF16("\uFEFF" + LongLine + "\n", EEndian_Little));
							Client.f_Add(Workspace / "WideType.txt");
							Client.f_Submit(Workspace / "WideType.txt", "Add a UTF-16 file");
						}

						// Typed 'text' after the base, which doesn't expand keywords.
						{
							Client.f_Delete(Workspace / "Rekeyed.cpp");
							Client.f_Submit(Workspace / "Rekeyed.cpp", "Delete the file with keywords");
							Client.f_SetTypeMap(TypeMap);

							CStr Long = fg_Repeat("x", 200);

							CFile::fs_WriteStringToFile(Workspace / "Rekeyed.cpp", "int g_A; // $Id: " + Long + " $\n", false);
							Client.f_Add(Workspace / "Rekeyed.cpp");
							Client.f_Submit(Workspace / "Rekeyed.cpp", "Add it again as text");
						}

						Client.f_OpenForEdit(Workspace / "Wide.txt");
						Client.f_OpenForEdit(Workspace / "Blank.cpp");
						Client.f_OpenForEdit(Workspace / "Synced.cpp");
						Client.f_Delete(Workspace / "Deleted.cpp");
						Client.f_OpenForEdit(Workspace / "Ordered.cpp");
						Client.f_OpenForEdit(Workspace / "Ordered.txt");
						Client.f_OpenForEdit(Workspace / "Accented.cpp");
						Client.f_OpenForEdit(Workspace / "Renamed.cpp");
						Client.f_Move(Workspace / "Renamed.cpp", Workspace / "RenamedTo.cpp");

						return BinaryChangelist;
					}
					% "Failed to set up the test workspace"
				)
			;

			// Submitted while the workspace has the file opened from its first revision: a sync schedules a resolve, and the
			// workspace has the second.
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Synced.cpp", "int g_Old;\n"}}, CStr("Fix a line"));

			// Added after the changelist the binary revision is at, with no revision at it.
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Later.cpp", "int  g_Later;\n"}}, CStr("Add a file"));

			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Keyword.cpp", "int  g_Id; // $Revision$\nint g_B;\n"}}
					, CStr("Change a file with keywords")
				)
			;

			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Behind.cpp", "int g_Old;\nint g_New;\n"}}, CStr("Add a line"));
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"MacBase.cpp", "int g_A;\rint  g_B;\r"}, {"LocalChange.cpp", "int g_A;\nint  g_B;\n"}}
					, CStr("Add lines")
				)
			;

			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Workspace = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						Client.f_Sync(Workspace / "Retyped.cpp#1");
						Client.f_OpenForEdit(Workspace / "Keyword.cpp");
						Client.f_OpenForEdit(Workspace / "Unexpanded.cpp");
						Client.f_OpenForEdit(Workspace / "WideType.txt");
						Client.f_OpenForEdit(Workspace / "MacLines.cpp");
						Client.f_OpenForEdit(Workspace / "CrToLf.cpp");
						Client.f_OpenForEdit(Workspace / "NoNewline.txt");
						Client.f_Sync(Workspace / "Behind.cpp#1");
					}
					% "Failed to sync the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write("MacLines.cpp", "int g_A;\rint g_B;\rint  g_C;\r");
			Repo.f_Write("CrToLf.cpp", "int  g_A;\nint g_B;\n");
			Repo.f_Write("NoNewline.txt", "a\n" + LongLine);

			// A line in front of the revision's, in the workspace's file only.
			CFile::fs_DeleteFile(Repo.m_Path / "LocalChange.cpp");
			Repo.f_WriteWorkspaceText("LocalChange.cpp", "int  g_X;\nint g_A;\nint  g_B;\n");
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			Repo.f_WriteWorkspaceText("Synced.cpp", "int  g_Old;\nint  g_New;\n");
			Repo.f_WriteWorkspaceText("Accented.cpp", "int  g_A; // caf\u00e9\nint  g_B;\n");
			Repo.f_WriteWorkspaceText("RenamedTo.cpp", "int  g_Old;\nint  g_New;\n");
			Repo.f_WriteWorkspaceText("Keyword.cpp", "int  g_Id; // $Revision: #2 $\nint g_B;\nint  g_C;\n");
			Repo.f_WriteWorkspaceText("Unexpanded.cpp", "int  g_Id; // $Revision$\nint  g_C;\n");

			{
				// UTF-16 without a signature, little endian as the platforms are, after a line too long already.
				CFile::fs_WriteFile(Repo.m_Path / "WideType.txt", fg_EncodeUTF16(LongLine + "\n" + LongLine + "\n", EEndian_Little));
			}

			{
				// UTF-16 with its signature, little endian.
				CFile::fs_WriteFile(Repo.m_Path / "Wide.txt", fg_EncodeUTF16("\uFEFFa\n" + LongLine + "\n", EEndian_Little));
			}

			// Unreadable, which shows that it is not read.
			CFile::fs_SetAttributes(Repo.m_Path / "Added.bin", EFileAttrib_UnixAttributesValid);

			Repo.f_WriteWorkspaceText("Blank.cpp", "int g_A;\n\nint g_B;\nint  g_C;\n");
			Repo.f_WriteWorkspaceText("Deleted.cpp", "int g_Deleted;\nint  g_Extra;\n");
			Repo.f_WriteWorkspaceText("Ordered.cpp", "int g_A;\nint  g_B;\n");

			{
				CStr Long = fg_Repeat("x", 200);

				Repo.f_WriteWorkspaceText("Ordered.txt", "a\n" + Long + "\n");
			}

			auto fRun = [&](TCVector<CStr> _Params)
				{
					_Params.f_Insert
						(
							{
								"--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						)
					;

					return Repo.f_Tool(fg_Move(_Params), true);
				}
			;

			// Sets the server's triggers, whose scripts see what a run asks of the server. The shell runs a script, which Windows can't
			// launch by itself.
			auto fSetTriggers = [&](CStr _Triggers) -> TCUnsafeFuture<void>
				{
					co_await
						(
							g_Dispatch(fg_BlockingActor())
							/ [Connection, _Triggers]()
							{
								NPerforce::CPerforceClientThrow Client(Connection);
								Client.f_Login(CStr());
								Client.f_SetTriggers(_Triggers);
							}
							% "Failed to set the triggers"
						)
					;

					co_return {};
				}
			;

			auto fWriteScript = [&](CStr const &_Name, CStr const &_Script)
				{
					CStr Path = Repo.m_Root / _Name;
					CFile::fs_WriteStringToFile(Path, "#!/bin/sh\n" + _Script, false);
					CFile::fs_SetAttributes(Path, EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute);

					return Path;
				}
			;

			auto fTrigger = [&](CStr const &_Name, CStr const &_Command, CStr const &_Script) -> CStr
				{
					return "\t{} command pre-user-{} \"%quote%{}%quote% %quote%{}%quote% %args%\"\n"_f << _Name << _Command << Repo.m_Shell << _Script;
				}
			;

			{
				DMibTestPath("Staged");
				auto Result = co_await fRun({"validate", "--staged"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("could not be validated"), <, 0);

				// Text in UTF-16 with its signature, typed text, is read decoded, for all its NULs, and has all of its lines changed, as p4
				// compares it as its bytes. A file not opened isn't validated.
				DMibExpect(Output.f_Find("Wide.txt:2:"), >=, 0);
				DMibExpect(Output.f_Find("WideType.txt:"), <, 0);

				// The lines of a file whose lines end in a lone CR are numbered as the formatter numbers them.
				DMibExpect(Output.f_Find("MacLines.cpp:3:"), >=, 0);
				DMibExpect(Output.f_Find("CrToLf.cpp:1:"), >=, 0);

				// A line that loses its final newline is changed.
				DMibExpect(Output.f_Find("NoNewline.txt:2:"), >=, 0);

				// Compared with the revision the file was opened from, which the workspace no longer has.
				DMibExpect(Output.f_Find("Synced.cpp:2:"), >=, 0);
				DMibExpect(Output.f_Find("Synced.cpp:1:"), <, 0);

				DMibExpect(Output.f_Find("Deleted.cpp"), <, 0);

				// In the files' order, though only the formatted one's is analyzed.
				auto iFormatted = Output.f_Find("Ordered.cpp:2:");
				auto iLineLength = Output.f_Find("Ordered.txt:2");
				DMibExpect(iFormatted, >=, 0);
				DMibExpect(iLineLength, >, iFormatted);

				// The depot's UTF-8 is the workspace's, in no code page.
				DMibExpect(Output.f_Find("Accented.cpp:2:"), >=, 0);
				DMibExpect(Output.f_Find("Accented.cpp:1:"), <, 0);

				// A move's destination is compared with its source, and the source has no lines.
				DMibExpect(Output.f_Find("RenamedTo.cpp:2:"), >=, 0);
				DMibExpect(Output.f_Find("RenamedTo.cpp:1:"), <, 0);
				DMibExpect(Output.f_Find("Renamed.cpp:"), <, 0);

				// The workspace's file has the keywords of the revision it was opened from expanded, which p4 diff compares
				// with them expanded, or not, as 'p4 sync -K' leaves them, where p4 diff finds the line changed.
				DMibExpect(Output.f_Find("Keyword.cpp:3:"), >=, 0);
				DMibExpect(Output.f_Find("Keyword.cpp:1:"), <, 0);
				DMibExpect(Output.f_Find("Unexpanded.cpp:1:"), >=, 0);
			}

			{
				// A file p4 diff finds unchanged that changes before the diff returns, as a trigger after it changes it, isn't validated.
				DMibTestPath("ChangedAfterDiff");
				co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Unchanged.cpp", "int g_A;\n"}}, CStr("Add a file"));
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path = Repo.m_Path / "Unchanged.cpp"]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Sync(Path);
							Client.f_OpenForEdit(Path);
						}
						% "Failed to open the file"
					)
				;
				CStr Script = fWriteScript("ChangeAfterDiff.sh", "printf 'int g_B;\\n' >> '{}'\nexit 0\n"_f << (Repo.m_Path / "Unchanged.cpp"));
				co_await fSetTriggers("Triggers:\n\tChangeAfterDiff command post-user-diff \"%quote%{}%quote% %quote%{}%quote%\"\n"_f << Repo.m_Shell << Script);
				auto Result = co_await fRun({"validate", "--staged"});
				co_await fSetTriggers("Triggers:\n");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path = Repo.m_Path / "Unchanged.cpp"]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
						}
						% "Failed to revert the file"
					)
				;
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Unchanged.cpp: not validated: it changed during validation"), >=, 0);
			}

			{
				// A file opened for add is read as the files compared are, so one that changes before the diff returns isn't validated.
				DMibTestPath("AddedChangedAfterDiff");
				CStr Path = Repo.m_Path / "AddedLater.cpp";
				Repo.f_Write("AddedLater.cpp", "int g_A;\n");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Add(Path);
						}
						% "Failed to add the file"
					)
				;
				CStr Script = fWriteScript("ChangeAddedAfterDiff.sh", "printf 'int  g_B;\\n' >> '{}'\nexit 0\n"_f << Path);
				co_await fSetTriggers("Triggers:\n\tChangeAddedAfterDiff command post-user-diff \"%quote%{}%quote% %quote%{}%quote%\"\n"_f << Repo.m_Shell << Script);
				auto Result = co_await fRun({"validate", "--staged"});
				co_await fSetTriggers("Triggers:\n");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
						}
						% "Failed to revert the file"
					)
				;
				CFile::fs_DeleteFile(Path);
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("AddedLater.cpp: not validated: it changed during validation"), >=, 0);
				DMibExpect(Output.f_Find("AddedLater.cpp:2:"), <, 0);
			}

			{
				// A file opened for edit that the workspace no longer has isn't validated, and fails the run, where one opened for
				// delete has no lines.
				DMibTestPath("MissingOpened");
				// A binary file opened and missing too isn't validated at all.
				co_await Server
					(
						&NPerforce::CPerforceTestServer::f_SubmitFiles
						, CStr("//Streams/Main")
						, TCVector<CPerforceFile>{{"Missing.cpp", "int g_A;\n"}, {"MissingBinary.cpp", "int g_B;\n"}}
						, CStr("Add the files")
					)
				;
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path = Repo.m_Path / "Missing.cpp", BinaryPath = Repo.m_Path / "MissingBinary.cpp"]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Sync(Path);
							Client.f_Sync(BinaryPath);
							Client.f_OpenForEdit(Path);
							Client.f_OpenForEdit(BinaryPath, "binary");
							CFile::fs_DeleteFile(Path);
							CFile::fs_DeleteFile(BinaryPath);
						}
						% "Failed to open the files"
					)
				;
				auto Result = co_await fRun({"validate", "--staged"});
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path = Repo.m_Path / "Missing.cpp", BinaryPath = Repo.m_Path / "MissingBinary.cpp"]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
							Client.f_Revert(BinaryPath);
						}
						% "Failed to revert the files"
					)
				;
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Missing.cpp: not validated: it is opened but missing from the workspace"), >=, 0);
				DMibExpect(Result.f_GetCombinedOut().f_Find("MissingBinary.cpp: not validated"), <, 0);
			}

			{
				// The revision the workspace has is text, whatever the head's type is.
				DMibTestPath("BaseRetyped");
				auto Result = co_await fRun({"validate", "--base", "@0"});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Retyped.cpp:1:"), >=, 0);
			}

			{
				// A binary revision at the changelist is neither printed, which a trigger refuses, nor compared with: the text after
				// it is new.
				DMibTestPath("BaseBinaryAtBase");
				CStr Script = fWriteScript("RefusePrint.sh", "case \"$*\" in\n*Became.cpp@*) echo \"Refused\"; exit 1;;\nesac\nexit 0\n");
				co_await fSetTriggers("Triggers:\n" + fTrigger("RefusePrint", "print", Script));
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << BinaryChangelist)});
				co_await fSetTriggers("Triggers:\n");
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Refused"), <, 0);
				DMibExpect(Output.f_Find("Became.cpp:1:"), >=, 0);
				DMibExpect(Output.f_Find("Later.cpp:1:"), >=, 0);

				// A file is validated once, however many of its revisions are after the base.
				DMibExpect(Output.f_Find("Became.cpp:1:"), ==, Output.f_FindReverse("Became.cpp:1:"));

				// The revision the workspace has is validated, not the workspace's file, which holds another line in front.
				DMibExpect(Output.f_Find("LocalChange.cpp:2:"), >=, 0);
				DMibExpect(Output.f_Find("LocalChange.cpp:1:"), <, 0);

				// p4 diff2 numbers the lines by their LFs: the one whose lone CRs end the formatter's lines holds them all.
				DMibExpect(Output.f_Find("MacBase.cpp:2:"), >=, 0);
			}

			{
				// A workspace whose revision is from before the base has no change after it, whatever the depot has after it.
				DMibTestPath("BaseHaveBeforeBase");
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << BinaryChangelist)});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Behind.cpp"), <, 0);
			}

			{
				// A revision collapses the keywords its own type expands: the later one is text, whose '$Id: ...' is its own.
				DMibTestPath("BaseRekeyed");
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << BinaryChangelist)});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Rekeyed.cpp:1:"), >=, 0);
			}

			{
				// The depot's revisions are compared without their keywords expanded, which differ between the two.
				DMibTestPath("BaseKeywords");
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << BinaryChangelist)});
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Keyword.cpp:1:"), <, 0);
			}

#ifndef DPlatformFamily_Windows
			{
				// The revision the workspace has is validated, whatever the workspace's file has become since: a symbolic link here.
				DMibTestPath("BaseLinkedInWorkspace");
				CStr Path = Repo.m_Path / "Later.cpp";
				CFile::fs_WriteStringToFile(Repo.m_Root / "LinkTarget.cpp", "int g_Target;\n", false);
				CFile::fs_MakeFileWritable(Path);
				CFile::fs_DeleteFile(Path);
				CFile::fs_CreateSymbolicLink(Repo.m_Root / "LinkTarget.cpp", Path, EFileAttrib_File, ESymbolicLinkFlag_None);
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << BinaryChangelist)});
				CFile::fs_DeleteFile(Path);
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Later.cpp:1:"), >=, 0);
			}
#endif

			{
				// A file changed after the base that the workspace has no revision of, synced to #none with a file of its own in its
				// place, has no change in it, which the other files' don't depend on.
				DMibTestPath("BaseUnsynced");
				CStr Path = Repo.m_Path / "Later.cpp";
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Sync(Path + "#none");
						}
						% "Failed to sync the file to #none"
					)
				;
				Repo.f_Write("Later.cpp", "int  g_Local;\n");
				auto Result = co_await fRun({"validate", "--base", CStr("@{}"_f << BinaryChangelist)});
				CFile::fs_DeleteFile(Path);
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Later.cpp:"), <, 0);
				DMibExpect(Output.f_Find("Became.cpp:1:"), >=, 0);
			}

			{
				// With nothing else opened, a file opened but missing from the workspace leaves no file for the formatter, and the
				// summary still counts the file it couldn't validate.
				DMibTestPath("MissingOpenedAlone");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Workspace = Repo.m_Path]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Revert(Workspace / "...");
							Client.f_OpenForEdit(Workspace / "Blank.cpp");
							CFile::fs_DeleteFile(Workspace / "Blank.cpp");
						}
						% "Failed to open only a missing file"
					)
				;
				auto Result = co_await fRun({"validate", "--staged", "--summary"});
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [Connection, Path = Repo.m_Path / "Blank.cpp"]()
						{
							NPerforce::CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
						}
						% "Failed to revert the file"
					)
				;
				DMibExpect(Result.m_ExitCode, ==, 1u);
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Blank.cpp: not validated: it is opened but missing from the workspace"), >=, 0);
				DMibExpect(Output.f_Find(" line length violation(s), 1 unanalyzable."), >=, 0);
			}

			co_return {};
		};

		// A Unicode server's 'unicode' files, which p4 writes in the workspace in the charset its settings select, and prints in
		// UTF-8.
		DMibTestSuite("PerforceRootCharsets") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing the charsets of a Perforce root's workspace in mib validate");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of the charsets of a Perforce root: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_StartUnicode, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection]()
					{
						NPerforce::CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());
						Client.f_SetTypeMap({"unicode //Streams/Main/Latin.txt", "unicode //Streams/Main/Wide.txt", "unicode //Streams/Main/NulChar.txt"});
					}
					% "Failed to set the type map"
				)
			;

			// A line too long already, accented, and another added after it.
			CStr LongLine = fg_Repeat("x", 200);

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			CStr Text = LongLine + " caf\u00e9\n";

			// A first line of U+0101, whose UTF-16 has no NUL byte where the audit looks for one, before a line that becomes a U+0000.
			CStr NulFirstLine = fg_Repeat("\u0101", 3999);

			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Latin.txt", Text}, {"Wide.txt", Text}, {"NulChar.txt", NulFirstLine + "\na\n b\n"}}
					, CStr("Add the sources")
				)
			;

			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			// Each file synced and opened in its own charset.
			struct CFileCharset
			{
				CStr m_File;
				CStr m_Charset;
			};

			for (auto const &FileCharset : TCVector<CFileCharset>{{"Latin.txt", "iso8859-1"}, {"Wide.txt", "utf16le"}})
			{
				auto FileConnection = Connection;
				FileConnection.m_Charset = FileCharset.m_Charset;
				FileConnection.m_CommandCharset = CStr("utf8");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [FileConnection, Path = Repo.m_Path / FileCharset.m_File]()
						{
							NPerforce::CPerforceClientThrow Client(FileConnection);
							Client.f_Login(CStr());
							Client.f_Sync(Path);
							Client.f_OpenForEdit(Path);
						}
						% "Failed to sync the test workspace"
					)
				;
			}

			CUStr Changed = CStr(Text + LongLine + "\n");
			{
				NContainer::CByteVector Latin;
				for (umint iChar = 0; iChar < Changed.f_GetLen(); ++iChar)
					Latin.f_Insert(uint8(Changed[iChar]));

				CFile::fs_WriteFile(Repo.m_Path / "Latin.txt", Latin);
				CFile::fs_WriteFile(Repo.m_Path / "Wide.txt", fg_EncodeUTF16(Text + LongLine + "\n", EEndian_Little));
			}

			CStr Settings = "P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port;
			Repo.f_MakePerforceRoot(Settings + "P4CHARSET=iso8859-1\n");
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto fRun = [&](TCVector<CStr> _Params)
				{
					_Params.f_Insert
						(
							{
								"--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						)
					;

					return Repo.f_Tool(fg_Move(_Params), true);
				}
			;

			{
				// The workspace's file is in ISO 8859-1, which the unchanged line is the same text in.
				DMibTestPath("Latin1");
				auto Result = co_await fRun({"validate", "--staged"});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("Latin.txt:2:"), >=, 0);
				DMibExpect(Output.f_Find("Latin.txt:1:"), <, 0);
			}

			{
				// The workspace's file is in UTF-16 without a signature. The one in ISO 8859-1 is reverted, which p4 can't read in
				// that charset.
				DMibTestPath("UTF16LE");
				auto RevertConnection = Connection;
				RevertConnection.m_Charset = "iso8859-1";
				RevertConnection.m_CommandCharset = CStr("utf8");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [RevertConnection, Path = Repo.m_Path / "Latin.txt"]()
						{
							NPerforce::CPerforceClientThrow Client(RevertConnection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
						}
						% "Failed to revert the file in ISO 8859-1"
					)
				;

				Repo.f_Write(".p4config", Settings + "P4CHARSET=utf16le\nP4COMMANDCHARSET=utf8\n");
				auto Result = co_await fRun({"validate", "--staged"});
				CStr Output = Result.f_GetCombinedOut();
				// Read as the audit reads it, which takes UTF-16 without a signature, whose NULs a plain read sees, for binary.
				DMibExpect(Output.f_Find("could not be validated"), <, 0);
				DMibExpect(Output.f_Find("Wide.txt:"), <, 0);
			}

			{
				// The workspace's file is in UTF-16 big endian with its signature, which p4 compares converted to UTF-8: its lines are
				// those of its text as the audit reads it, its U+0000 a space, which doesn't end the text. The file in UTF-16 without a
				// signature is reverted, which p4 can't read in that charset.
				DMibTestPath("UTF16BEWithNul");
				auto RevertConnection = Connection;
				RevertConnection.m_Charset = "utf16le";
				RevertConnection.m_CommandCharset = CStr("utf8");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [RevertConnection, Path = Repo.m_Path / "Wide.txt"]()
						{
							NPerforce::CPerforceClientThrow Client(RevertConnection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
						}
						% "Failed to revert the file in UTF-16 without a signature"
					)
				;

				auto NulConnection = Connection;
				NulConnection.m_Charset = "utf16be-bom";
				NulConnection.m_CommandCharset = CStr("utf8");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [NulConnection, Path = Repo.m_Path / "NulChar.txt"]()
						{
							NPerforce::CPerforceClientThrow Client(NulConnection);
							Client.f_Login(CStr());
							Client.f_Sync(Path);
							Client.f_OpenForEdit(Path);
						}
						% "Failed to open the file in UTF-16 big endian"
					)
				;

				// Changed in the workspace: its third line a U+0000 and a b, and a line added after it.
				auto Wide = fg_EncodeUTF16("\uFEFF" + NulFirstLine + "\na\n", EEndian_Big);
				Wide.f_Insert(uint8(0));
				Wide.f_Insert(uint8(0));
				Wide.f_Insert(fg_EncodeUTF16("b\n" + LongLine + "\n", EEndian_Big));

				CFile::fs_WriteFile(Repo.m_Path / "NulChar.txt", Wide);

				Repo.f_Write(".p4config", Settings + "P4CHARSET=utf16be-bom\nP4COMMANDCHARSET=utf8\n");
				auto Result = co_await fRun({"validate", "--staged"});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("could not be validated"), <, 0);
				DMibExpect(Output.f_Find("NulChar.txt:4:"), >=, 0);
			}

			co_return {};
		};

		// A changelist beyond 32 bits, which a server allows with sys.types.allow64, after a base below them.
		DMibTestSuite("PerforceRoot64BitChangelists") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing 64-bit changelists in mib validate");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of 64-bit changelists: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&NPerforce::CPerforceTestServer::f_SetConfigurable, CStr("sys.types.allow64"), CStr("1"));
			co_await Server(&NPerforce::CPerforceTestServer::f_SetCounter, CStr("change"), CStr("100"));
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			int64 BaseChangelist = co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Big.cpp", "int g_A;\n"}}
					, CStr("Add a file")
				)
			;

			// The next changelist is a few below the base in its low 32 bits.
			co_await Server(&NPerforce::CPerforceTestServer::f_SetCounter, CStr("change"), CStr::fs_ToStr((int64(1) << 32) + BaseChangelist - 10));
			int64 Changelist = co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Big.cpp", "int g_A;\nint  g_B;\n"}}
					, CStr("Add a line")
				)
			;

			DMibAssert(Changelist, >, int64(1) << 32);

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
					}
					% "Failed to sync the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			TCVector<CStr> Params =
				{
					"validate"
					, "--base"
					, "@{}"_f << BaseChangelist
					, "--skip-update"
					, "--no-color"
					, "--build-system"
					, Repo.m_Path / "Test.MBuildSystem"
					, "--output-directory"
					, Repo.m_Root / "output"
					, "--no-use-user-settings"
					, "--no-use-cached-environment"
				}
			;

			auto Result = co_await Repo.f_Tool(fg_Move(Params), true);
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Result.f_GetCombinedOut().f_Find("Big.cpp:2:"), >=, 0);

			co_return {};
		};

		// A workspace whose root p4 spells otherwise than the root is spelled, in another case on a volume that ignores case: p4
		// diff names the files as the workspace spells them.
		DMibTestSuite("PerforceRootSpelling") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a Perforce root spelled otherwise than its workspace");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's spelling: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			// Only a volume that ignores case has the workspace's spelling of the root.
			CStr WorkspaceRoot = CFile::fs_GetPath(Repo.m_Path) / CFile::fs_GetFile(Repo.m_Path).f_UpperCase();
			if (!CFile::fs_FileExists(WorkspaceRoot, EFileAttrib_Directory))
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's spelling: the volume distinguishes case");
				co_return {};
			}

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Edited.cpp", "int g_A;\n"}}, CStr("Add a file"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkspaceRoot);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, WorkspaceRoot]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkspaceRoot / "...");
						Client.f_OpenForEdit(WorkspaceRoot / "Edited.cpp");
					}
					% "Failed to open the file"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			Repo.f_Write("Edited.cpp", "int g_A;\nint  g_B;\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Result.f_GetCombinedOut().f_Find("Edited.cpp:2:"), >=, 0);

			co_return {};
		};

		// Two opened files that are hard links of one another share their identity, so each diff p4 gives is matched by its path, also
		// where the workspace's root is spelled otherwise than the project's on a volume that ignores case.
		DMibTestSuite("PerforceRootHardLinks") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing opened files of a Perforce root that are hard links");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's hard links: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			// Only a volume that ignores case has the workspace's spelling of the root.
			CStr WorkspaceRoot = CFile::fs_GetPath(Repo.m_Path) / CFile::fs_GetFile(Repo.m_Path).f_UpperCase();
			if (!CFile::fs_FileExists(WorkspaceRoot, EFileAttrib_Directory))
				WorkspaceRoot = Repo.m_Path;

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"First.cpp", "int g_A;\n"}, {"Second.cpp", "int g_A;\n"}}
					, CStr("Add the files")
				)
			;
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkspaceRoot);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, WorkspaceRoot]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkspaceRoot / "...");
						Client.f_OpenForEdit(WorkspaceRoot / "First.cpp");
						Client.f_OpenForEdit(WorkspaceRoot / "Second.cpp");
					}
					% "Failed to open the files"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			Repo.f_Write("First.cpp", "int g_A;\nint  g_B;\n");
			CFile::fs_DeleteFile(Repo.m_Path / "Second.cpp");
			CFile::fs_CreateHardLink(Repo.m_Path / "First.cpp", Repo.m_Path / "Second.cpp");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("First.cpp:2:"), >=, 0);
			DMibExpect(Output.f_Find("Second.cpp:2:"), >=, 0);

			co_return {};
		};

		// --base compares the revisions of two text types, as text and utf8, as text, and a file added since has all of its lines,
		// without comparing again.
		DMibTestSuite("PerforceRootTextTypes") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing the revisions of two text types in a Perforce root");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's text types: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

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
					, TCVector<CPerforceFile>{{"Retyped.cpp", "int g_A;\nint g_B;\n"}}
					, CStr("Add the file")
				)
			;
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, AdminConnection, Workspace = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						Client.f_Delete(Workspace / "Retyped.cpp");
						Client.f_Submit(Workspace / "Retyped.cpp", "Delete the text file");

						NPerforce::CPerforceClientThrow Admin(AdminConnection);
						Admin.f_Login(CStr());
						Admin.f_SetTypeMap({"utf8 //Streams/Main/Retyped.cpp"});

						CFile::fs_WriteStringToFile(Workspace / "Retyped.cpp", "int g_A;\nint  g_B;\n", false);
						Client.f_Add(Workspace / "Retyped.cpp");
						Client.f_Submit(Workspace / "Retyped.cpp", "Add it as utf8");
						CFile::fs_WriteStringToFile(Workspace / "Added.cpp", "int  g_C;\n", false);
						Client.f_Add(Workspace / "Added.cpp");
						Client.f_Submit(Workspace / "Added.cpp", "Add a file");
					}
					% "Failed to set up the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			CStr ServerLog = Repo.m_Root / "Perforce/p4d/log";
			umint nLogged = CFile::fs_ReadStringFromFile(ServerLog, true).f_GetLen();
			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--base"
						, CStr("@{}"_f << FirstChangelist)
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("Retyped.cpp:2:"), >=, 0);
			DMibExpect(Output.f_Find("Retyped.cpp:1:"), <, 0);
			DMibExpect(Output.f_Find("Added.cpp:1:"), >=, 0);

			// Only the retyped file is compared again as text.
			CStr Logged = CFile::fs_ReadStringFromFile(ServerLog, true).f_Extract(nLogged);
			umint nTextDiffs = 0;
			for (aint iFound = Logged.f_Find("'user-diff2 -t "); iFound >= 0; iFound = Logged.f_Find(iFound + 1, "'user-diff2 -t "))
				++nTextDiffs;
			DMibExpect(nTextDiffs, ==, 1u);

			co_return {};
		};

		// A binary file opened for edit as text, which p4 diff compares with its binary revision as text, has all of its lines
		// changed for --staged, as it has for --base once submitted: one at its head revision, one at an older revision, and one
		// moved, which p4 diff compares with the revision of the file it is moved from.
		DMibTestSuite("PerforceRootRetypedToText") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a binary file of a Perforce root opened for edit as text");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a binary file opened for edit as text: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("OtherWork"), CStr("//Streams/Main"), Repo.m_Root / "Other");
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto OtherConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("OtherWork"));
			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());

			CStr LongLine = fg_Repeat("x", 200);
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, OtherConnection, AdminConnection, Workspace = Repo.m_Path, OtherWorkspace = Repo.m_Root / "Other", LongLine]()
					{
						NPerforce::CPerforceClientThrow Admin(AdminConnection);
						Admin.f_Login(CStr());
						Admin.f_SetTypeMap({"binary //Streams/Main/Retyped.txt", "binary //Streams/Main/Older.txt", "binary //Streams/Main/Moved.txt"});

						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						for (auto pName : {"Retyped.txt", "Older.txt", "Moved.txt"})
						{
							CFile::fs_WriteStringToFile(Workspace / pName, "a\n" + LongLine + "\n", false);
							Client.f_Add(Workspace / pName);
							Client.f_Submit(Workspace / pName, "Add the file as binary");
						}

						Client.f_OpenForEdit(Workspace / "Older.txt");
						CFile::fs_WriteStringToFile(Workspace / "Older.txt", "c\n" + LongLine + "\n", false);
						Client.f_Submit(Workspace / "Older.txt", "Change the file");
						Client.f_Sync(Workspace / "Older.txt#1");

						for (auto pName : {"Retyped.txt", "Older.txt", "Moved.txt"})
							Client.f_OpenForEdit(Workspace / pName, "text");

						Client.f_Move(Workspace / "Moved.txt", Workspace / "MovedTo.txt");
						for (auto pName : {"Retyped.txt", "Older.txt", "MovedTo.txt"})
							CFile::fs_WriteStringToFile(Workspace / pName, NPerforce::CPerforceTestServer::fs_WorkspaceText("b\n" + LongLine + "\n"), false);

						// A text file moved, whose source another workspace makes binary, which a sync of the source alone gives the
						// workspace: p4 diff still compares the moved file with the text revision it was moved from, whose changed lines
						// alone are checked, not with the binary one the workspace now has of the source.
						CFile::fs_WriteStringToFile(Workspace / "Synced.txt", NPerforce::CPerforceTestServer::fs_WorkspaceText("a\n" + LongLine + "\n"), false);
						Client.f_Add(Workspace / "Synced.txt");
						Client.f_Submit(Workspace / "Synced.txt", "Add the file as text");
						Client.f_OpenForEdit(Workspace / "Synced.txt");
						Client.f_Move(Workspace / "Synced.txt", Workspace / "SyncedTo.txt");
						CFile::fs_WriteStringToFile(Workspace / "SyncedTo.txt", NPerforce::CPerforceTestServer::fs_WorkspaceText("b\n" + LongLine + "\n"), false);

						NPerforce::CPerforceClientThrow Other(OtherConnection);
						Other.f_Login(CStr());
						Other.f_Sync(OtherWorkspace / "...");
						Other.f_OpenForEdit(OtherWorkspace / "Synced.txt", "binary");
						Other.f_Submit(OtherWorkspace / "Synced.txt", "Make the file binary");
						Client.f_NoThrow().f_Sync("//Streams/Main/Synced.txt");
					}
					% "Failed to set up the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("Retyped.txt:2:"), >=, 0);
			DMibExpect(Output.f_Find("Older.txt:2:"), >=, 0);
			DMibExpect(Output.f_Find("MovedTo.txt:2:"), >=, 0);
			DMibExpect(Output.f_Find("SyncedTo.txt:2:"), <, 0);

			co_return {};
		};

		// A file whose resolve, which a sync scheduled, is done has the lines of its changes from the revision the workspace has,
		// which p4 diff compares with, not those that the resolve merged in from it.
		DMibTestSuite("PerforceRootResolvedAfterSync") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a file of a Perforce root resolved after a sync");
			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a file resolved after a sync: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Merged.txt", "a\nb\nc\nd\n"}}, CStr("Add a file"));

			CStr LongLine = fg_Repeat("x", 200);
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Workspace = Repo.m_Path, LongLine]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						Client.f_OpenForEdit(Workspace / "Merged.txt");
						CFile::fs_WriteStringToFile(Workspace / "Merged.txt", NPerforce::CPerforceTestServer::fs_WorkspaceText("a\n" + LongLine + "\nb\nc\nd\n"), false);
					}
					% "Failed to set up the test workspace"
				)
			;

			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Merged.txt", "a\nb\nc\nd\n" + LongLine + "\n"}}
					, CStr("Add a line")
				)
			;

			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Workspace = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						Client.f_ResolveAutomatic(Workspace / "Merged.txt", 0, true);
					}
					% "Failed to resolve the file"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("Merged.txt:2:"), >=, 0);
			DMibExpect(Output.f_Find("Merged.txt:6:"), <, 0);

			co_return {};
		};

		// Files whose names differ only in their whitespace are other files, each validated with its own diff.
		DMibTestSuite("PerforceRootWhitespaceNames") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing files of a Perforce root whose names differ in whitespace");
			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of names that differ in whitespace: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Spaced.txt", "a\nb\n"}, {" Spaced.txt", "a\nb\n"}}
					, CStr("Add the files")
				)
			;

			CStr LongLine = fg_Repeat("x", 200);
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Workspace = Repo.m_Path, LongLine]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Workspace / "...");
						Client.f_OpenForEdit(Workspace / "Spaced.txt");
						Client.f_OpenForEdit(Workspace / " Spaced.txt");
						CFile::fs_WriteStringToFile(Workspace / "Spaced.txt", NPerforce::CPerforceTestServer::fs_WorkspaceText(LongLine + "\nb\n"), false);
						CFile::fs_WriteStringToFile(Workspace / " Spaced.txt", NPerforce::CPerforceTestServer::fs_WorkspaceText("a\n" + LongLine + "\n"), false);
					}
					% "Failed to set up the test workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("/Spaced.txt:1:"), >=, 0);
			DMibExpect(Output.f_Find("/ Spaced.txt:2:"), >=, 0);
			DMibExpect(Output.f_Find("/Spaced.txt:2:"), <, 0);
			DMibExpect(Output.f_Find("/ Spaced.txt:1:"), <, 0);

			co_return {};
		};

		// A project that reaches its workspace's root through another spelling, as /tmp and /private/tmp spell one directory on macOS,
		// is validated and formatted with the paths as the workspace spells them, which p4 tells the workspace's paths by, and reports
		// them as the project spells them. The workspace's root is spelled with a separator at its end.
		DMibTestSuite("PerforceRootAliasedWorkspace") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a Perforce root reached through another spelling of its workspace's root");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root under another spelling of its workspace's root: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			// The workspace's root is a symbolic link to the project, which spells the same directory otherwise.
			CStr Alias = Repo.m_Root / "Alias";
			CFile::fs_CreateSymbolicLink(Repo.m_Path, Alias, EFileAttrib_Directory, ESymbolicLinkFlag_None);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Alias + "/");
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<NPerforce::CPerforceTestServer::CFile>{{"Long.txt", "a\n"}, {"Format.cpp", gc_Formatted}}
					, CStr("Add the files")
				)
			;

			CStr LongLine = fg_Repeat("x", 200);

			// UTF-16 little endian with its signature, whose NULs a plain read sees, typed text, which p4 compares as its bytes.
			auto fWideText = [&](CStr const &_Text)
				{
					return fg_EncodeUTF16("\uFEFF" + _Text, EEndian_Little);
				}
			;

			// Each U+0A15 of the first line holds an LF byte.
			auto fRawText = [&](CStr const &_Lines)
				{
					return fWideText(fg_Repeat("\u0A15", 3999) + "\n" + _Lines);
				}
			;

			// A line added, a line deleted, and a U+0A15 deleted from a line that stays, which p4 sees as a line of its deleted.
			struct CWideFile
			{
				CStr m_Name;
				NContainer::CByteVector m_Depot;
				NContainer::CByteVector m_Workspace;
			};

			TCVector<CWideFile> WideFiles =
				{
					{"Widened.txt", fWideText("a\n"), fWideText("a\n" + LongLine + "\n")}
					, {"Shrunk.txt", fWideText("a\n" + LongLine + "\nb\n"), fWideText("a\n" + LongLine + "\n")}
					, {"Merged.txt", fWideText("a\n" + LongLine + "\u0A15\u0A15\n"), fWideText("a\n" + LongLine + "\u0A15\n")}
				}
			;

			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			int64 BaseChangelist = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [AdminConnection, Connection, Alias, Raw = fRawText("a\n"), Changed = fRawText("a\n" + LongLine + "\n"), WideFiles]() -> int64
					{
						{
							NPerforce::CPerforceClientThrow Admin(AdminConnection);
							Admin.f_Login(CStr());
							TCVector<CStr> TypeMap = {"text //Streams/Main/Raw.txt", "text //Streams/Main/RawBase.txt"};
							for (auto const &WideFile : WideFiles)
								TypeMap.f_Insert("text //Streams/Main/" + WideFile.m_Name);
							Admin.f_SetTypeMap(TypeMap);
						}

						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Alias / "...");
						Client.f_OpenForEdit(Alias / "Long.txt");

						CFile::fs_WriteFile(Raw, Alias / "Raw.txt");
						Client.f_Add(Alias / "Raw.txt");
						Client.f_Submit(Alias / "Raw.txt", "Add a UTF-16 file typed text");
						Client.f_OpenForEdit(Alias / "Raw.txt");

						for (auto const &WideFile : WideFiles)
						{
							CFile::fs_WriteFile(WideFile.m_Depot, Alias / WideFile.m_Name);
							Client.f_Add(Alias / WideFile.m_Name);
							Client.f_Submit(Alias / WideFile.m_Name, "Add a UTF-16 file typed text");
							Client.f_OpenForEdit(Alias / WideFile.m_Name);
						}

						// Changed after the base.
						CFile::fs_WriteFile(Raw, Alias / "RawBase.txt");
						Client.f_Add(Alias / "RawBase.txt");
						Client.f_Submit(Alias / "RawBase.txt", "Add another UTF-16 file typed text");
						int64 BaseChangelist = Client.f_GetHeadChangelist(Alias / "...");
						Client.f_OpenForEdit(Alias / "RawBase.txt");
						CFile::fs_WriteFile(Changed, Alias / "RawBase.txt");
						Client.f_Submit(Alias / "RawBase.txt", "Change the UTF-16 file");

						return BaseChangelist;
					}
					% "Failed to set up the workspace"
				)
			;

#ifndef DPlatformFamily_Windows
			// A name with a backslash, opened for add, which Malterlib's file functions take for a separator, beside the file they
			// would read in its place, which Perforce doesn't have.
			CStr Backslashed = Repo.m_Path + "/literal\\thing.txt";
			DMibAssertTrue(fg_WriteNativeFile(Backslashed, "a\n" + LongLine + "\n"));
			auto RemoveBackslashed = g_OnScopeExit / [&]
				{
					unlink(Backslashed.f_GetStr());
				}
			;
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, InWorkspace = Alias + "/literal\\thing.txt"]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Add(InWorkspace);
					}
					% "Failed to add the file with a backslash in its name"
				)
			;
			Repo.f_Write("literal/thing.txt", LongLine + "\n");
#endif

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			CFile::fs_MakeFileWritable(Repo.m_Path / "Long.txt");
			Repo.f_Write("Long.txt", "a\n" + LongLine + "\n");
			CFile::fs_MakeFileWritable(Repo.m_Path / "Raw.txt");
			CFile::fs_WriteFile(fRawText("a\n" + LongLine + "\n"), Repo.m_Path / "Raw.txt");
			for (auto const &WideFile : WideFiles)
			{
				CFile::fs_MakeFileWritable(Repo.m_Path / WideFile.m_Name);
				CFile::fs_WriteFile(WideFile.m_Workspace, Repo.m_Path / WideFile.m_Name);
			}

			// Changed without being opened, and read-only as a file the workspace has not opened is.
			CStr Unopened = Repo.m_Path / "Format.cpp";
			CFile::fs_MakeFileWritable(Unopened);
			Repo.f_Write("Format.cpp", gc_Unformatted);
#ifdef DPlatformFamily_Windows
			CFile::fs_SetAttributes(Unopened, CFile::fs_GetAttributes(Unopened) | EFileAttrib_ReadOnly);
#else
			CFile::fs_SetAttributes(Unopened, EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_GroupRead | EFileAttrib_EveryoneRead);
#endif

			auto fRun = [&](CStr const &_Command, TCVector<CStr> _Params)
				{
					TCVector<CStr> Params = {_Command};
					Params.f_Insert(fg_Move(_Params));
					Params.f_Insert
						(
							{
								"--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
						)
					;

					return Repo.f_Tool(fg_Move(Params), true);
				}
			;

			{
				DMibTestPath("Validate");
				auto Result = co_await fRun("validate", {"--staged"});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Output.f_Find("not under"), <, 0);
				DMibExpect(Output.f_Find("Long.txt:2:"), >=, 0);
				DMibExpect(Output.f_Find("/Alias/"), <, 0);

				// Text in UTF-16 that p4 compares as its bytes has all of its lines changed, the first too, whose U+0A15 hold LF bytes.
				DMibExpect(Output.f_Find("Raw.txt:3:"), >=, 0);
				DMibExpect(Output.f_Find("Raw.txt:1:"), >=, 0);

				// Text in UTF-16 with its signature is text, which the audit reads decoded, for all its NULs, and all of its lines are
				// changed also where p4 sees lines deleted only.
				DMibExpect(Output.f_Find("Widened.txt:2:"), >=, 0);
				DMibExpect(Output.f_Find("Shrunk.txt:2:"), >=, 0);
				DMibExpect(Output.f_Find("Merged.txt:2:"), >=, 0);

#ifndef DPlatformFamily_Windows
				// The file with a backslash in its name is reported unvalidated, and the one its name would be read as isn't read.
				DMibExpect(Output.f_Find("literal\\thing.txt: not validated"), >=, 0);
				DMibExpect(Output.f_Find("thing.txt:1:"), <, 0);
#endif
			}

#ifndef DPlatformFamily_Windows
			{
				// The audit walks past the file with a backslash in its name and reports it unchecked.
				DMibTestPath("Audit");
				auto Result = co_await fRun("validate", {});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Output.f_Find("literal\\thing.txt: not checked"), >=, 0);
			}
#endif

			{
				// The audit reads text in UTF-16 with its signature decoded, as --staged does, for all its NULs, also a file the
				// configuration formats.
				DMibTestPath("AuditWideText");
				CFile::fs_WriteFile(fWideText("int  g_Wide;\n"), Repo.m_Path / "WideFormatted.cpp");
				auto Result = co_await fRun("validate", {});
				CFile::fs_DeleteFile(Repo.m_Path / "WideFormatted.cpp");
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Output.f_Find("Widened.txt:2:"), >=, 0);
				DMibExpect(Output.f_Find("WideFormatted.cpp:1:"), >=, 0);
			}

			{
				// Text in UTF-16 that p4 compares as its bytes has all of its lines changed after a base too.
				DMibTestPath("ValidateBase");
				auto Result = co_await fRun("validate", {"--base", CStr("@{}"_f << BaseChangelist)});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Result.m_ExitCode, ==, 1u);
				DMibExpect(Output.f_Find("RawBase.txt:1:"), >=, 0);
				DMibExpect(Output.f_Find("RawBase.txt:3:"), >=, 0);
			}

			{
				// A read-only file is opened for edit as the workspace spells it.
				DMibTestPath("Format");
				auto Result = co_await fRun("format", {});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Output.f_Find("Format.cpp: opened for edit in Perforce"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Unopened, true), ==, gc_Formatted);
			}

			co_return {};
		};

		// An overlay mapping, '+//Overlay/...', added to the view of a workspace with a file opened maps a second depot file to its path,
		// which fstat lists ahead of the one opened: --staged validates the file opened, as its action has it, whatever the other is.
		DMibTestSuite("PerforceRootOverlayMapping") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a Perforce root whose workspace has an overlay mapping");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root with an overlay mapping: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			auto SetupConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestSetup"));
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			// The file is opened for add in the primary depot, and submitted after in the overlay, which sorts first.
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, SetupConnection, Connection, SetupRoot = Repo.m_Root / "Setup", Workspace = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Admin(AdminConnection);
						Admin.f_Login(CStr());
						Admin.f_CreateDepot("Primary", "local");
						Admin.f_CreateDepot("Overlay", "local");

						NPerforce::CPerforceClient::CClient SetupSpec;
						SetupSpec.m_Root = SetupRoot;
						SetupSpec.m_Description = "Submits the files.";
						SetupSpec.m_View = {{"//Overlay/...", "//TestSetup/..."}};
						Admin.f_SetClient("TestSetup", SetupSpec);

						NPerforce::CPerforceClient::CClient WorkSpec;
						WorkSpec.m_Root = Workspace;
						WorkSpec.m_Description = "Overlays one depot on another.";
						WorkSpec.m_View = {{"//Primary/...", "//TestWork/..."}};
						Admin.f_SetClient("TestWork", WorkSpec);

						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						CFile::fs_WriteStringToFile(Workspace / "File.cpp", "int g_A;\n", false);
						Client.f_Add(Workspace / "File.cpp");

						WorkSpec.m_View.f_Insert({"+//Overlay/...", "//TestWork/..."});
						Admin.f_SetClient("TestWork", WorkSpec);

						NPerforce::CPerforceClientThrow Setup(SetupConnection);
						Setup.f_Login(CStr());
						CStr Path = SetupRoot / "File.cpp";
						CFile::fs_CreateDirectory(SetupRoot);
						CFile::fs_WriteStringToFile(Path, "int g_A;\n", false);
						Setup.f_Add(Path);
						Setup.f_Submit(Path, "Add the file");
					}
					% "Failed to set up the workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			Repo.f_Write("File.cpp", "int g_A;\nint  g_B;\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("File.cpp:2:"), >=, 0);
			DMibExpect(Output.f_Find("File.cpp:1:"), <, 0);

			co_return {};
		};

		// A project reached through a symbolic link to a directory below its workspace's root, which p4 would take for a directory of
		// the link's name, is validated as the workspace spells the directory. Windows resolves no link of the project's path.
		DMibTestSuite("PerforceRootLinkedProject") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a Perforce root reached through a link to a directory of its workspace");
#ifdef DPlatformFamily_Windows
			constexpr bool c_bResolvesLinks = false;
#else
			constexpr bool c_bResolvesLinks = true;
#endif
			if (!c_bResolvesLinks || NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root reached through a link: links aren't resolved, or p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			// The workspace's root is the project's, which the project is run through a link to from outside the workspace. The link
			// leads through a .. after another link, which the kernel resolves after that link, as Away/Sub/.. is Away.
			CStr Link = Repo.m_Root / "Link";
			CFile::fs_CreateDirectory(Repo.m_Root / "Away/Sub");
			CFile::fs_CreateSymbolicLink(Repo.m_Root / "Away/Sub", Repo.m_Root / "Inner", EFileAttrib_Directory, ESymbolicLinkFlag_None);
			CFile::fs_CreateSymbolicLink("Inner/../../" + CFile::fs_GetFile(Repo.m_Path), Link, EFileAttrib_Directory, ESymbolicLinkFlag_Relative);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), Repo.m_Path);
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<NPerforce::CPerforceTestServer::CFile>{{"Long.txt", "a\n"}}
					, CStr("Add the file")
				)
			;

			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, Path = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Path / "...");
						Client.f_OpenForEdit(Path / "Long.txt");
					}
					% "Failed to open the file"
				)
			;

			CStr LongLine = fg_Repeat("x", 200);

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			CFile::fs_MakeFileWritable(Repo.m_Path / "Long.txt");
			Repo.f_Write("Long.txt", "a\n" + LongLine + "\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Link / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("Long.txt:2:"), >=, 0);

			// The audit walks the root the link names, which bounds the walk.
			auto Audit = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Link / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr AuditOutput = Audit.f_GetCombinedOut();
			DMibExpect(Audit.m_ExitCode, ==, 1u);
			DMibExpect(AuditOutput.f_Find("Long.txt:2:"), >=, 0);
			DMibExpect(AuditOutput.f_Find("Refusing"), <, 0);

			co_return {};
		};

		// A project reached through a symbolic link below its workspace's root, whose view maps the link's name and not the directory it
		// leads to, is validated as the project spells it. Windows resolves no link of the project's path.
		DMibTestSuite("PerforceRootLinkMappedByName") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a Perforce root reached through a link its workspace's view maps");
#ifdef DPlatformFamily_Windows
			constexpr bool c_bResolvesLinks = false;
#else
			constexpr bool c_bResolvesLinks = true;
#endif
			if (!c_bResolvesLinks || NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root reached through a mapped link: links aren't resolved, or p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			auto SetupConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestSetup"));
			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			// The workspace's root holds the project and a link to it, Linked, which is all the view maps.
			CStr Link = Repo.m_Root / "Linked";
			CFile::fs_CreateSymbolicLink(CFile::fs_GetFile(Repo.m_Path), Link, EFileAttrib_Directory, ESymbolicLinkFlag_Relative);
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, SetupConnection, Connection, SetupRoot = Repo.m_Root / "Setup", Root = Repo.m_Root, Link]()
					{
						NPerforce::CPerforceClientThrow Admin(AdminConnection);
						Admin.f_Login(CStr());
						Admin.f_CreateDepot("Depot", "local");

						NPerforce::CPerforceClient::CClient SetupSpec;
						SetupSpec.m_Root = SetupRoot;
						SetupSpec.m_Description = "Submits the files.";
						SetupSpec.m_View = {{"//Depot/...", "//TestSetup/..."}};
						Admin.f_SetClient("TestSetup", SetupSpec);

						NPerforce::CPerforceClient::CClient WorkSpec;
						WorkSpec.m_Root = Root;
						WorkSpec.m_Description = "Maps the project by the name of a link to it.";
						WorkSpec.m_View = {{"//Depot/...", "//TestWork/Linked/..."}};
						Admin.f_SetClient("TestWork", WorkSpec);

						NPerforce::CPerforceClientThrow Setup(SetupConnection);
						Setup.f_Login(CStr());
						CFile::fs_CreateDirectory(SetupRoot);
						CFile::fs_WriteStringToFile(SetupRoot / "Long.txt", "a\n", false);
						Setup.f_Add(SetupRoot / "Long.txt");
						Setup.f_Submit(SetupRoot / "Long.txt", "Add the file");

						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(Link / "...");
						Client.f_OpenForEdit(Link / "Long.txt");
					}
					% "Failed to set up the workspace"
				)
			;

			CStr LongLine = fg_Repeat("x", 200);

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");
			CFile::fs_MakeFileWritable(Repo.m_Path / "Long.txt");
			Repo.f_Write("Long.txt", "a\n" + LongLine + "\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"validate"
						, "--staged"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Link / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, ==, 1u);
			DMibExpect(Output.f_Find("Long.txt:2:"), >=, 0);

			co_return {};
		};

		// p4 diff compares a unicode file as p4's read of it in UTF-8, which converts its charset, leaves its signature out and turns
		// the line ends of a workspace whose LineEnd is mac, lone CRs, into LFs: the formatter ends its lines at the CRs, and the
		// lines p4 changes are mapped to them by their count.
		DMibTestSuite("PerforceRootMacLineEnds") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a unicode file of a Perforce root whose workspace ends lines in CR");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's CR line ends: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_StartUnicode, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			auto AdminConnection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, Workspace = Repo.m_Path]()
					{
						NPerforce::CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());
						Client.f_SetTypeMap({"unicode //Streams/Main/Mac.txt", "unicode //Streams/Main/Bom.txt", "unicode //Streams/Main/Mixed.txt"});

						NPerforce::CPerforceClient::CClient Spec;
						Spec.m_Root = Workspace;
						Spec.m_Description = "A workspace whose lines end in CR.";
						Spec.m_LineEndings = "mac";
						Spec.m_Stream = "//Streams/Main";
						Client.f_SetClient("TestWork", Spec);
					}
					% "Failed to set up the server"
				)
			;

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Mac.txt", "caf\u00e9\nb\n"}, {"Bom.txt", "caf\u00e9\nb\n"}, {"Mixed.txt", "caf\u00e9\n\nb\n"}}
					, CStr("Add the files")
				)
			;

			auto Connection = co_await Server(&NPerforce::CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto fOpen = [&](CStr const &_Charset, CStr const &_File) -> TCUnsafeFuture<void>
				{
					auto FileConnection = Connection;
					FileConnection.m_Charset = _Charset;
					FileConnection.m_CommandCharset = CStr("utf8");
					co_await
						(
							g_Dispatch(fg_BlockingActor())
							/ [FileConnection, Path = Repo.m_Path / _File]()
							{
								NPerforce::CPerforceClientThrow Client(FileConnection);
								Client.f_Login(CStr());
								Client.f_Sync(Path);
								Client.f_OpenForEdit(Path);
							}
							% "Failed to open the file"
						)
					;

					co_return {};
				}
			;

			// Longer than a line may be, on the third line.
			CStr LongLine = fg_Repeat("x", 200);

			CStr Settings = "P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port;
			Repo.f_MakePerforceRoot(Settings + "P4CHARSET=iso8859-1\n");
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto fWriteBytes = [&](CStr const &_File, CAnsiStr const &_Bytes)
				{
					NContainer::CByteVector Data;
					Data.f_Insert((uint8 const *)_Bytes.f_GetStr(), _Bytes.f_GetLen());
					CFile::fs_WriteFile(Data, Repo.m_Path / _File);
				}
			;

			auto fValidate = [&]() -> TCUnsafeFuture<CStr>
				{
					auto Result = co_await Repo.f_Tool
						(
							{
								"validate"
								, "--staged"
								, "--skip-update"
								, "--no-color"
								, "--build-system"
								, Repo.m_Path / "Test.MBuildSystem"
								, "--output-directory"
								, Repo.m_Root / "output"
								, "--no-use-user-settings"
								, "--no-use-cached-environment"
							}
							, true
						)
					;
					DMibExpect(Result.m_ExitCode, ==, 1u);

					co_return Result.f_GetCombinedOut();
				}
			;

			{
				// In ISO 8859-1, which p4 converts to UTF-8, a non-ASCII character spelled otherwise there.
				DMibTestPath("Latin1");
				co_await fOpen("iso8859-1", "Mac.txt");
				fWriteBytes("Mac.txt", CAnsiStr("caf\xE9\rb\r") + CAnsiStr(LongLine.f_GetStr()) + "\r");
				CStr Output = co_await fValidate();
				DMibExpect(Output.f_Find("Mac.txt:3:"), >=, 0);
				DMibExpect(Output.f_Find("Mac.txt:1:"), <, 0);
			}

			{
				// In UTF-8 with a signature, which p4 leaves out of the text it compares. The file in ISO 8859-1 is reverted, which p4
				// can't read in that charset.
				DMibTestPath("UTF8BOM");
				auto RevertConnection = Connection;
				RevertConnection.m_Charset = "iso8859-1";
				RevertConnection.m_CommandCharset = CStr("utf8");
				co_await
					(
						g_Dispatch(fg_BlockingActor())
						/ [RevertConnection, Path = Repo.m_Path / "Mac.txt"]()
						{
							NPerforce::CPerforceClientThrow Client(RevertConnection);
							Client.f_Login(CStr());
							Client.f_Revert(Path);
						}
						% "Failed to revert the file in ISO 8859-1"
					)
				;
				co_await fOpen("utf8-bom", "Bom.txt");
				Repo.f_Write(".p4config", Settings + "P4CHARSET=utf8-bom\n");
				fWriteBytes("Bom.txt", CAnsiStr("\xEF\xBB\xBF" "caf\xC3\xA9\rb\r") + CAnsiStr(LongLine.f_GetStr()) + "\r");
				CStr Output = co_await fValidate();
				DMibExpect(Output.f_Find("Bom.txt:3:"), >=, 0);
				DMibExpect(Output.f_Find("Bom.txt:1:"), <, 0);
			}

			{
				// A CRLF among the CRs, which p4's read makes two LFs, so p4 numbers a line more in front of the changed one than
				// the formatter does, which ends one line at the CRLF.
				DMibTestPath("MixedLineEnds");
				co_await fOpen("utf8-bom", "Mixed.txt");
				fWriteBytes("Mixed.txt", CAnsiStr("\xEF\xBB\xBF" "caf\xC3\xA9\r\nb\r") + CAnsiStr(LongLine.f_GetStr()) + "\r");
				CStr Output = co_await fValidate();
				DMibExpect(Output.f_Find("Mixed.txt:3:"), >=, 0);
				DMibExpect(Output.f_Find("Mixed.txt:1:"), <, 0);
			}

			co_return {};
		};

#ifdef DPlatformFamily_macOS
		// The immutable flag, the system's read-only mark on macOS, which no workspace sets, leaves a file of a Perforce root as it
		// is, unopened, also one opened already, and no staging file behind.
		DMibTestSuite("PerforceRootImmutable") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing immutable files of a Perforce root");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's immutable files: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			co_await Server
				(
					&NPerforce::CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Locked.cpp", gc_Unformatted}, {"Opened.cpp", gc_Unformatted}}
					, CStr("Add the files")
				)
			;
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
						Client.f_OpenForEdit(Workspace / "Opened.cpp");
					}
					% "Failed to open the file"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto fSetImmutable = [&](CStr const &_Path, bool _bImmutable)
				{
					auto Attributes = CFile::fs_GetAttributes(_Path);
					CFile::fs_SetAttributes(_Path, _bImmutable ? Attributes | EFileAttrib_ReadOnly : Attributes & ~EFileAttrib_ReadOnly);
				}
			;
			// Armed before the flags are set, so a failure leaves no immutable file behind for the fixture to remove.
			auto fRestore = [&]
				{
					for (auto pName : {"Locked.cpp", "Opened.cpp"})
						fSetImmutable(Repo.m_Path / pName, false);
					for (auto &File : CFile::fs_FindFiles(Repo.m_Path / "*.mtool-format-*"))
						fSetImmutable(File, false);
				}
			;
			auto Restore = g_OnScopeExitCatch / [&]
				{
					fRestore();
				}
			;
			for (auto pName : {"Locked.cpp", "Opened.cpp"})
				fSetImmutable(Repo.m_Path / pName, true);

			auto Result = co_await Repo.f_Tool
				(
					{
						"format"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;

			auto Staged = CFile::fs_FindFiles(Repo.m_Path / "*.mtool-format-*");
			Restore.f_Clear();
			fRestore();

			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, !=, 0u);
			DMibExpect(Output.f_Find("Locked.cpp: is read-only"), >=, 0);
			DMibExpect(Output.f_Find("Opened.cpp: is read-only"), >=, 0);
			DMibExpect(Staged, ==, TCVector<CStr>{});
			DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Locked.cpp", true), ==, gc_Unformatted);
			DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Opened.cpp", true), ==, gc_Unformatted);

			auto Opened = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, Workspace = Repo.m_Path]() -> TCVector<CStr>
					{
						NPerforce::CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						TCVector<CStr> Opened;
						Client.f_NoThrow().f_GetOpened(Workspace / "...", CStr(), Opened);

						return Opened;
					}
				)
			;
			DMibExpect(Opened.f_GetLen(), ==, 1u);

			co_return {};
		};
#endif

#ifndef DPlatformFamily_Windows
		// A file opened for edit stays open when the write after it fails, here in a directory nothing can be created in, so the
		// open is reported with the failure.
		DMibTestSuite("PerforceRootOpenedNotWritten") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a Perforce root's file opened but not written");

			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the tests of a Perforce root's unwritten files: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			TCActor<NPerforce::CPerforceTestServer> Server = fg_Construct<NPerforce::CPerforceTestServer>();
			co_await Server(&NPerforce::CPerforceTestServer::f_Start, Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			CStr Port = co_await Server(&NPerforce::CPerforceTestServer::f_GetPort);
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&NPerforce::CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			using CPerforceFile = NPerforce::CPerforceTestServer::CFile;
			co_await Server(&NPerforce::CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceFile>{{"Locked/Source.cpp", gc_Unformatted}}, CStr("Add the file"));
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
					}
					% "Failed to sync the workspace"
				)
			;

			Repo.f_MakePerforceRoot("P4PORT={}\nP4USER=tester\nP4CLIENT=TestWork\n"_f << Port);
			Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			// Read-only, as a file the workspace has not opened is, so it is opened for edit before the write fails.
			CStr Locked = Repo.m_Path / "Locked";
			auto FileAttributes = CFile::fs_GetAttributes(Locked / "Source.cpp");
			CFile::fs_SetAttributes
				(
					Locked / "Source.cpp"
					, (FileAttributes & ~(EFileAttrib_UserWrite | EFileAttrib_GroupWrite | EFileAttrib_EveryoneWrite)) | EFileAttrib_UnixAttributesValid
				)
			;
			auto DirectoryAttributes = CFile::fs_GetAttributes(Locked);
			auto Unlock = g_OnScopeExitCatch / [&]
				{
					CFile::fs_SetAttributes(Locked, DirectoryAttributes | EFileAttrib_UnixAttributesValid);
				}
			;
			CFile::fs_SetAttributes(Locked, (DirectoryAttributes & ~(EFileAttrib_UserWrite | EFileAttrib_GroupWrite | EFileAttrib_EveryoneWrite)) | EFileAttrib_UnixAttributesValid);

			auto Result = co_await Repo.f_Tool
				(
					{
						"format"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
					}
					, true
				)
			;
			Unlock.f_Clear();
			CFile::fs_SetAttributes(Locked, DirectoryAttributes | EFileAttrib_UnixAttributesValid);

			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Result.m_ExitCode, !=, 0u);
			DMibExpect(Output.f_Find("Locked/Source.cpp: opened for edit in Perforce"), >=, 0);
			DMibExpect(CFile::fs_ReadStringFromFile(Locked / "Source.cpp", true), ==, gc_Unformatted);

			co_return {};
		};
#endif

		// MTool Format run on its own has no project, so no .p4config file, which any directory can hold, opens a read-only file for
		// edit, and nothing the settings name runs: a file without write permission is replaced as a file outside Perforce is, and
		// one the system marks read-only, which the replacement can't take the place of, is left as it is.
		DMibTestSuite("FormatStandaloneReadsNoP4Config") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing MTool Format with a .p4config file beside a read-only file");

			CRepositoryFixture Repo;
			co_await Repo.f_Init();
			Repo.f_SetPerforceEnvironment();

			CStr Directory = Repo.m_Root / "Standalone";
			CStr Marker = Repo.m_Root / "Ran";
			CStr Script = Directory / "Port.sh";
			CStr Source = Directory / "Source.cpp";
			CFile::fs_CreateDirectory(Directory);
			CFile::fs_WriteStringToFile(Script, "#!/bin/sh\ntouch \"{}\"\nexit 1\n"_f << Marker, false);
			CFile::fs_WriteStringToFile(Directory / ".p4config", "P4PORT=rsh:{} {}\nP4CLIENT=Untrusted\n"_f << Repo.m_Shell << Script, false);
			CFile::fs_WriteStringToFile(Directory / ".editorconfig", g_FormatConfiguration, false);

			auto fFormat = [&](EFileAttrib _ReadOnly) -> TCUnsafeFuture<CProcessLaunchActor::CSimpleLaunchResult>
				{
					CFile::fs_WriteStringToFile(Source, gc_Unformatted, false);
					auto Attributes = CFile::fs_GetAttributes(Source);
					auto Restore = g_OnScopeExitCatch / [&]
						{
							CFile::fs_SetAttributes(Source, (CFile::fs_GetAttributes(Source) & ~EFileAttrib_ReadOnly) | (Attributes & EFileAttrib_UserWrite));
						}
					;
					CFile::fs_SetAttributes(Source, (Attributes | (_ReadOnly & EFileAttrib_ReadOnly)) & ~(_ReadOnly & EFileAttrib_UserWrite));

					co_return co_await Repo.f_Tool({"Format", "--no-color", "-C", Directory, "-f", "Source.cpp"});
				}
			;

			if (CFile::fs_GetSupportedAttributes() & EFileAttrib_UserWrite)
			{
				DMibTestPath("NoWritePermission");
				auto Result = co_await fFormat(EFileAttrib_UserWrite | EFileAttrib_UnixAttributesValid);
				DMibExpect(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("opened for edit"), <, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Source, true), ==, gc_Formatted);
			}

#if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Windows)
			// Where the read-only mark is the macOS immutable flag or the read-only attribute of Windows, not the missing write
			// permission as on Linux.
			{
				DMibTestPath("MarkedReadOnly");
				auto Result = co_await fFormat(EFileAttrib_ReadOnly);
				DMibExpect(Result.m_ExitCode, !=, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find("Source.cpp: is read-only"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Source, true), ==, gc_Unformatted);
				DMibExpect(CFile::fs_FindFiles(Directory / "Source.cpp.mtool-format-*"), ==, TCVector<CStr>{});
			}
#endif

			DMibExpectFalse(CFile::fs_FileExists(Marker));

			co_return {};
		};

		// A .p4config file above the project's root, the build system's base directory, doesn't make it a Perforce root.
		DMibTestSuite("PerforceRootConfigAboveProject") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing a .p4config file above the project's root");

			CRepositoryFixture Repo;
			co_await Repo.f_Init();
			Repo.f_SetPerforceEnvironment();
			CFile::fs_DeleteDirectoryRecursive(Repo.m_Path / ".git");
			CFile::fs_WriteStringToFile(Repo.m_Root / ".p4config", "P4CLIENT=Above\n", false);
			Repo.f_Write(".editorconfig", g_FormatConfiguration);
			Repo.f_Write("Source.cpp", gc_Unformatted);
			Repo.f_Write("Test.MBuildSystem", "Property\n{\n\tPerforceRoot {Format: true}\n}\n");

			auto Result = co_await Repo.f_Tool
				(
					{
						"format"
						, "--skip-update"
						, "--no-color"
						, "--build-system"
						, Repo.m_Path / "Test.MBuildSystem"
						, "--output-directory"
						, Repo.m_Root / "output"
						, "--no-use-user-settings"
						, "--no-use-cached-environment"
						, "--check"
					}
					, true
				)
			;
			CStr Output = Result.f_GetCombinedOut();
			DMibExpect(Output.f_Find("Source.cpp"), <, 0);
			DMibExpect(Output.f_Find("No repository sets Repository.Format"), >=, 0);

			co_return {};
		};
	}

	DMibTestRegister(CFormat_Tests, Malterlib::Tool);
}
