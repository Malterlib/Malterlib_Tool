// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Git/Helpers/Launch>
#include <Mib/Develop/EditorConfig>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Time/Stopwatch>

namespace
{
	void fg_NormalizeValidationNuls(CStr &_Text)
	{
		// NUL occupies one column, just like space. Normalize it before string
		// splitting, whose substring constructors treat a leading NUL as empty.
		auto pStart = _Text.f_GetStrUniqueWritable();
		auto pEnd = pStart + _Text.f_GetLen();
		for (auto pParse = pStart; pParse != pEnd; ++pParse)
		{
			if (*pParse == 0)
				*pParse = ' ';
		}
	}

	CStr fg_UnquoteGitPath(CStr const &_Path)
	{
		if (!_Path.f_StartsWith("\""))
			return _Path;

		CStr Path;
		auto pParse = _Path.f_GetStr() + 1;
		while (*pParse && *pParse != '"')
		{
			auto Char = *pParse++;
			if (Char == '\\')
			{
				Char = *pParse++;
				if (Char >= '0' && Char <= '7')
				{
					uint32 Value = Char - '0';
					for (umint i = 1; i < 3; ++i)
					{
						if (*pParse < '0' || *pParse > '7')
							DError("Invalid octal escape in Git path");

						Value = Value * 8 + *pParse++ - '0';
					}

					if (Value == 0 || Value > 255)
						DError("Invalid byte in Git path");

					Char = ch8(Value);
				}
				else
				{
					switch (Char)
					{
						case 'a':
							Char = '\a';

							break;
						case 'b':
							Char = '\b';

							break;
						case 't':
							Char = '\t';

							break;
						case 'n':
							Char = '\n';

							break;
						case 'v':
							Char = '\v';

							break;
						case 'f':
							Char = '\f';

							break;
						case 'r':
							Char = '\r';

							break;
						case '\\':
						case '"':
							break;
						default: DError("Invalid escape in Git path");
					}
				}
			}

			Path.f_AddChar(Char);
		}

		if (*pParse != '"' || *(pParse + 1))
			DError("Invalid quoted Git path");

		return Path;
	}

	TCVector<CStr> fg_ParseRawDiff(CStr const &_Text)
	{
		// Process output is text: use Git's quoted paths rather than NUL separators,
		// which can be discarded by text buffering at a process-output chunk boundary.
		TCVector<CStr> Records;
		for (auto const &Line : _Text.f_SplitLine<true>())
		{
			auto Fields = Line.f_Split("\t");
			if (Fields.f_GetLen() < 2 || Fields.f_GetLen() > 3)
				DError("Invalid raw Git diff record");

			Records.f_Insert(Fields[0]);
			for (umint i = 1; i < Fields.f_GetLen(); ++i)
				Records.f_Insert(fg_UnquoteGitPath(Fields[i]));
		}

		return Records;
	}

	umint fg_ParsePositive(CStr const &_Value, CStr const &_Name)
	{
		umint Value = 0;
		for (auto pParse = _Value.f_GetStr(); *pParse; ++pParse)
		{
			auto Char = *pParse;
			if (Char < '0' || Char > '9' || Value > (TCLimitsInt<umint>::mc_Max - (Char - '0')) / 10)
				DError("Invalid {}: '{}'"_f << _Name << _Value);

			Value = Value * 10 + Char - '0';
		}

		if (!Value)
			DError("Invalid {}: '{}' (expected a positive integer)"_f << _Name << _Value);

		return Value;
	}


	struct CLineValidationSettings
	{
		explicit CLineValidationSettings(TCMap<CStr, CStr> const &_Properties)
		{
			if (auto pValue = _Properties.f_FindEqual("max_line_length"))
			{
				auto Value = pValue->f_LowerCase();
				if (Value != "off" && Value != "unset")
					m_nMaxColumns = fg_ParsePositive(Value, "max_line_length");
			}

			auto pWidth = _Properties.f_FindEqual("tab_width");
			if (!pWidth || *pWidth == "unset")
				pWidth = _Properties.f_FindEqual("indent_size");

			if (pWidth && *pWidth != "unset" && *pWidth != "tab")
				m_nTabWidth = fg_ParsePositive(*pWidth, "tab_width");
		}

