// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		CStr const gc_FormatConfiguration =
			"root = true\n\n[*]\nindent_style = tab\nindent_size = 4\ntab_width = 4\nmax_line_length = 190\n\n"
			"[*.{c,cpp,h,hpp}]\nmalterlib_format = malterlib\n"
		;
		CStr const gc_Unformatted = "void fg_Test()\n{\n\n    int a;   \n\tif(a==2)\n\t\tfg_Other(a,1);\n}\n";
		CStr const gc_Formatted = "void fg_Test()\n{\n\tint a;\n\tif (a == 2)\n\t\tfg_Other(a, 1);\n}\n";

		struct CRejectedCase
		{
			CStr m_Name;
			TCVector<CStr> m_Params;
		};
	}

	struct CFormat_Tests : CTest
	{
		void f_DoTests()
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Source.cpp", gc_Formatted);
					Repo.f_Write("Plain/Bad.cpp", gc_Unformatted);
					Repo.f_Write(".gitignore", "/Nested/\n/Plain/\n");
					co_await Repo.f_Git({"init", "-q", "Nested"});
					Repo.f_Write("Nested/.editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Example.cpp", gc_Unformatted);

					auto fExpectUnchanged = [&](CStr _Case)
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
					CFile::fs_WriteStringToFile(Repo.m_Root / "Plain/.editorconfig", gc_FormatConfiguration, false);
					CFile::fs_WriteStringToFile(Repo.m_Root / "Plain/Example.cpp", gc_Unformatted, false);
					CFile::fs_CreateSymbolicLink(Repo.m_Root / "Plain", Repo.m_Root / "PlainLink", EFileAttrib_Directory, ESymbolicLinkFlag_None);

					auto Linked = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Root / "PlainLink", "--pattern", "*.cpp"});
					DMibExpect(Linked.m_ExitCode, !=, 0u);
					DMibExpect(Linked.f_GetCombinedOut().f_Find("Refusing to walk the symbolic link"), >=, 0);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Root / "Plain/Example.cpp", true), ==, gc_Unformatted);

					CFile::fs_CreateDirectory(Repo.m_Path / "Sub");
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Example.cpp", gc_Unformatted);
					co_await Repo.f_Git({"-C", Repo.m_Root, "init", "-q", "Other"});
					CFile::fs_WriteStringToFile(Repo.m_Root / "Other/.editorconfig", gc_FormatConfiguration, false);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					CStr Files;
					for (umint i = 0; i < 100; ++i)
					{
						Repo.f_Write("File{}.cpp"_f << i, gc_Unformatted);
						Files += "File{}.cpp,"_f << i;
					}

					auto Result = co_await Repo.f_Tool({"Format", "--no-color", "--check", "--jobs", "1", "-C", Repo.m_Path, "--file", Files + "Missing.cpp"});
					DMibExpect(Result.m_ExitCode, ==, 2u);
					DMibExpect(Result.f_GetCombinedOut().f_Find("is not an existing regular file"), >=, 0);

					auto Written = co_await Repo.f_Tool({"Format", "--no-color", "--jobs", "1", "-C", Repo.m_Path, "--file", Files + "Missing.cpp"});
					DMibExpect(Written.m_ExitCode, ==, 2u);
					for (umint i = 0; i < 100; ++i)
					{
						if (CFile::fs_ReadStringFromFile(Repo.m_Path / ("File{}.cpp"_f << i), true) == gc_Unformatted)
							continue;

						DMibTestPath("File{}"_f << i);
						DMibExpect(Written.f_GetCombinedOut().f_Find(CStr("/File{}.cpp' was changed before the selection failed"_f << i)), >=, 0);
					}

					co_return {};
				};

				// A changed line starting with a NUL is diffed as the bytes it holds.
				DMibTestCategory("DiffLineStartingWithNul") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "DiffLineStartingWithNul");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Example.cpp", gc_Unformatted);
					// A file beside the one formatted that a user keeps is no staging file of the run.
					Repo.f_Write("Example.cpp.mtool-format", "Kept\n");

					auto fExpectFormatted = [&](CStr _Case)
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					for (umint i = 0; i < 12; ++i)
						Repo.f_Write("Source/Example{}.cpp"_f << i, gc_Unformatted);

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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					for (umint i = 0; i < 3; ++i)
						Repo.f_Write("Source/Example{}.cpp"_f << i, gc_Unformatted);

					auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check", "--slowest", "2"});
					auto Output = Result.f_GetCombinedOut();
					auto iList = Output.f_Find("The files that took the longest:\n");
					DMibExpect(iList, >=, 0);

					umint nListed = 0;
					for (umint i = 0; i < 3; ++i)
					{
						CStr Line = " s  {}"_f << CFile::fs_AppendPath(Repo.m_Path, CStr("Source/Example{}.cpp"_f << i));
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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

			DMibTestSuite("Validation") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing formatting checks in Validate");

				DMibTestCategory("StagedReportsOnlyChangedLines") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "StagedReportsOnlyChangedLines");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
				CStr const c_Source = "constexpr inline_small static int gc_Value = 1;\n";
				CStr const c_Reordered = "static constexpr inline_small int gc_Value = 1;\n";
				CStr const c_Style = "Version: 1\nSpecifierMacros: ['inline_small']\n";

				DMibTestCategory("NoNamingReadsOnlyCpp") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "NoNamingReadsOnlyCpp");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Source.cpp", c_Source);

					auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Source.cpp"});
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source.cpp", true), ==, c_Source);

					co_return {};
				};

				DMibTestCategory("NearestDocument") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "NearestDocument");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write(".malterlib-format", c_Style);
					Repo.f_Write("Module/.malterlib-format", "Version: 1\n");
					Repo.f_Write("Source/Deep/Source.cpp", c_Source);
					Repo.f_Write("Module/Source.cpp", c_Source);

					auto Named = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Source/Deep/Source.cpp"});
					DMibExpect(Named.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Source/Deep/Source.cpp", true), ==, c_Reordered);

					// The nearest document wins, even one that names nothing.
					auto Empty = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Module/Source.cpp"});
					DMibExpect(Empty.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Module/Source.cpp", true), ==, c_Source);

					co_return {};
				};

				DMibTestCategory("ExtendsAndFallback") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "ExtendsAndFallback");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Malterlib/Core/.malterlib-format", c_Style);
					Repo.f_Write("Malterlib/Module/.malterlib-format", "Version: 1\nExtends: ../Core/.malterlib-format\nSpecifierMacros: {Append: ['inline_medium']}\n");
					Repo.f_Write("Malterlib/Module/Source/Source.cpp", c_Source + "constexpr inline_medium static int gc_Other = 2;\n");
					Repo.f_Write("Other/Source.cpp", c_Source);

					auto Extended = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Malterlib/Module/Source/Source.cpp"});
					DMibExpect(Extended.m_ExitCode, ==, 0u);
					DMibExpect
						(
							CFile::fs_ReadStringFromFile(Repo.m_Path / "Malterlib/Module/Source/Source.cpp", true)
							, ==
							, c_Reordered + "static constexpr inline_medium int gc_Other = 2;\n"
						)
					;

					// Without a document of its own, a file reads Malterlib/Core's.
					auto Fallback = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Other/Source.cpp"});
					DMibExpect(Fallback.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Other/Source.cpp", true), ==, c_Reordered);

					co_return {};
				};

				DMibTestCategory("InvalidDocumentsFail") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "InvalidDocumentsFail");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Unknown/.malterlib-format", "Version: 1\nFunctionName: ['f_.*']\n");
					Repo.f_Write("Unknown/Source.cpp", c_Source);
					Repo.f_Write("Missing/.malterlib-format", "Version: 1\nExtends: Absent/.malterlib-format\n");
					Repo.f_Write("Missing/Source.cpp", c_Source);

					auto Unknown = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Unknown/Source.cpp"});
					DMibExpect(Unknown.m_ExitCode, !=, 0u);
					DMibExpect(Unknown.f_GetCombinedOut().f_Find("Unknown key 'FunctionName'"), >=, 0);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Unknown/Source.cpp", true), ==, c_Source);

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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("A/.malterlib-format", "Version: 1\nExtends: ../B/.malterlib-format\n");
					Repo.f_Write("B/.malterlib-format", "Version: 1\nExtends: ../A/.malterlib-format\n");
					Repo.f_Write("A/Source.cpp", c_Source);
					Repo.f_Write("B/Source.cpp", c_Source);

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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);

					CStr Long = "char const *g_p = \"";
					for (umint i = 0; i < 200; ++i)
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
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
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
		}
	};

	DMibTestRegister(CFormat_Tests, Malterlib::Tool);
}
