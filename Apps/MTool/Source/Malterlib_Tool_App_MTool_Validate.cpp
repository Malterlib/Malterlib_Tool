// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Validate.h"

#include <Mib/Git/Helpers/Launch>
#include <Mib/Develop/EditorConfig>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Time/Stopwatch>

namespace
{
	using namespace NMib::NDevelop;
	using namespace NMib::NTool::NFormat;
	using namespace NMib::NTool::NValidate;
}

namespace NMib::NTool::NValidate
{
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


	bool fg_ValidateLine(CStr const &_Line, CStr const &_AbsolutePath, umint _LineNumber, CCodeFormattingSettings const &_Settings, CFormatSink &_Sink)
	{
		CStr Report;
		bool bValid = fg_ValidateLineLength(_Line, _AbsolutePath, _LineNumber, _Settings, Report);
		if (Report)
			_Sink.m_fReport(Report);

		return bValid;
	}

	umint CValidationCounts::f_GetErrors() const
	{
		return m_nLineErrors + m_nFormatErrors;
	}

	CValidationCounts &CValidationCounts::operator += (CValidationCounts const &_Other)
	{
		m_nFiles += _Other.m_nFiles;
		m_nExcluded += _Other.m_nExcluded;
		m_nLineErrors += _Other.m_nLineErrors;
		m_nFormatFiles += _Other.m_nFormatFiles;
		m_nFormatErrors += _Other.m_nFormatErrors;
		m_nFormatFailed += _Other.m_nFormatFailed;

		return *this;
	}

	CStr fg_DescribeValidationFailure(CStr const &_Kind, CValidationCounts const &_Counts)
	{
		auto nErrors = _Counts.f_GetErrors();
		if (!nErrors || _Kind == "text")
			return {};

		return "Commit validation failed: {} changed line(s) violate the configured rules.\n"_f << nErrors;
	}

	// Line-length-only repositories keep the original summary so existing output stays compatible.
	CStr fg_DescribeValidationSummary(CStr const &_Kind, CValidationCounts const &_Counts, fp64 _Seconds)
	{
		if (!_Counts.m_nFormatFiles)
		{
			return "Validated {} {} file(s): {} line length violation(s). Excluded {} file(s). Time: {fe2} s.\n"_f
				<< _Counts.m_nFiles
				<< _Kind
				<< _Counts.m_nLineErrors
				<< _Counts.m_nExcluded
				<< _Seconds
			;
		}

		return "Validated {} {} file(s): {} line length violation(s), {} formatting violation(s) in {} formatted file(s), {} unanalyzable. Excluded {} file(s). Time: {fe2} s.\n"_f
			<< _Counts.m_nFiles
			<< _Kind
			<< _Counts.m_nLineErrors
			<< _Counts.m_nFormatErrors
			<< _Counts.m_nFormatFiles
			<< _Counts.m_nFormatFailed
			<< _Counts.m_nExcluded
			<< _Seconds
		;
	}

	bool fg_HasNul(CStr const &_Text)
	{
		auto pStart = _Text.f_GetStr();
		for (umint i = 0; i < umint(_Text.f_GetLen()); ++i)
		{
			if (!pStart[i])
				return true;
		}

		return false;
	}

	// Validation never writes, so an opted-in file is analyzed as a whole snapshot and
	// reported through the shared engine instead of a second set of layout checks.
	bool fg_UsesFormattingEngine(CCodeFormattingSettings const &_Settings)
	{
		return _Settings.f_IsFormattingEnabled();
	}

	TCFuture<CStr> fg_ResolveValidationCommit(CStr _Reference, CStr _Directory)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Resolving validation reference '{}'"_f << _Reference));

		auto Result = co_await NGit::fg_LaunchGitWithResult({"rev-parse", "--verify", "--end-of-options", _Reference + "^{commit}"}, _Directory);
		if (Result.m_ExitCode)
			co_return DMibErrorInstance("Cannot resolve validation reference '{}': {}"_f << _Reference << Result.f_GetStdErr().f_Trim());