		umint m_nMaxColumns = 0;
		umint m_nTabWidth = 4;
	};

	bool fg_ValidateLine(CStr const &_Line, CStr const &_AbsolutePath, umint _LineNumber, CLineValidationSettings const &_Settings, CCommandLineControl &_CommandLine)
	{
		if (!_Settings.m_nMaxColumns)
			return true;

		// The iterator's character value may be NUL before the end of its input.
		// File-leading BOM removal is handled by the caller.
		umint nColumns = 0;
		umint iPrevious = 0;
		if (_Line.f_GetLen() > iPrevious)
		{
			CStrIteratorUTF8 End(_Line.f_GetStr() + _Line.f_GetLen(), 0);
			for (CStrIteratorUTF8 Iterator(_Line); ; ++Iterator)
			{
				auto iEnd = _Line.f_GetLen() - umint(End - Iterator);
				umint nAdvance = 1;
				if (Iterator.f_IsBroken() || !Iterator.f_IsWholeCodePoint())
					nAdvance = iEnd - iPrevious;
				else if (*Iterator == '\t')
					nAdvance = _Settings.m_nTabWidth - nColumns % _Settings.m_nTabWidth;

				if (nColumns > TCLimitsInt<umint>::mc_Max - nAdvance)
				{
					_CommandLine %= "{}:{}: line length overflows the column counter and exceeds max_line_length = {}\n"_f
						<< _AbsolutePath << _LineNumber << _Settings.m_nMaxColumns
					;

					return false;
				}

				nColumns += nAdvance;
				iPrevious = iEnd;

				if (Iterator - End >= 0)
					break;
			}
		}

		if (nColumns <= _Settings.m_nMaxColumns)
			return true;

		_CommandLine %= "{}:{}: line length {} exceeds max_line_length = {}\n"_f << _AbsolutePath << _LineNumber << nColumns << _Settings.m_nMaxColumns;

		return false;
	}

	void fg_OutputValidationSummary(CCommandLineControl &_CommandLine, CStr const &_Kind, umint _nFiles, umint _nExcluded, umint _nErrors, fp64 _Seconds)
	{
		_CommandLine %= "Validated {} {} file(s): {} line length violation(s). Excluded {} file(s). Time: {fe2} s.\n"_f
			<< _nFiles << _Kind << _nErrors << _nExcluded << _Seconds
		;
	}

	TCFuture<CStr> fg_ResolveValidationCommit(CStr _Reference, CStr _Directory)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Resolving validation reference '{}'"_f << _Reference));

		auto Result = co_await NGit::fg_LaunchGitWithResult({"rev-parse", "--verify", "--end-of-options", _Reference + "^{commit}"}, _Directory);
		if (Result.m_ExitCode)
			co_return DMibErrorInstance("Cannot resolve validation reference '{}': {}"_f << _Reference << Result.f_GetStdErr().f_Trim());

