// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/String/Appender>

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
				DMibTestPath("Diff");
				auto Result = co_await fFormat({"--diff"});
				CStr Output = Result.f_GetCombinedOut();
				DMibExpect(Output.f_Find("+++ b/Source.cpp"), >=, 0);
				DMibExpect(Output.f_Find("Ignored"), <, 0);
				DMibExpect(Output.f_Find("Unknown"), <, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, gc_Unformatted);
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
	}

	DMibTestRegister(CFormat_Tests, Malterlib::Tool);
}
