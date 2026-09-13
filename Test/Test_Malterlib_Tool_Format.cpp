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
		CStr const gc_Unformatted = "void fg_Test()\n{\n\n        int a;   \n\tif(a==2)\n\t\tfg_Other(a,1);\n}\n";
		CStr const gc_Formatted = "void fg_Test()\n{\n\t\tint a;\n\tif (a == 2)\n\t\tfg_Other(a, 1);\n}\n";

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
								"Format", "--no-color", "-C", Repo.m_Path, "--check"
								, "--file", "Source/Deep/Example.cpp", "--pattern", "Source/*.cpp,Source/*.h", "--recursive"
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

				DMibTestCategory("WriteIsIdempotent") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "WriteIsIdempotent");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Example.cpp", gc_Unformatted);

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

					auto Single = co_await Repo.f_Tool
						({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check", "--jobs", "1"})
					;
					auto Parallel = co_await Repo.f_Tool
						({"Format", "--no-color", "-C", Repo.m_Path, "--pattern", "Source/*.cpp", "--recursive", "--check", "--jobs", "8"})
					;
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
					Repo.f_Write("Example.cpp", "void fg_Test()\n{\n        int a;\n        int b;\n}\n");

					auto Result = co_await Repo.f_Tool({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--lines", "3:3"});
					DMibExpect(Result.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, "void fg_Test()\n{\n\t\tint a;\n        int b;\n}\n");

					co_return {};
				};

				DMibTestCategory("ByteRangeAndStrict") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "ByteRangeAndStrict");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					CStr Source = "void fg_Test()\n{\n        int a;\n        int b;\n}\n";
					Repo.f_Write("Example.cpp", Source);

					// A partial selection expands to the line it touches by default.
					auto Expanded = co_await Repo.f_Tool
						({"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp", "--offset", "20", "--length", "2"})
					;
					DMibExpect(Expanded.m_ExitCode, ==, 0u);
					DMibExpect(CFile::fs_ReadStringFromFile(Repo.m_Path / "Example.cpp", true), ==, CStr("void fg_Test()\n{\n\t\tint a;\n        int b;\n}\n"));

					Repo.f_Write("Example.cpp", Source);
					auto Strict = co_await Repo.f_Tool
						(
							{
								"Format", "--no-color", "-C", Repo.m_Path, "--file", "Example.cpp"
								, "--offset", "20", "--length", "2", "--strict-range"
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
					Repo.f_Write("Example.cpp", "void fg_Test()\n{\n        int a;\n}\n");
					co_await Repo.f_Commit();

					// The committed indentation violation is unchanged, so only the new line is reported.
					Repo.f_Write("Example.cpp", "void fg_Test()\n{\n        int a;\n        int b;\n}\n");
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("Staged", 1);
					DMibExpect(Output.f_Find("Example.cpp:4:1: indentation"), >=, 0);
					DMibExpect(Output.f_Find("Example.cpp:3:1: indentation"), ==, -1);
					DMibExpect(Output.f_Find("formatting violation(s)"), >=, 0);

					co_return {};
				};

				DMibTestCategory("AuditReportsEveryLine") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "AuditReportsEveryLine");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", gc_FormatConfiguration);
					Repo.f_Write("Example.cpp", "void fg_Test()\n{\n        int a;\n        int b;\n}\n");
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
					Repo.f_Write("Example.cpp", "void fg_Test()\n{\n        int a;\n        int b;\n}\n");
					co_await Repo.f_Commit();

					// Removing a line adds none, so the remaining violations stay suppressed.
					Repo.f_Write("Example.cpp", "void fg_Test()\n{\n        int a;\n}\n");
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
		}
	};

	DMibTestRegister(CFormat_Tests, Malterlib::Tool);
}