		co_return Result.f_GetStdOut().f_Trim();
	}

	TCFuture<uint32> fg_ValidateChanges(CStr _Directory, CStr _Base, TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Validating changes in '{}'"_f << _Directory));

		CStopwatch Stopwatch{true};
		auto RepositoryInfo = co_await NGit::fg_LaunchGitWithResult
			(
				{
					"rev-parse", "--show-toplevel", "--absolute-git-dir", "--show-object-format", "--path-format=absolute"
					, "--git-path", "objects", "--git-path", "index", "--shared-index-path", "--verify", "--quiet", "HEAD^{commit}"
				}
				, _Directory
			)
		;
		auto Info = RepositoryInfo.f_GetStdOut().f_SplitLine<true>();
		bool bHasHead = RepositoryInfo.m_ExitCode == 0;
		if (RepositoryInfo.m_ExitCode > 1 || Info.f_GetLen() < 5 + umint(bHasHead))
			co_return DMibErrorInstance("Cannot read validation repository metadata: {}"_f << RepositoryInfo.f_GetStdErr());

		_Directory = Info[0];
		CStr Head;
		TCVector<CStr> Comparison = {"--cached"};
		if (_Base)
		{
			if (!bHasHead)
				co_return DMibErrorInstance("Cannot resolve validation reference 'HEAD': HEAD does not name a commit");

			Head = Info.f_GetLast();
			auto Base = co_await fg_ResolveValidationCommit(_Base, _Directory);
			auto Result = co_await NGit::fg_LaunchGitWithResult({"merge-base", "--all", Base, Head}, _Directory);
			if (Result.m_ExitCode == 1)
				co_return DMibErrorInstance("Validation base '{}' has no common ancestor with HEAD"_f << _Base);
			if (Result.m_ExitCode)
				co_return DMibErrorInstance("Cannot find merge base for '{}': {}"_f << _Base << Result.f_GetStdErr().f_Trim());

			auto MergeBases = Result.f_GetStdOut().f_SplitLine<true>();
			if (MergeBases.f_GetLen() != 1)
				co_return DMibErrorInstance("Validation base '{}' does not have a unique merge base with HEAD; specify a more precise base commit"_f << _Base);

			Comparison = {MergeBases[0], Head};
		}

		CStr Kind = Head ? "committed" : "staged";
		CSystemEnvironment DiffEnvironment;

		auto GitDirectory = Info[1];
		auto AttributeDirectory = GitDirectory / "MToolValidate";
		auto DiffDirectory = AttributeDirectory / "worktree";
		auto SnapshotGitDirectory = AttributeDirectory / "git";
		TCUniquePointer<CLockFile> pAttributeLock;
		{
			auto BlockingActor = fg_BlockingActor();
			pAttributeLock = co_await
				(
					g_Dispatch(BlockingActor) / [GitDirectory]()
					{
						TCUniquePointer<CLockFile> pLock = fg_Construct(GitDirectory / "MToolValidate.lock");
						pLock->f_LockWithException(5.0 * 60.0);

						return fg_Move(pLock);
					}
				)
			;
		}

		auto CleanupAttributes = co_await fg_AsyncDestroy
			(
				[AttributeDirectory, pAttributeLock = fg_Move(pAttributeLock)]() mutable -> TCFuture<void>
				{
					auto CaptureScope = co_await (g_CaptureExceptions % "Removing temporary validation attributes");
					if (AttributeDirectory && CFile::fs_FileExists(AttributeDirectory))
						CFile::fs_DeleteDirectoryRecursive(AttributeDirectory);
					pAttributeLock.f_Clear();

					co_return {};
				}
			)
		;
		// Isolate the Git directory as well as the worktree: info/attributes takes
		// precedence over snapshot attributes, even when GIT_ATTR_SOURCE is supported.
		if (CFile::fs_FileExists(AttributeDirectory))
			CFile::fs_DeleteDirectoryRecursive(AttributeDirectory);

		CFile::fs_CreateDirectory(DiffDirectory);
		CFile::fs_CreateDirectory(SnapshotGitDirectory / "objects");
		CFile::fs_CreateDirectory(SnapshotGitDirectory / "refs");
		CStr SnapshotHead = Head ? Head : bHasHead ? Info.f_GetLast() : CStr("ref: refs/heads/validation");
		CFile::fs_WriteStringToFile(SnapshotGitDirectory / "HEAD", SnapshotHead + "\n", false);
		CFile::fs_WriteStringToFile
			(
				SnapshotGitDirectory / "config"
				, "[core]\nrepositoryformatversion = 1\nbare = false\nfsmonitor = false\n[extensions]\nobjectformat = " + Info[2] + "\n"
				, false
			)
		;
		CFile::fs_WriteStringToFile(AttributeDirectory / "attributes", "", false);
		DiffEnvironment["GIT_DIR"] = SnapshotGitDirectory;
		DiffEnvironment["GIT_COMMON_DIR"] = SnapshotGitDirectory;
		DiffEnvironment["GIT_WORK_TREE"] = DiffDirectory;
		DiffEnvironment["GIT_INDEX_FILE"] = SnapshotGitDirectory / "index";
		DiffEnvironment["GIT_OBJECT_DIRECTORY"] = SnapshotGitDirectory / "objects";
		DiffEnvironment["GIT_ATTR_NOSYSTEM"] = "1";
		DiffEnvironment["GIT_CONFIG_GLOBAL"] = AttributeDirectory / "attributes";
		DiffEnvironment["GIT_CONFIG_SYSTEM"] = AttributeDirectory / "attributes";
		DiffEnvironment["GIT_CONFIG_NOSYSTEM"] = "1";
		DiffEnvironment["GIT_CONFIG_COUNT"] = "0";
		DiffEnvironment["GIT_CONFIG_PARAMETERS"] = "";
		auto Alternates = CEJsonSorted(Info[3]).f_ToString();
		auto InheritedAlternates = fg_GetSys()->f_GetEnvironmentVariable("GIT_ALTERNATE_OBJECT_DIRECTORIES");
		if (InheritedAlternates)
		{
#ifdef DPlatformFamily_Windows
			Alternates += ";";
#else
			Alternates += ":";
#endif
			Alternates += InheritedAlternates;
		}
		DiffEnvironment["GIT_ALTERNATE_OBJECT_DIRECTORIES"] = fg_Move(Alternates);
		if (!Head && CFile::fs_FileExists(Info[4]))
		{
			CFile::fs_WriteFile(SnapshotGitDirectory / "index", CFile::fs_ReadFile(Info[4]));
			if (Info.f_GetLen() > 5 + umint(bHasHead))
				CFile::fs_WriteFile(SnapshotGitDirectory / CFile::fs_GetFile(Info[5]), CFile::fs_ReadFile(Info[5]));
		}

		// Read configuration from the selected snapshot: HEAD for --base, or the
		// committing index (including GIT_INDEX_FILE) for --staged.
		TCMap<CStr, CStr> ConfigurationContents;
		TCVector<CStr> ListParams = {"-c", "core.quotePath=true"};
		if (Head)
			ListParams.f_Insert({"ls-tree", "-r", "--full-tree", Head});
		else
			ListParams.f_Insert({"ls-files", "--stage"});

		auto Snapshot = co_await NGit::fg_LaunchGit(fg_Move(ListParams), DiffDirectory, DiffEnvironment);
		for (auto const &Entry : Snapshot.f_SplitLine<true>())
		{
			auto iTab = Entry.f_Find("\t");
			auto Header = Entry.f_Left(iTab);
			auto Path = fg_UnquoteGitPath(Entry.f_Extract(iTab + 1));
			auto Mode = fg_GetStrSep(Header, " ");
			if (Head)
				fg_GetStrSep(Header, " "); // ls-tree's object type precedes the hash.

			auto Hash = fg_GetStrSep(Header, " ");
			if (!Head && Header != "0")
				co_return DMibErrorInstance("Cannot validate an unmerged index: {}"_f << (_Directory / Path));

			bool bAttributes = CFile::fs_GetFile(Path) == ".gitattributes";
			if (CFile::fs_GetFile(Path) != ".editorconfig" && !bAttributes)
				continue;

			if (Mode != "100644" && Mode != "100755")
			{
				if (bAttributes)
					continue;

				co_return DMibErrorInstance("EditorConfig in the validation snapshot must be a regular file: {}"_f << (_Directory / Path));
			}

			auto Contents = co_await NGit::fg_LaunchGit({"cat-file", "blob", Hash}, DiffDirectory, DiffEnvironment);
			if (bAttributes)
			{
				CFile::fs_CreateDirectoryForFile(DiffDirectory / Path);
				CFile::fs_WriteStringToFile(DiffDirectory / Path, Contents, false);
				continue;
			}

			// Git returns raw bytes; unlike CFile::fs_ReadString*, it does not strip the BOM.
			ConfigurationContents(Path, Contents.f_RemovePrefix("\xEF\xBB\xBF"));
		}

		// The copied index also supplies a tree for modern Git. Older versions use
		// the materialized attribute files. New tree objects stay inside the snapshot.
		auto SnapshotTree = Head ? Head : (co_await NGit::fg_LaunchGit({"write-tree"}, DiffDirectory, DiffEnvironment)).f_Trim();
		DiffEnvironment["GIT_ATTR_SOURCE"] = SnapshotTree;

		TCVector<CStr> RawParams =
			{
				"-c", "core.quotePath=true", "-c", "core.attributesFile=" + (AttributeDirectory / "attributes"), "diff", "--raw", "--no-abbrev", "--find-renames"
				, "--no-color", "--no-ext-diff", "--no-textconv", "--ignore-submodules=all"
			}
		;
		RawParams.f_Insert(Comparison);
		RawParams.f_Insert("--");
		auto Changes = fg_ParseRawDiff(co_await NGit::fg_LaunchGit(fg_Move(RawParams), DiffDirectory, DiffEnvironment));

		if (Changes.f_IsEmpty())
		{
			fg_OutputValidationSummary(*_pCommandLine, Kind, 0, 0, 0, Stopwatch.f_GetTime());

			co_return 0;
		}

		TCActor<NDevelop::CEditorConfigResolver> Configurations = fg_Construct
			(
				g_ActorFunctorWeak / [&ConfigurationContents, &_Directory](CStr _Path) -> TCFuture<TCOptional<CStr>>
				{
					auto Capture = co_await (g_CaptureExceptions % "Loading EditorConfig snapshot contents");
					if (auto pContents = ConfigurationContents.f_FindEqual(CFile::fs_MakePathRelative(_Path, _Directory)))
						co_return *pContents;

					co_return {};
				}
				, _Directory
			)
		;
		auto DestroyConfigurations = co_await fg_AsyncDestroy(Configurations);

		umint nErrors = 0;
		umint nFiles = 0;
		umint nExcluded = 0;
		for (umint i = 0; i < Changes.f_GetLen(); ++i)
		{
			auto Header = Changes[i];
			fg_GetStrSep(Header, " "); // Old mode.
			auto Mode = fg_GetStrSep(Header, " ");
			fg_GetStrSep(Header, " "); // Old hash.
			fg_GetStrSep(Header, " "); // New hash.
			auto OldPath = Changes[++i];
			auto Path = OldPath;
			bool bRenamed = Header.f_StartsWith("R") || Header.f_StartsWith("C");
			if (bRenamed)
				Path = Changes[++i];

			if (Mode != "100644" && Mode != "100755")
				continue;

			auto Properties = co_await Configurations(&NDevelop::CEditorConfigResolver::f_Resolve, _Directory / Path);

			CLineValidationSettings Settings(Properties);
			if (!Settings.m_nMaxColumns)
			{
				++nExcluded;
				continue;
			}

			++nFiles;
			auto PatchPath = AttributeDirectory / "patch";
			TCVector<CStr> DiffParams =
				{
					"-c", "core.attributesFile=" + (AttributeDirectory / "attributes"), "--literal-pathspecs", "diff"
					, "--no-color", "--no-ext-diff", "--no-textconv", "--unified=2147483647"
					, "--inter-hunk-context=0", "--ignore-submodules=all", "--output=" + PatchPath
				}
			;
			// Compare only the renamed file's blobs, retaining their paths for Git attributes.
			// A replacement at the old path cannot affect this patch or rename detection.
			if (bRenamed)
				DiffParams.f_Insert({(Head ? Comparison[0] : SnapshotHead) + ":" + OldPath, SnapshotTree + ":" + Path, "--"});
			else
			{
				DiffParams.f_Insert(Comparison);
				DiffParams.f_Insert({"--no-renames", "--", Path});
			}

			// Full context maps Git's LF-based hunks to source lines, including CR-only
			// endings in unchanged content. Only '+' records are checked. A patch file
			// also preserves NULs that text process-output handling can discard.
			co_await NGit::fg_LaunchGit(fg_Move(DiffParams), DiffDirectory, DiffEnvironment);
			auto Diff = CFile::fs_ReadStringFromFile(PatchPath, true);
			fg_NormalizeValidationNuls(Diff);

			CStr AbsolutePath = _Directory / Path;
			umint iLine = 0;
			// Git patch records are LF-delimited; a bare CR belongs to the file content.
			for (auto const &Line : Diff.f_Split("\n"))
			{
				if (Line.f_StartsWith("diff --git "))
					iLine = 0;
				else if (Line.f_StartsWith("@@ "))
				{
					auto Range = Line.f_Extract(Line.f_Find(" +") + 2);
					auto Number = fg_GetStrSeparators(Range, " ,");
					iLine = Number == "0" ? 0 : fg_ParsePositive(Number, "diff line number");
				}
				else if (iLine && (Line.f_StartsWith("+") || Line.f_StartsWith(" ")))
				{
					auto SourceRecord = Line.f_Extract(1).f_RemoveSuffix("\r");
					if (iLine == 1)
						SourceRecord = SourceRecord.f_RemovePrefix("\xEF\xBB\xBF");

					for (auto const &SourceLine : SourceRecord.f_SplitLine())
					{
						if (Line.f_StartsWith("+"))
							nErrors += !fg_ValidateLine(SourceLine, AbsolutePath, iLine, Settings, *_pCommandLine);
						++iLine;
					}
				}
			}
		}

		if (nErrors)
			*_pCommandLine %= "Commit validation failed: {} changed line(s) exceed the configured limit.\n"_f << nErrors;

		fg_OutputValidationSummary(*_pCommandLine, Kind, nFiles, nExcluded, nErrors, Stopwatch.f_GetTime());

		co_return nErrors ? 1 : 0;
	}

	TCFuture<uint32> fg_ValidateRepository(CStr _Directory, TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Auditing repository '{}'"_f << _Directory));

		CStopwatch Stopwatch{true};
		_Directory = (co_await NGit::fg_LaunchGit({"rev-parse", "--show-toplevel"}, _Directory)).f_Trim();
		// Enumerate names first so Git does not read excluded files to classify their content.
		auto Index = co_await NGit::fg_LaunchGit
			(
				{"-c", "core.quotePath=true", "ls-files", "--cached", "--deduplicate"}
				, _Directory
			)
		;

		TCActor<NDevelop::CEditorConfigResolver> Configurations = fg_Construct(_Directory);
		auto DestroyConfigurations = co_await fg_AsyncDestroy(Configurations);
		TCMap<CStr, CLineValidationSettings> SettingsByPath;
		TCVector<CStr> CandidatePaths;
		umint nExcluded = 0;
		for (auto const &EncodedPath : Index.f_SplitLine<true>())
		{
			auto Path = fg_UnquoteGitPath(EncodedPath);
			auto Properties = co_await Configurations(&NDevelop::CEditorConfigResolver::f_Resolve, _Directory / Path);

			CLineValidationSettings Settings(Properties);
			if (!Settings.m_nMaxColumns)
			{
				++nExcluded;
				continue;
			}

			CandidatePaths.f_Insert(Path);
			SettingsByPath(Path, Settings);
		}

		umint nFiles = 0;
		umint nErrors = 0;
		umint iPath = 0;
		while (iPath < CandidatePaths.f_GetLen())
		{
			TCVector<CStr> Params =
				{
					"--literal-pathspecs", "-c", "core.quotePath=true", "grep", "--no-color", "--no-textconv"
					, "-I", "--full-name", "-l", "-e", "", "--"
				}
			;
			// Bound the escaped argument size, including quotes, for Windows command lines.
			umint nArgumentChars = 0;
			do
			{
				auto const &Path = CandidatePaths[iPath++];
				Params.f_Insert(Path);
				nArgumentChars += Path.f_GetLen() * 2 + 3;
			}
			while (iPath < CandidatePaths.f_GetLen() && nArgumentChars + CandidatePaths[iPath].f_GetLen() * 2 + 3 < 12000)
			;

			// Git selects nonempty, non-binary working-tree files from this filtered set.
			// Exit 1 means no matching files, rather than a launch failure.
			auto Result = co_await NGit::fg_LaunchGitWithResult(fg_Move(Params), _Directory);
			if (Result.m_ExitCode > 1)
				co_return DMibErrorInstance("Cannot select text files for validation: {}"_f << Result.f_GetStdErr());

			for (auto const &EncodedPath : Result.f_GetStdOut().f_SplitLine<true>())
			{
				auto Path = fg_UnquoteGitPath(EncodedPath);
				auto const &Settings = *SettingsByPath.f_FindEqual(Path);
				CStr AbsolutePath = _Directory / Path;
				auto Contents = CFile::fs_ReadStringFromFile(AbsolutePath, true);
				fg_NormalizeValidationNuls(Contents);
				umint iLine = 0;
				for (auto const &Line : Contents.f_SplitLine())
					nErrors += !fg_ValidateLine(Line, AbsolutePath, ++iLine, Settings, *_pCommandLine);

				++nFiles;
			}
		}

		fg_OutputValidationSummary(*_pCommandLine, "tracked text", nFiles, nExcluded, nErrors, Stopwatch.f_GetTime());

		co_return nErrors ? 1 : 0;
	}
}

