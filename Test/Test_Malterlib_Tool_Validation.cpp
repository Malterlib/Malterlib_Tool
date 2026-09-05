// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CValidation_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("CLI") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing MTool Validate");

				DMibTestCategory("EmptyRepository") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "EmptyRepository");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();

					auto Staged = co_await Repo.f_Validate("Staged");
					auto Audit = co_await Repo.f_Validate("Audit", 0, false);

					DMibExpect(Staged.f_Find("Validated 0 staged file(s)"), >=, 0);
					DMibExpect(Audit.f_Find("Validated 0 tracked text file(s)"), >=, 0);
					DMibExpect(CFile::fs_FileExists(Repo.m_Root / "mtool/TrustDatabase.MTool", EFileAttrib_Directory), ==, true);

					co_return {};
				};

				DMibTestCategory("BoundaryTabsUnicodeAndLineNumbers") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "BoundaryTabsUnicodeAndLineNumbers");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 190) + "\n\n\t" + fg_Repeat("x", 187) + "\n" + fg_Repeat("é", 190) + "\n");
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("Staged", 1);

					DMibExpect(Output.f_Find("file.cpp:3: line length 191"), >=, 0);
					DMibExpect(Output.f_Find("file.cpp:1:"), ==, -1);
					DMibExpect(Output.f_Find("file.cpp:4:"), ==, -1);

					co_return {};
				};

				DMibTestCategory("ColumnOverflow") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing column-count overflow");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", "root = true\n[*]\nmax_line_length = 190\ntab_width = {}\n"_f << (TCLimitsInt<umint>::mc_Max / 2 + 1));
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					Repo.f_Write("file.cpp", "\t\t\n");
					co_await Repo.f_Stage();

					for (auto Mode : {"Staged", "Audit", "Base"})
					{
						DMibTestPath(Mode);
						bool bBase = CStr(Mode) == "Base";
						if (bBase)
							co_await Repo.f_Commit();
						auto Output = co_await Repo.f_Validate("Validate", 1, CStr(Mode) == "Staged", bBase ? "origin/master" : "");

						DMibExpect(Output.f_Find("file.cpp:1: line length overflows"), >=, 0);
					}

					co_return {};
				};

				DMibTestCategory("UnchangedLinesAndAudit") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "UnchangedLinesAndAudit");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nold\n");
					co_await Repo.f_Commit();
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nnew\n");
					co_await Repo.f_Stage();

					co_await Repo.f_Validate("Staged");
					auto Output = co_await Repo.f_Validate("Audit", 1, false);

					DMibExpect(Output.f_Find("file.cpp:1:"), >=, 0);

					co_return {};
				};

				DMibTestCategory("PartiallyStagedContentAndRules") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "PartiallyStagedContentAndRules");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();
					Repo.f_Write("file.cpp", "short\n");
					Repo.f_Write(".editorconfig", "root = true\n[*]\nmax_line_length = off\n");

					co_await Repo.f_Validate("Staged", 1);
					co_await Repo.f_Validate("Audit", 0, false);

					co_return {};
				};

				DMibTestCategory("UnstagedViolation") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "UnstagedViolation");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Stage();
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));

					co_await Repo.f_Validate("Staged");
					co_await Repo.f_Validate("Audit", 1, false);

					co_return {};
				};

				DMibTestCategory("MarkdownExclusion") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "MarkdownExclusion");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", "root = true\n[*]\nmax_line_length = 190\n[*.md]\nmax_line_length = unset\n");
					Repo.f_Write("README.md", "| " + fg_Repeat("long table cell ", 30) + " |\n");
					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Stage();

					auto Staged = co_await Repo.f_Validate("Staged");
					auto Audit = co_await Repo.f_Validate("Audit", 0, false);

					DMibExpect(Staged.f_Find("Excluded 1 file(s)"), >=, 0);
					DMibExpect(Audit.f_Find("Validated 2 tracked text file(s)"), >=, 0);

					co_return {};
				};

				DMibTestCategory("CharacterRanges") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing EditorConfig character ranges");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write
						(
							".editorconfig"
							, "root = true\n[*]\nmax_line_length = off\n"
							"[file[0-9].cpp]\nmax_line_length = 190\n"
							"[letter[a-cx-z].cpp]\nmax_line_length = 190\n"
							"[negated[!0-9].cpp]\nmax_line_length = 190\n"
							"[literal[a\\-c].cpp]\nmax_line_length = 190\n"
							"[leading[-ac].cpp]\nmax_line_length = 190\n"
							"[trailing[ac-].cpp]\nmax_line_length = 190\n"
							"[closing[\\]].cpp]\nmax_line_length = 190\n"
							"[{name[ab,],other}.cpp]\nmax_line_length = 190\n"
							"[{brace[{},],{nestedA,nestedB}}.cpp]\nmax_line_length = 190\n"
							"[file{a..b}.cpp]\nmax_line_length = 190\n"
							"[file\\{1..3\\}.cpp]\nmax_line_length = 190\n"
							"[excluded*.cpp]\nmax_line_length = 190\n[excluded[0-9].cpp]\nmax_line_length = off\n"
						)
					;
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});

					TCVector<CStr> Included =
						{
							"file0.cpp", "file5.cpp", "file9.cpp", "letterb.cpp", "lettery.cpp", "negatedx.cpp", "negated-.cpp"
							, "literala.cpp", "literal-.cpp", "literalc.cpp", "leading-.cpp", "trailing-.cpp", "closing].cpp", "excludedx.cpp"
							, "namea.cpp", "name,.cpp", "other.cpp", "brace{.cpp", "brace}.cpp", "brace,.cpp", "nestedB.cpp", "file{a..b}.cpp", "file{1..3}.cpp"
						}
					;
					TCVector<CStr> Excluded = {"file-.cpp", "filex.cpp", "letterd.cpp", "negated5.cpp", "literalb.cpp", "leadingb.cpp", "trailingb.cpp", "excluded5.cpp"};
					for (auto const &Name : Included)
						Repo.f_Write(Name, fg_Repeat("x", 191));
					for (auto const &Name : Excluded)
						Repo.f_Write(Name, fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					for (auto Mode : {"Staged", "Audit", "Base"})
					{
						DMibTestPath(Mode);
						bool bBase = CStr(Mode) == "Base";
						if (bBase)
							co_await Repo.f_Commit();
						auto Output = co_await Repo.f_Validate("Validate", 1, CStr(Mode) == "Staged", bBase ? "origin/master" : "");

						for (auto const &Name : Included)
						{
							DMibTestPath(Name);
							DMibExpect(Output.f_Find(Name + ":1: line length 191"), >=, 0);
						}
						for (auto const &Name : Excluded)
						{
							DMibTestPath(Name);
							DMibExpect(Output.f_Find(Name + ":1:"), ==, -1);
						}
					}

					co_return {};
				};

				DMibTestCategory("RepeatedWildcardsAndAlternatives") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing repeated glob states");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write
						(
							".editorconfig"
							, "root = true\n[*]\nmax_line_length = off\n[" + fg_Repeat("a*", 16) + "b.cpp]\nmax_line_length = 190\n["
							+ fg_Repeat("{c,c}", 24) + "d.cpp]\nmax_line_length = 190\n"
						)
					;
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});

					TCVector<CStr> Included = {fg_Repeat("a", 32) + "b.cpp", fg_Repeat("c", 24) + "d.cpp"};
					TCVector<CStr> Excluded = {fg_Repeat("a", 32) + ".cpp", fg_Repeat("c", 24) + ".cpp"};
					for (auto const &Name : Included)
						Repo.f_Write(Name, fg_Repeat("x", 191));
					for (auto const &Name : Excluded)
						Repo.f_Write(Name, fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					for (auto Mode : {"Staged", "Audit", "Base"})
					{
						DMibTestPath(Mode);
						bool bBase = CStr(Mode) == "Base";
						if (bBase)
							co_await Repo.f_Commit();
						auto Output = co_await Repo.f_Validate("Validate", 1, CStr(Mode) == "Staged", bBase ? "origin/master" : "");

						for (auto const &Name : Included)
						{
							DMibTestPath(Name);
							DMibExpect(Output.f_Find(Name + ":1: line length 191"), >=, 0);
						}
						for (auto const &Name : Excluded)
						{
							DMibTestPath(Name);
							DMibExpect(Output.f_Find(Name + ":1:"), ==, -1);
						}
					}

					co_return {};
				};

				DMibTestCategory("GlobBoundariesAndUnicode") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing glob token boundaries and Unicode");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write
						(
							".editorconfig", "root = true\n[*]\nmax_line_length = off\n"
							"[star/src/{*,x}*/file.cpp]\nmax_line_length = 190\n"
							"[double/prefix**/file.cpp]\nmax_line_length = 190\n"
							"[joined/{**,src}/join.cpp]\nmax_line_length = 190\n"
							"[uni?.cpp]\nmax_line_length = 190\n[set[é😀].cpp]\nmax_line_length = 190\n"
							"[greek[α-ω].cpp]\nmax_line_length = 190\n[negated[!é].cpp]\nmax_line_length = 190\n"
							"[slash[!a/].cpp]\nmax_line_length = 190\n[/root.cpp]\nmax_line_length = 190\n"
							"[\xEF\xBB\xBF" "marker.cpp]\nmax_line_length = 190\n"
						)
					;
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});

					TCVector<CStr> Included =
						{
							"star/src/dir/file.cpp", "double/prefix/file.cpp", "double/prefix/dir/file.cpp", "unié.cpp", "uni😀.cpp"
							, "seté.cpp", "set😀.cpp", "greekλ.cpp", "negatedλ.cpp", "sub/slashb.cpp", "root.cpp"
							, "\xEF\xBB\xBF" "marker.cpp"
							, "joined/join.cpp", "joined/src/join.cpp", "joined/deep/nested/join.cpp"
						}
					;
					TCVector<CStr> Excluded =
						{
							"star/src/dir/nested/file.cpp", "double/prefixfile.cpp", "uniaa.cpp", "seta.cpp", "greekA.cpp"
							, "negatedé.cpp", "sub/slasha.cpp", "sub/root.cpp", "marker.cpp"
						}
					;
					for (auto const &Name : Included)
						Repo.f_Write(Name, fg_Repeat("x", 191));
					for (auto const &Name : Excluded)
						Repo.f_Write(Name, fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					for (auto Mode : {"Staged", "Audit", "Base"})
					{
						DMibTestPath(Mode);
						bool bBase = CStr(Mode) == "Base";
						if (bBase)
							co_await Repo.f_Commit();
						auto Output = co_await Repo.f_Validate("Validate", 1, CStr(Mode) == "Staged", bBase ? "origin/master" : "");

						for (auto const &Name : Included)
						{
							DMibTestPath(Name);
							DMibExpect(Output.f_Find("/repo/" + Name + ":1: line length 191"), >=, 0);
						}
						for (auto const &Name : Excluded)
						{
							DMibTestPath(Name);
							DMibExpect(Output.f_Find("/repo/" + Name + ":1:"), ==, -1);
						}
					}

					co_return {};
				};

				DMibTestCategory("NestedRulesAndRoot") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "NestedRulesAndRoot");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("sub/.editorconfig", "[*.cpp]\nmax_line_length = 20\n[ignored.cpp]\nmax_line_length = unset\n");
					Repo.f_Write("sub/file.cpp", fg_Repeat("x", 21));
					Repo.f_Write("sub/ignored.cpp", fg_Repeat("x", 200));
					Repo.f_Write("isolated/.editorconfig", "root = true\n[*]\ntab_width = 2\n");
					Repo.f_Write("isolated/file.cpp", fg_Repeat("x", 200));
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("Staged", 1);

					DMibExpect(Output.f_Find("sub/file.cpp:1:"), >=, 0);
					DMibExpect(Output.f_Find("ignored.cpp:1:"), ==, -1);
					DMibExpect(Output.f_Find("isolated/file.cpp:1:"), ==, -1);

					co_return {};
				};

				DMibTestCategory("NestedReenable") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "NestedReenable");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", "root = true\n[*]\nmax_line_length = 190\n[Generated/**]\nmax_line_length = unset\n");
					Repo.f_Write("Generated/ignored.cpp", fg_Repeat("x", 200));
					Repo.f_Write("Generated/owned/.editorconfig", "[*.cpp]\nmax_line_length = 20\n");
					Repo.f_Write("Generated/owned/file.cpp", fg_Repeat("x", 21));
					co_await Repo.f_Stage();

					for (bool bStaged : {true, false})
					{
						DMibTestPath(bStaged ? "Staged" : "Audit");
						auto Output = co_await Repo.f_Validate("Validate", 1, bStaged);

						DMibExpect(Output.f_Find("Generated/owned/file.cpp:1:"), >=, 0);
						DMibExpect(Output.f_Find("Generated/ignored.cpp:1:"), ==, -1);
					}

					co_return {};
				};

				DMibTestCategory("GlobsAndPrecedence") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "GlobsAndPrecedence");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write
						(
							".editorconfig"
							, "root = true\n[*]\nmax_line_length = 190\n[/sub/**/file.{cpp,h}]\nmax_line_length = off\n[sub/deep/file.?]\nmax_line_length = 20\n"
						)
					;
					Repo.f_Write("sub/file.cpp", fg_Repeat("x", 200));
					Repo.f_Write("sub/deep/file.cpp", fg_Repeat("x", 200));
					Repo.f_Write("sub/deep/file.h", fg_Repeat("x", 21));
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("Staged", 1);

					DMibExpect(Output.f_Find("sub/deep/file.h:1:"), >=, 0);
					DMibExpect(Output.f_Find("file.cpp:1:"), ==, -1);

					co_return {};
				};

				DMibTestCategory("Renames") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Renames");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("old.cpp", fg_Repeat("x", 200) + "\n" + fg_Repeat("short\n", 20));
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"mv", "old.cpp", "new.cpp"});
					co_await Repo.f_Validate("UnchangedRename");
					Repo.f_Write("new.cpp", fg_Repeat("x", 200) + "\n" + fg_Repeat("short\n", 19) + fg_Repeat("y", 191) + "\n");
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("ChangedRename", 1);

					DMibExpect(Output.f_Find("new.cpp:21:"), >=, 0);
					DMibExpect(Output.f_Find("new.cpp:1:"), ==, -1);

					co_return {};
				};

				DMibTestCategory("RenamedAttributes") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing renamed file attributes");
					for (bool bBinary : {true, false})
					{
						DMibTestPath(bBinary ? "BinaryAttribute" : "TextAttribute");
						CRepositoryFixture Repo;
						co_await Repo.f_Init();
						Repo.f_Write("nested/.gitattributes", bBinary ? "*.dat -diff\n" : "*.dat diff\n");
						CStr Original = "prefix\n" + fg_Repeat("x", 200) + "\n" + fg_Repeat("short\n", 100);
						Repo.f_Write("nested/old.dat", Original);
						if (!bBinary)
						{
							auto Data = CFile::fs_ReadFile(Repo.m_Path / "nested/old.dat");
							Data[0] = 0;
							CFile::fs_WriteFile(Repo.m_Path / "nested/old.dat", Data);
						}
						co_await Repo.f_Commit();
						co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
						co_await Repo.f_Git({"mv", "nested/old.dat", "nested/new [x] é.dat"});
						Repo.f_Write("nested/new [x] é.dat", Original + fg_Repeat("y", 191) + "\n");
						if (!bBinary)
						{
							auto Data = CFile::fs_ReadFile(Repo.m_Path / "nested/new [x] é.dat");
							Data[0] = 0;
							CFile::fs_WriteFile(Repo.m_Path / "nested/new [x] é.dat", Data);
						}
						co_await Repo.f_Stage();

						for (bool bBase : {false, true})
						{
							DMibTestPath(bBase ? "Base" : "Staged");
							if (bBase)
								co_await Repo.f_Commit();
							auto Raw = co_await Repo.f_Git({"diff", "--raw", "--find-renames", "origin/master", bBase ? "HEAD" : "--cached"});
							DMibExpect(Raw.f_Find(" R"), >=, 0);

							auto Output = co_await Repo.f_Validate("Validate", bBinary ? 0 : 1, !bBase, bBase ? "origin/master" : "");

							DMibExpect(Output.f_Find("nested/new [x] é.dat:103: line length 191") >= 0, ==, !bBinary);
							DMibExpect(Output.f_Find("nested/new [x] é.dat:2:"), ==, -1);
						}
					}

					co_return {};
				};

				DMibTestCategory("ReplacedAndCopiedFile") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "ReplacedAndCopiedFile");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("old.cpp", fg_Repeat("x", 200) + "\n" + fg_Repeat("short\n", 20));
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"mv", "old.cpp", "new.cpp"});
					Repo.f_Write("old.cpp", fg_Repeat("y", 191));
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("Staged", 1);

					DMibExpect(Output.f_Find("old.cpp:1:"), >=, 0);
					DMibExpect(Output.f_Find("new.cpp:1:"), >=, 0);

					co_return {};
				};

				DMibTestCategory("DeletedBinaryAndSymlink") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "DeletedBinaryAndSymlink");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("deleted.cpp", fg_Repeat("x", 200));
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"rm", "deleted.cpp"});
					Repo.f_WriteBinary("binary.dat");
					co_await Repo.f_Stage();
					auto Hash = (co_await Repo.f_Git({"hash-object", "-w", "binary.dat"})).f_Trim();
					co_await Repo.f_Git({"update-index", "--add", "--cacheinfo", "120000," + Hash + ",link"});

					co_await Repo.f_Validate("Staged");

					co_return {};
				};

				DMibTestCategory("LiteralPathsAndLargeListing") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "LiteralPathsAndLargeListing");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					for (umint i = 0; i < 256; ++i)
						Repo.f_Write(CStr("listing/{}_"_f << i) + fg_Repeat("é", 50) + ".cpp", "short\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Validate("InitialAudit", 0, false);
					Repo.f_Write("space [x] é.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					for (bool bStaged : {true, false})
					{
						DMibTestPath(bStaged ? "Staged" : "Audit");
						auto Output = co_await Repo.f_Validate("Validate", 1, bStaged);

						DMibExpect(Output.f_Find("space [x] é.cpp:1:"), >=, 0);
					}

					co_return {};
				};

				DMibTestCategory("ColorAndExternalDiff") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "ColorAndExternalDiff");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					co_await Repo.f_Git({"config", "color.ui", "always"});
					co_await Repo.f_Git({"config", "diff.external", "nonexistent-diff-command"});
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					co_await Repo.f_Validate("Staged", 1);
					co_await Repo.f_Validate("Audit", 1, false);

					co_return {};
				};

				DMibTestCategory("AlternateIndex") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "AlternateIndex");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Commit();
					CStr Alternate = Repo.m_Path / ".git/alternate-index";
					CFile::fs_WriteFile(Alternate, CFile::fs_ReadFile(Repo.m_Path / ".git/index"));
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					co_await Repo.f_Validate("OriginalIndex", 1);
					Repo.m_Environment["GIT_INDEX_FILE"] = Alternate;
					co_await Repo.f_Validate("AlternateIndex");

					co_return {};
				};

				DMibTestCategory("BomAndLineEndings") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "BomAndLineEndings");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".editorconfig", "\xEF\xBB\xBFroot = true\r\n[*]\r\nmax_line_length = 190\r\n");
					Repo.f_Write("file.cpp", CStr("\xEF\xBB\xBF") + fg_Repeat("x", 190) + "\r\n" + fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					auto Output = co_await Repo.f_Validate("Staged", 1);

					DMibExpect(Output.f_Find("file.cpp:1:"), ==, -1);
					DMibExpect(Output.f_Find("file.cpp:2:"), >=, 0);

					co_return {};
				};

				DMibTestCategory("BareCarriageReturnInPatch") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing carriage returns inside Git patch records");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", "before\runmodified\nold\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					Repo.f_Write("file.cpp", "before\runmodified\nshort\r" + fg_Repeat("x", 191) + "\n" + fg_Repeat("y", 191) + "\r\n");
					Repo.f_Write("short.cpp", fg_Repeat("x", 100) + "\r" + fg_Repeat("y", 100) + "\r");
					co_await Repo.f_Stage();

					for (bool bBase : {false, true})
					{
						DMibTestPath(bBase ? "Base" : "Staged");
						if (bBase)
							co_await Repo.f_Commit();
						auto Output = co_await Repo.f_Validate("Validate", 1, !bBase, bBase ? "origin/master" : "");

						DMibExpect(Output.f_Find("file.cpp:4: line length 191"), >=, 0);
						DMibExpect(Output.f_Find("file.cpp:5: line length 191"), >=, 0);
						DMibExpect(Output.f_Find("short.cpp:"), ==, -1);
					}

					auto Audit = co_await Repo.f_Validate("Audit", 1, false);
					DMibExpect(Audit.f_Find("file.cpp:4: line length 191"), >=, 0);
					DMibExpect(Audit.f_Find("short.cpp:"), ==, -1);

					co_return {};
				};

				DMibTestCategory("EmbeddedNulInText") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing length-aware text validation");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".gitattributes", "*.cpp diff\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					Repo.f_Write("file.cpp", fg_Repeat("x", 191) + "\n");
					auto Data = CFile::fs_ReadFile(Repo.m_Path / "file.cpp");
					Data[5] = 0;
					CFile::fs_WriteFile(Repo.m_Path / "file.cpp", Data);
					Data[0] = 0;
					CFile::fs_WriteFile(Repo.m_Path / "leading.cpp", Data);
					for (auto &Byte : Data)
						Byte = 0;
					CFile::fs_WriteFile(Repo.m_Path / "all.cpp", Data);
					Repo.f_Write("marker.cpp", "short\n" + CStr("\xEF\xBB\xBF") + fg_Repeat("x", 190));
					Repo.f_Write("malformed.cpp", CStr("\x80") + fg_Repeat("\t", 48) + "\n");
					co_await Repo.f_Stage();

					for (auto Mode : {"Staged", "Audit", "Base"})
					{
						DMibTestPath(Mode);
						bool bBase = CStr(Mode) == "Base";
						if (bBase)
							co_await Repo.f_Commit();
						auto Output = co_await Repo.f_Validate("Validate", 1, CStr(Mode) == "Staged", bBase ? "origin/master" : "");

						DMibExpect(Output.f_Find("file.cpp:1: line length 191"), >=, 0);
						DMibExpect(Output.f_Find("leading.cpp:1: line length 191"), >=, 0);
						DMibExpect(Output.f_Find("all.cpp:1: line length 192"), >=, 0);
						DMibExpect(Output.f_Find("marker.cpp:2: line length 191"), >=, 0);
						DMibExpect(Output.f_Find("malformed.cpp:1: line length 192"), >=, 0);
					}

					co_return {};
				};

				DMibTestCategory("UntrackedConfiguration") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "UntrackedConfiguration");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("sub/file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Commit();
					Repo.f_Write("sub/.editorconfig", "[*.cpp]\nmax_line_length = off\n");

					co_await Repo.f_Validate("Audit", 0, false);

					co_return {};
				};

				DMibTestCategory("BaseChangedLines") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "BaseChangedLines");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nold\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nnew\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Validate("ShortChange", 0, false, "origin/master");
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nnew\n" + fg_Repeat("y", 191) + "\n");
					co_await Repo.f_Commit();

					auto Output = co_await Repo.f_Validate("LongChange", 1, false, "origin/master");

					DMibExpect(Output.f_Find("file.cpp:3:"), >=, 0);
					DMibExpect(Output.f_Find("file.cpp:1:"), ==, -1);
					DMibExpect(Output.f_Find("Validated 1 committed file(s)"), >=, 0);

					co_return {};
				};

				DMibTestCategory("BaseHeadSnapshot") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "BaseHeadSnapshot");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Commit();
					Repo.f_Write("file.cpp", "short\n");
					Repo.f_Write(".editorconfig", "root = true\n[*]\nmax_line_length = unset\n");
					Repo.f_Write(".gitattributes", "file.cpp -diff\n");
					co_await Repo.f_Stage();
					auto StagedTree = co_await Repo.f_Git({"write-tree"});

					co_await Repo.f_Validate("Base", 1, false, "origin/master");
					auto StagedTreeAfter = co_await Repo.f_Git({"write-tree"});
					DMibExpect(StagedTreeAfter, ==, StagedTree);

					co_await Repo.f_Validate("Staged");
					co_await Repo.f_Validate("Audit", 0, false);

					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					CFile::fs_DeleteFile(Repo.m_Path / ".gitattributes");
					co_await Repo.f_Commit();
					auto Output = co_await Repo.f_Validate("CommittedExclusion", 0, false, "origin/master");

					DMibExpect(Output.f_Find("Excluded 2 file(s)"), >=, 0);

					co_return {};
				};

				DMibTestCategory("DivergedBase") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "DivergedBase");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nold\n");
					co_await Repo.f_Commit();
					auto Fork = (co_await Repo.f_Git({"rev-parse", "HEAD"})).f_Trim();
					Repo.f_Write("file.cpp", fg_Repeat("x", 200) + "\nnew\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"switch", "-c", "upstream", Fork});
					Repo.f_Write("file.cpp", "old\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					co_await Repo.f_Git({"switch", "main"});

					co_await Repo.f_Validate("MergeBase", 0, false, "origin/master");

					co_return {};
				};

				DMibTestCategory("BaseRenamesAndNestedRules") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "BaseRenamesAndNestedRules");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("old.cpp", fg_Repeat("x", 200) + "\n" + fg_Repeat("short\n", 20));
					Repo.f_Write("deleted.cpp", fg_Repeat("x", 200));
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					co_await Repo.f_Git({"mv", "old.cpp", "renamed [x] é.cpp"});
					co_await Repo.f_Git({"rm", "deleted.cpp"});
					Repo.f_WriteBinary("binary.dat");
					Repo.f_Write("sub/.editorconfig", "[*.cpp]\nmax_line_length = 20\n");
					Repo.f_Write("sub/file.cpp", fg_Repeat("x", 21));
					co_await Repo.f_Commit();

					auto Initial = co_await Repo.f_Validate("Initial", 1, false, "origin/master");

					DMibExpect(Initial.f_Find("sub/file.cpp:1:"), >=, 0);
					DMibExpect(Initial.f_Find("renamed [x] é.cpp:1:"), ==, -1);
					DMibExpect(Initial.f_Find("deleted.cpp:1:"), ==, -1);

					Repo.f_Write("renamed [x] é.cpp", fg_Repeat("x", 200) + "\n" + fg_Repeat("short\n", 19) + fg_Repeat("y", 191) + "\n");
					co_await Repo.f_Commit();
					auto Changed = co_await Repo.f_Validate("Changed", 1, false, "origin/master");

					DMibExpect(Changed.f_Find("renamed [x] é.cpp:21:"), >=, 0);
					DMibExpect(Changed.f_Find("renamed [x] é.cpp:1:"), ==, -1);

					co_return {};
				};

				DMibTestCategory("StagedAttributeSnapshot") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing staged attribute isolation");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();
					Repo.f_Write(".gitattributes", "file.cpp -diff\n");
					Repo.f_Write(".git/info/attributes", "file.cpp -diff\n");
					CFile::fs_WriteStringToFile(Repo.m_Root / "global.attributes", "file.cpp -diff\n", false);
					co_await Repo.f_Git({"config", "--global", "core.attributesFile", Repo.m_Root / "global.attributes"});

					co_await Repo.f_Validate("IgnoreAmbientRules", 1);
					co_await Repo.f_Stage();
					Repo.f_Write(".gitattributes", "file.cpp diff\n");
					Repo.f_Write(".git/info/attributes", "file.cpp diff\n");
					CFile::fs_WriteStringToFile(Repo.m_Root / "global.attributes", "file.cpp diff\n", false);

					co_await Repo.f_Validate("HonorStagedBinaryRule");

					co_return {};
				};

				DMibTestCategory("IntentToAdd") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing intent-to-add entries");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Git({"add", "--intent-to-add", "file.cpp"});

					auto Output = co_await Repo.f_Validate("UnstagedContent");

					DMibExpect(Output.f_Find("Validated 0 staged file(s)"), >=, 0);

					co_return {};
				};

				DMibTestCategory("Sha256AndSplitIndex") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing alternate object format and split index");
					CRepositoryFixture Repo;
					Repo.m_Path = Repo.m_Root / "repo é [x]";
					CFile::fs_CreateDirectory(Repo.m_Path);
					co_await Repo.f_Git({"init", "-q", "-b", "main", "--object-format=sha256"});
					co_await Repo.f_Init();
					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();
					co_await Repo.f_Git({"update-index", "--split-index"});
					auto IndexBefore = CFile::fs_ReadFile(Repo.m_Path / ".git/index");

					co_await Repo.f_Validate("Staged", 1);
					auto IndexAfter = CFile::fs_ReadFile(Repo.m_Path / ".git/index");
					DMibExpect(IndexAfter, ==, IndexBefore);

					co_await Repo.f_Commit();
					co_await Repo.f_Validate("Base", 1, false, "origin/master");

					co_return {};
				};

				DMibTestCategory("BaseCommittedAttributes") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing committed attributes");
					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					Repo.f_Write(".gitattributes", "binary.cpp -diff\n*.dat diff\n");
					Repo.f_Write("nested/.gitattributes", "file.cpp -diff\n");
					for (auto Name : {"text.dat", "binary.cpp", "nested/file.cpp"})
						Repo.f_Write(Name, "short\n");
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"update-ref", "refs/remotes/origin/master", "HEAD"});
					for (auto Name : {"text.dat", "binary.cpp", "nested/file.cpp"})
						Repo.f_Write(Name, fg_Repeat("x", 191));
					Repo.f_WriteBinary("binary.bin");
					co_await Repo.f_Commit();
					Repo.f_Write(".gitattributes", "text.dat -diff\nbinary.cpp diff\n");
					Repo.f_Write("nested/.gitattributes", "file.cpp diff\n");
					co_await Repo.f_Stage();
					CStr AmbientAttributes = "text.dat -diff\nbinary.cpp diff\nnested/file.cpp diff\n";
					Repo.f_Write(".git/info/attributes", AmbientAttributes);
					CFile::fs_WriteStringToFile(Repo.m_Root / "global.attributes", AmbientAttributes, false);
					co_await Repo.f_Git({"config", "--global", "core.attributesFile", Repo.m_Root / "global.attributes"});

					auto Output = co_await Repo.f_Validate("Base", 1, false, "origin/master");

					DMibExpect(Output.f_Find("text.dat:1: line length 191"), >=, 0);
					DMibExpect(Output.f_Find("binary.cpp:1:"), ==, -1);
					DMibExpect(Output.f_Find("nested/file.cpp:1:"), ==, -1);
					DMibExpect(Output.f_Find("binary.bin:1:"), ==, -1);

					Repo.f_Write(".editorconfig", "[*]\nmax_line_length = broken\n");
					co_await Repo.f_Commit();
					auto Invalid = co_await Repo.f_Tool({"Validate", "-C", Repo.m_Path, "--base", "origin/master", "--no-color"});

					DMibExpect(Invalid.m_ExitCode, !=, 0);
					DMibExpect(Invalid.f_GetCombinedOut().f_Find("max_line_length"), >=, 0);
					DMibExpect(Repo.f_HasTemporaryAttributes(), ==, false);

					co_return {};
				};

				DMibTestCategory("InvalidOptionsAndReferences") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "InvalidOptionsAndReferences");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					auto Unborn = co_await Repo.f_Tool({"Validate", "-C", Repo.m_Path, "--base", "HEAD", "--no-color"});

					DMibExpect(Unborn.m_ExitCode, !=, 0);
					DMibExpect(Unborn.f_GetCombinedOut().f_Find("Cannot resolve validation reference 'HEAD'"), >=, 0);

					co_await Repo.f_Commit();
					co_await Repo.f_Validate("EmptyComparison", 0, false, "HEAD");
					auto Unrelated = (co_await Repo.f_Git({"commit-tree", "HEAD^{tree}", "-m", "unrelated"})).f_Trim();
					TCVector<TCVector<CStr>> Cases = {{"--base", "missing-reference"}, {"--base", Unrelated}, {"--base", "HEAD", "--staged"}, {"--base", ""}};
					TCVector<CStr> Messages = {"Cannot resolve validation reference", "no common ancestor", "cannot be combined", "nonempty commit reference"};
					for (umint i = 0; i < Cases.f_GetLen(); ++i)
					{
						DMibTestPath(Messages[i]);
						TCVector<CStr> Params = {"Validate", "-C", Repo.m_Path, "--no-color"};
						Params.f_Insert(Cases[i]);
						auto Result = co_await Repo.f_Tool(fg_Move(Params));

						DMibExpect(Result.m_ExitCode, !=, 0);
						DMibExpect(Result.f_GetCombinedOut().f_Find(Messages[i]), >=, 0);
					}

					Repo.f_Write(".editorconfig", "[*]\nmax_line_length = broken\n");
					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Stage();
					auto Invalid = co_await Repo.f_Tool({"Validate", "-C", Repo.m_Path, "--staged", "--no-color"});

					DMibExpect(Invalid.m_ExitCode, !=, 0);

					co_return {};
				};

				DMibTestCategory("CommitHook") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "CommitHook");

					CRepositoryFixture Repo;
					co_await Repo.f_Init();
					TCVector<CStr> Command = {Repo.m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension), "Validate", "--staged", "--no-color"};
					Repo.f_Write(".git/hooks/pre-commit", "#!/bin/bash\nexec " + CProcessLaunchParams::fs_GetParamsBash(Command) + "\n");
					CFile::fs_SetAttributes(Repo.m_Path / ".git/hooks/pre-commit", CFile::fs_GetAttributes(Repo.m_Path / ".git/hooks/pre-commit") | EFileAttrib_Executable);
					co_await Repo.f_Git({"config", "core.hooksPath", Repo.m_Path / ".git/hooks"});
					Repo.f_Write("file.cpp", fg_Repeat("x", 191));
					co_await Repo.f_Stage();

					auto Rejected = co_await Repo.f_Run("git", {"commit", "-qm", "rejected"}, Repo.m_Path);

					DMibExpect(Rejected.m_ExitCode, !=, 0);
					DMibExpect(Rejected.f_GetCombinedOut().f_Find("file.cpp:1:"), >=, 0);

					Repo.f_Write("file.cpp", "short\n");
					co_await Repo.f_Stage();
					co_await Repo.f_Git({"commit", "-qm", "accepted"});

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CValidation_Tests, Malterlib::Tool);
}