		co_return Result.f_GetStdOut().f_Trim();
	}

	TCFuture<CValidationResult> fg_ValidateChanges(CStr _Directory, CStr _Base, TCActor<CFormatWorkerPool> _Workers, CFormatSink _Sink)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Validating changes in '{}'"_f << _Directory));

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
		CStr SnapshotHead = Head ? Head : bHasHead ? Info.f_GetLast()
			: CStr("ref: refs/heads/validation");
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
			co_return CValidationResult{Kind, {}};

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

		CValidationCounts Counts;
		TCVector<CFormatJob> FormatJobs;
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

			CCodeFormattingSettings Settings(Properties);
			bool bFormat = fg_UsesFormattingEngine(Settings);
			if (!Settings.m_nMaxColumns && !bFormat)
			{
				++Counts.m_nExcluded;
				continue;
			}

			++Counts.m_nFiles;
			// Every parallel job needs its own patch file, so the path carries the file index.
			auto PatchPath = AttributeDirectory / ("patch-{}"_f << Counts.m_nFiles);
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
			// Formatting analyses the exact snapshot bytes, so a file needing NUL normalization
			// is reported as unanalyzable instead of being rewritten into something parseable.
			bool bNormalizedNuls = fg_HasNul(Diff);
			fg_NormalizeValidationNuls(Diff);

			CStr AbsolutePath = _Directory / Path;
			CStr Snapshot;
			TCVector<umint> AddedLines;
			bool bWholeFile = true;
			umint iLine = 0;
			// Git patch records are LF-delimited; a bare CR belongs to the file content.
			for (auto const &Line : Diff.f_Split("\n"))
			{
				if (Line.f_StartsWith("diff --git "))
				{
					iLine = 0;
					Snapshot.f_Clear();
					AddedLines.f_Clear();
				}
				else if (Line.f_StartsWith("@@ "))
				{
					auto Range = Line.f_Extract(Line.f_Find(" +") + 2);
					auto Number = fg_GetStrSeparators(Range, " ,");
					iLine = Number == "0" ? 0 : fg_ParsePositive(Number, "diff line number");
					// Maximal context produces one hunk covering the file; anything else
					// would leave gaps that cannot be reassembled into a complete snapshot.
					bWholeFile &= iLine == 1 && Snapshot.f_IsEmpty();
				}
				else if (Line.f_StartsWith("\\ "))
					Snapshot = Snapshot.f_RemoveSuffix("\n");
				else if (iLine && (Line.f_StartsWith("+") || Line.f_StartsWith(" ")))
				{
					auto Record = Line.f_Extract(1);
					Snapshot += Record;
					Snapshot += "\n";
					auto SourceRecord = Record.f_RemoveSuffix("\r");
					if (iLine == 1)
						SourceRecord = SourceRecord.f_RemovePrefix("\xEF\xBB\xBF");

					for (auto const &SourceLine : SourceRecord.f_SplitLine())
					{
						if (Line.f_StartsWith("+"))
						{
							AddedLines.f_Insert(iLine);
							if (!bFormat)
								Counts.m_nLineErrors += !fg_ValidateLine(SourceLine, AbsolutePath, iLine, Settings, _Sink);
						}

						++iLine;
					}
				}
			}

			// A change with no added or modified lines has nothing to report, which also
			// covers deletion-only hunks and a file emptied by the change.
			if (!bFormat || AddedLines.f_IsEmpty())
				continue;

			++Counts.m_nFormatFiles;
			if (!bWholeFile || bNormalizedNuls)
			{
				++Counts.m_nFormatFailed;
				_Sink.m_fReport("{}: the changed snapshot could not be reassembled for formatting analysis\n"_f << AbsolutePath);

				continue;
			}

			auto &Job = FormatJobs.f_Insert();
			Job.m_Path = AbsolutePath;
			Job.m_DisplayPath = Path;
			Job.m_Settings = Settings;
			Job.m_Source = fg_Move(Snapshot);
			Job.m_bHasSource = true;
			Job.m_ReportedLines = fg_Move(AddedLines);
			Job.m_Mode = EFormatMode::mc_Report;
		}

		for (auto const &Result : co_await fg_RunFormatJobs(_Workers, fg_Move(FormatJobs)))
		{
			if (Result.m_Report)
				_Sink.m_fReport(Result.m_Report);

			Counts.m_nFormatErrors += Result.m_nReported;
			Counts.m_nFormatFailed += Result.m_Outcome == EFormatOutcome::mc_Failed;
		}

		co_return CValidationResult{Kind, Counts};
	}

	TCFuture<CValidationResult> fg_ValidateRepositories(TCVector<CStr> _Directories, CFormatSink _Sink)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Auditing repositories");

		TCVector<CFormatSelection> Selections;
		for (auto const &Directory : _Directories)
		{
			auto Full = CFile::fs_GetFullPath(Directory, CFile::fs_GetCurrentDirectory());
			auto Root = NGit::fg_FindGitWorkingTreeRoot(Full);
			auto &Selection = Selections.f_Insert();
			Selection.m_WorkingDirectory = Root ? Root : Full;
			Selection.m_Patterns = {"*"};
			Selection.m_bRecursive = true;
		}

		CFormatOptions Options;
		Options.m_Mode = EFormatMode::mc_Report;
		Options.m_nJobs = fg_GetDefaultFormatJobs();
		Options.m_bValidateLineLength = true;
		Options.m_bRequireFiles = false;
		auto Run = co_await fg_RunFormat(fg_Move(Selections), fg_Move(Options), fg_Move(_Sink));

		auto const &Summary = Run.m_Summary;
		CValidationCounts Counts;
		Counts.m_nFiles = Summary.m_nSelected - Summary.m_nExcluded;
		Counts.m_nExcluded = Summary.m_nExcluded;
		Counts.m_nLineErrors = Summary.m_nLineErrors;
		Counts.m_nFormatFiles = Summary.m_nFormatFiles;
		Counts.m_nFormatErrors = Summary.m_nReported;
		Counts.m_nFormatFailed = Summary.m_nFailed;

		co_return CValidationResult{"text", Counts};
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
		)
		override
	{
		if (fg_IsMalterlib() || fg_IsCMake() || fg_IsLibTool())
			return;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["Validate"]
					, "Description"_o= "Validate the text files of a repository using .editorconfig: max_line_length, and the formatting rules of files that opt in.\n"
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

					CStopwatch Stopwatch{true};
					CStr Base;
					if (auto pBase = _Params.f_GetMember("Base"))
					{
						if (_Params["Staged"].f_Boolean())
							co_return DMibErrorInstance("--base and --staged cannot be combined");
						if (pBase->f_String().f_IsEmpty())
							co_return DMibErrorInstance("--base requires a nonempty commit reference");

						Base = pBase->f_String();
					}

					CFormatSink Sink;
					Sink.m_fReport = [_pCommandLine](CStr const &_Text)
						{
							*_pCommandLine %= _Text;
						}
					;
					auto Directory = _Params["WorkingDirectory"].f_String();
					CValidationResult Result;
					if (Base || _Params["Staged"].f_Boolean())
					{
						auto Workers = fg_ConstructFormatWorkerPool(fg_GetDefaultFormatJobs());
						auto DestroyWorkers = co_await fg_AsyncDestroy(Workers);
						Result = co_await fg_ValidateChanges(Directory, Base, Workers, fg_Move(Sink));
					}
					else
						Result = co_await fg_ValidateRepositories({Directory}, fg_Move(Sink));
					*_pCommandLine %= fg_DescribeValidationFailure(Result.m_Kind, Result.m_Counts);
					*_pCommandLine %= fg_DescribeValidationSummary(Result.m_Kind, Result.m_Counts, Stopwatch.f_GetTime());

					co_return Result.m_Counts.f_GetErrors() || Result.m_Counts.m_nFormatFailed ? 1 : 0;
				}
			)
		;
	}
};

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_Validate);