struct CTool_Validate : CDistributedTool
{
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		) override
	{
		if (fg_IsMalterlib() || fg_IsCMake() || fg_IsLibTool())
			return;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["Validate"]
					, "Description"_o= "Validate tracked text files using .editorconfig. Currently checks max_line_length.\n"
					, "Category"_o= "Validation"
					, "Options"_o=
					{
						"WorkingDirectory?"_o=
						{
							"Names"_o= _o["--working-directory", "-C"]
							, "Default"_o= CFile::fs_GetCurrentDirectory()
							, "Description"_o= "Git repository to validate.\n"
						}
						, "Staged?"_o=
						{
							"Names"_o= _o["--staged"]
							, "Default"_o= false
							, "Description"_o= "Only validate added or modified lines in the index, using staged .editorconfig files.\n"
						}
						, "Base?"_o=
						{
							"Names"_o= _o["--base"]
							, "Type"_o= ""
							, "Description"_o= "Validate committed changes from the merge base with this reference to HEAD, using HEAD's .editorconfig files.\n"
						}
					}
				}
				, [](CEJsonSorted const _Params, TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					auto CaptureScope = co_await (g_CaptureExceptions % "Running Validate");

					if (auto pBase = _Params.f_GetMember("Base"))
					{
						if (_Params["Staged"].f_Boolean())
							co_return DMibErrorInstance("--base and --staged cannot be combined");
						if (pBase->f_String().f_IsEmpty())
							co_return DMibErrorInstance("--base requires a nonempty commit reference");

						co_return co_await fg_ValidateChanges(_Params["WorkingDirectory"].f_String(), pBase->f_String(), fg_Move(_pCommandLine));
					}

					if (_Params["Staged"].f_Boolean())
						co_return co_await fg_ValidateChanges(_Params["WorkingDirectory"].f_String(), {}, fg_Move(_pCommandLine));

					co_return co_await fg_ValidateRepository(_Params["WorkingDirectory"].f_String(), fg_Move(_pCommandLine));
				}
			)
		;
	}
};

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_Validate);
