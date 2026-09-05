// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		CStr const gc_GeneratedHeader = "<!-- GENERATED FILE: Do not edit manually. Run ./mib update-agents to regenerate. -->\n\n";

		struct CAgentFixture : CRepositoryFixture
		{
			TCFuture<void> f_Generate(CStr _Case, TCVector<CStr> _Extra = {}, uint32 _Expected = 0)
			{
				auto Capture = co_await (g_CaptureExceptions % "Generating fixture agent instructions");
				DMibTestPath(_Case);
				TCVector<CStr> Params = {"update-agents", "--no-color", "-C", m_Path};
				Params.f_Insert(fg_Move(_Extra));
				auto Result = co_await f_Tool(fg_Move(Params), true);

				DMibExpect(Result.m_ExitCode, ==, _Expected);
				if (Result.m_ExitCode != _Expected)
					co_return DMibErrorInstance(Result.f_GetCombinedOut());

				co_return {};
			}

			CStr f_Read(CStr const &_Name = "AGENTS.md") const
			{
				return CFile::fs_ReadStringFromFile(m_Path / _Name, true);
			}
		};
	}

	struct CUpdateAgents_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Includes") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing agent instruction generation");

				DMibTestCategory("OrderingAndRelativePaths") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing include ordering");
					CAgentFixture Repo;
					Repo.f_Write("CLAUDE.md", "# Root\nSee @bare.md and `@inline.md`.\n@nested/one.md\n`@nested/./one.md`\n");
					Repo.f_Write("nested/one.md", "# One\n@../shared.md\n");
					Repo.f_Write("shared.md", "Shared\n");
					Repo.f_Write("inline.md", "Inline\n");
					Repo.f_Write("bare.md", "Bare\n");

					co_await Repo.f_Generate("Generate");

					CStr Expected = gc_GeneratedHeader
						+ "<!-- Begin include: CLAUDE.md -->\n"
						"# Root\nSee @bare.md (see below) and `@inline.md` (see below).\n"
						"@nested/one.md  (see below)\n`@nested/./one.md`  (see below)\n"
						"<!-- Begin include: nested/one.md -->\n# One\n@../shared.md  (see below)\n"
						"<!-- Begin include: shared.md -->\nShared\n\n<!-- End include: shared.md -->\n"
						"\n<!-- End include: nested/one.md -->\n"
						"<!-- Begin include: inline.md -->\nInline\n\n<!-- End include: inline.md -->\n"
						"<!-- Begin include: bare.md -->\nBare\n\n<!-- End include: bare.md -->\n"
						"\n<!-- End include: CLAUDE.md -->\n"
					;
					DMibExpect(Repo.f_Read(), ==, Expected);
					DMibExpect(CFile::fs_FileExists(Repo.m_Path / ".git", EFileAttrib_Directory), ==, false);

					co_return {};
				};

				DMibTestCategory("CyclesDuplicatesAndMissingFiles") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing include graph markers");
					CAgentFixture Repo;
					Repo.f_Write("CLAUDE.md", "@nested/one.md\n@shared.md\n@missing.md\n");
					Repo.f_Write("nested/one.md", "@../CLAUDE.md\n@../shared.md\n");
					Repo.f_Write("shared.md", "Shared\n");

					co_await Repo.f_Generate("Generate");
					auto Output = Repo.f_Read();
					CStr Cycle = Repo.m_Path / "CLAUDE.md" + " -> " + (Repo.m_Path / "nested/one.md") + " -> " + (Repo.m_Path / "CLAUDE.md");

					DMibExpect(Output.f_Find("<!-- Cyclic include detected: " + Cycle + " -->\n"), >=, 0);
					DMibExpect(Output.f_Find("<!-- Duplicate include skipped: shared.md -->\n"), >=, 0);
					DMibExpect(Output.f_Find("<!-- Missing include: missing.md (resolved to " + (Repo.m_Path / "missing.md") + ") -->\n"), >=, 0);

					co_return {};
				};

				DMibTestCategory("CustomPathsBomAndLineEndings") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing portable agent output");
					CAgentFixture Repo;
					Repo.f_Write("docs/input.md", "\xef\xbb\xbf# Input\r\n\t``` @é.md ```\rFinal");
					Repo.f_Write("docs/é.md", "\xef\xbb\xbfUnicode\r\n");

					co_await Repo.f_Generate("Generate", {"--input", "docs/input.md", "--output", "custom.md"});

					CStr Expected = gc_GeneratedHeader
						+ "<!-- Begin include: docs/input.md -->\n# Input\n\t``` @é.md ```  (see below)\nFinal"
						"<!-- Begin include: docs/é.md -->\nUnicode\n\n<!-- End include: docs/é.md -->\n"
						"\n<!-- End include: docs/input.md -->\n"
					;
					DMibExpect(Repo.f_Read("custom.md"), ==, Expected);
					DMibExpect(CFile::fs_FileExists(Repo.m_Path / "AGENTS.md"), ==, false);

					co_return {};
				};

				DMibTestCategory("EmptyAndRepeatedGeneration") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing repeated generation");
					CAgentFixture Repo;
					Repo.f_Write("CLAUDE.md", "");
					co_await Repo.f_Generate("First");
					auto First = Repo.f_Read();
					co_await Repo.f_Generate("Second");

					DMibExpect(First, ==, gc_GeneratedHeader + "<!-- Begin include: CLAUDE.md -->\n\n<!-- End include: CLAUDE.md -->\n");
					DMibExpect(Repo.f_Read(), ==, First);

					co_return {};
				};

				DMibTestCategory("MissingInputPreservesOutput") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing missing input");
					CAgentFixture Repo;
					Repo.f_Write("AGENTS.md", "Keep existing output\n");

					co_await Repo.f_Generate("Missing", {}, 2);

					DMibExpect(Repo.f_Read(), ==, CStr("Keep existing output\n"));

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CUpdateAgents_Tests, Malterlib::Tool);
}
