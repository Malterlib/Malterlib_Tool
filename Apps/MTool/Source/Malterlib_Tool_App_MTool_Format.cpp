// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Format.h"

#include <Mib/Git/Helpers/Launch>
#include <Mib/Git/Ignore>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Container/Map>
#include <Mib/Container/Set>
#include <Mib/Core/OnScopeExitCatch>
#include <Mib/Time/Stopwatch>

// Actor call arguments and results need externally nameable types, so the command's
// helpers live in a named namespace instead of an anonymous one.
namespace NMib::NTool::NFormat
{
	using namespace NMib::NDevelop;

	// File I/O of a run: directory listings, configuration loads, reads, and writes take
	// turns on this many blocking actors, however many workers and walks are in flight.
	umint const gc_nFormatBlockingActors = 8;

	// Counts the lines a reader sees: the synthetic empty line after a final terminator
	// is a position, not content.
	umint fg_GetPrintableLineCount(CTextLineMap const &_Lines, CStr const &_Text)
	{
		if (_Text.f_IsEmpty())
			return 0;

		auto nLines = _Lines.f_GetLineCount();
		auto const &Last = _Lines.f_GetLine(nLines - 1);
		if (nLines > 1 && !Last.m_nLength && Last.m_Ending == ETextLineEnding::mc_None)
			--nLines;

		return nLines;
	}

	void fg_AppendDiffLines(CStr &o_Patch, CStr const &_Marker, CStr const &_Text, CTextLineMap const &_Lines, umint _iFirst, umint _iLast)
	{
		for (umint iLine = _iFirst; iLine <= _iLast; ++iLine)
		{
			auto const &Line = _Lines.f_GetLine(iLine);
			o_Patch += _Marker;
			o_Patch += CStr(_Text.f_GetStr() + Line.m_iOffset, Line.m_nLength);
			o_Patch += "\n";
			if (Line.m_Ending == ETextLineEnding::mc_None)
				o_Patch += "\\ No newline at end of file\n";
		}
	}

	// Builds a unified diff from the edit plan. Each changed region is rewritten by applying
	// the edits that belong to it, so the replacement text is exact rather than reconstructed.
	CStr fg_BuildFormatPatch(CStr const &_Path, CStr const &_Source, TCVector<CCodeFormattingEdit> const &_Edits)
	{
		constexpr umint c_nContext = 3;
		if (_Edits.f_IsEmpty())
			return {};

		CTextLineMap Lines(_Source);
		auto nPrintable = fg_GetPrintableLineCount(Lines, _Source);
		if (!nPrintable)
			return {};

		TCVector<uint8> bTouched;
		bTouched.f_SetLen(nPrintable);
		for (auto &Value : bTouched)
			Value = 0;

		auto fEditLines = [&](CCodeFormattingEdit const &_Edit, umint &o_iFirst, umint &o_iLast)
			{
				o_iFirst = fg_Min(Lines.f_FindLine(_Edit.m_iOffset), nPrintable - 1);
				o_iLast = fg_Min(Lines.f_FindLine(_Edit.f_GetEnd() > _Edit.m_iOffset ? _Edit.f_GetEnd() - 1 : _Edit.m_iOffset), nPrintable - 1);
			}
		;
		for (auto const &Edit : _Edits)
		{
			umint iFirst = 0;
			umint iLast = 0;
			fEditLines(Edit, iFirst, iLast);
			for (umint i = iFirst; i <= iLast; ++i)
				bTouched[i] = 1;
		}

		struct CHunk
		{
			umint m_iFirst = 0;
			umint m_iLast = 0;
		};
		TCVector<CHunk> Hunks;
		for (umint iLine = 0; iLine < nPrintable; ++iLine)
		{
			if (!bTouched[iLine])
				continue;

			auto iFirst = iLine >= c_nContext ? iLine - c_nContext : 0;
			auto iLast = fg_Min(iLine + c_nContext, nPrintable - 1);
			if (!Hunks.f_IsEmpty() && iFirst <= Hunks.f_GetLast().m_iLast + 1)
			{
				Hunks.f_GetLast().m_iLast = fg_Max(Hunks.f_GetLast().m_iLast, iLast);

				continue;
			}

			auto &Hunk = Hunks.f_Insert();
			Hunk.m_iFirst = iFirst;
			Hunk.m_iLast = iLast;
		}

		CStr Patch = "--- a/{}\n+++ b/{}\n"_f << _Path << _Path;
		aint nDelta = 0;
		for (auto const &Hunk : Hunks)
		{
			CStr Body;
			umint nOld = 0;
			umint nNew = 0;
			umint iLine = Hunk.m_iFirst;
			while (iLine <= Hunk.m_iLast)
			{
				if (!bTouched[iLine])
				{
					fg_AppendDiffLines(Body, " ", _Source, Lines, iLine, iLine);
					++nOld;
					++nNew;
					++iLine;

					continue;
				}

				auto iSegmentLast = iLine;
				while (iSegmentLast + 1 <= Hunk.m_iLast && bTouched[iSegmentLast + 1])
					++iSegmentLast;

				auto iStart = Lines.f_GetLineStart(iLine);
				auto iEnd = Lines.f_GetLineEnd(iSegmentLast);
				TCVector<CCodeFormattingEdit> Local;
				for (auto const &Edit : _Edits)
				{
					umint iFirst = 0;
					umint iLast = 0;
					fEditLines(Edit, iFirst, iLast);
					if (iFirst < iLine || iLast > iSegmentLast)
						continue;

					auto Rebased = Edit;
					Rebased.m_iOffset -= iStart;
					Local.f_Insert(Rebased);
				}

				CStr Original(_Source.f_GetStr() + iStart, iEnd - iStart);
				auto Replacement = fg_ApplyCodeFormattingEdits(Original, Local);
				CTextLineMap OriginalLines(Original);
				CTextLineMap ReplacementLines(Replacement);
				auto nOriginal = fg_GetPrintableLineCount(OriginalLines, Original);
				auto nReplacement = fg_GetPrintableLineCount(ReplacementLines, Replacement);
				if (nOriginal)
					fg_AppendDiffLines(Body, "-", Original, OriginalLines, 0, nOriginal - 1);

				if (nReplacement)
					fg_AppendDiffLines(Body, "+", Replacement, ReplacementLines, 0, nReplacement - 1);

				nOld += nOriginal;
				nNew += nReplacement;
				iLine = iSegmentLast + 1;
			}

			Patch += "@@ -{},{} +{},{} @@\n"_f << Hunk.m_iFirst + 1 << nOld << umint(aint(Hunk.m_iFirst) + 1 + nDelta) << nNew;
			Patch += Body;
			nDelta += aint(nNew) - aint(nOld);
		}

		return Patch;
	}

	bool fg_IsReportedLine(TCVector<umint> const &_ReportedLines, umint _iLine)
	{
		if (_ReportedLines.f_IsEmpty())
			return true;

		umint iLow = 0;
		umint iHigh = _ReportedLines.f_GetLen();
		while (iLow < iHigh)
		{
			auto iMiddle = iLow + (iHigh - iLow) / 2;
			if (_ReportedLines[iMiddle] < _iLine)
				iLow = iMiddle + 1;
			else
				iHigh = iMiddle;
		}

		return iLow < _ReportedLines.f_GetLen() && _ReportedLines[iLow] == _iLine;
	}

	// Changed-line reporting masks diagnostics by their original affected span, so unchanged
	// surrounding violations stay suppressed while the analysis still sees the whole file.
	// Writing the file or printing its patch is the report of what was fixed, so only what
	// stays unresolved is listed then.
	void fg_DescribeDiagnostics(CFormatJob const &_Job, TCVector<CCodeFormattingDiagnostic> const &_Diagnostics, CFormatJobResult &o_Result)
	{
		bool bFixing = _Job.m_Mode == EFormatMode::mc_Write || _Job.m_Mode == EFormatMode::mc_Diff;
		for (auto const &Diagnostic : _Diagnostics)
		{
			if (!fg_IsReportedLine(_Job.m_ReportedLines, Diagnostic.m_iLine))
				continue;

			if (bFixing && Diagnostic.m_bHasAutomaticFix)
				continue;

			++o_Result.m_nReported;
			o_Result.m_nUnresolved += !Diagnostic.m_bHasAutomaticFix;
			o_Result.m_Report += "{}:{}:{}: {}: {}\n"_f
				<< _Job.m_Path
				<< Diagnostic.m_iLine
				<< Diagnostic.m_iColumn
				<< Diagnostic.m_Rule
				<< Diagnostic.m_Explanation
			;
		}
	}

	// Every job is queued at once and a job that awaits lets the worker start the next, so a
	// blocking actor checked out per job would mean a thread per file. The run's shared set
	// bounds that to its capacity, whatever the number of workers and resolvers.
	CFormatWorker::CFormatWorker(TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors)
		: mp_pBlockingActors(_pBlockingActors)
	{
	}

	// The resolvers are actors of their own, and their destruction is awaited here so that
	// the worker's own destruction, and the run, do not end before them.
	TCFuture<void> CFormatWorker::fp_Destroy()
	{
		for (auto &Configurations : mp_Configurations)
			co_await fg_Move(Configurations).f_Destroy();

		co_return {};
	}

	TCFuture<CFormatJobResult> CFormatWorker::f_Process(CFormatJob _Job)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Formatting '{}'"_f << _Job.m_Path));

		CFormatJobResult Result;
		Result.m_Path = _Job.m_Path;
		if (_Job.m_bResolveSettings)
		{
			// The opt-in property decides what is formatted, and what it opts in is C++.
			auto pConfigurations = mp_Configurations.f_FindEqual(_Job.m_Root);
			if (!pConfigurations)
			{
				pConfigurations = &mp_Configurations[_Job.m_Root];
				*pConfigurations = fg_Construct(_Job.m_Root, mp_pBlockingActors);
			}

			auto Properties = co_await (*pConfigurations)(&NDevelop::CEditorConfigResolver::f_Resolve, _Job.m_Path);
			_Job.m_Settings = CCodeFormattingSettings(Properties);
			if (!_Job.m_Settings.f_IsFormattingEnabled())
			{
				Result.m_Outcome = EFormatOutcome::mc_Excluded;

				co_return Result;
			}

			_Job.m_DisplayPath = CFile::fs_MakePathRelative(_Job.m_Path, _Job.m_Root);
		}

		auto Snapshot = _Job.m_Source;
		if (!_Job.m_bHasSource)
		{
			Snapshot = co_await
				(
					g_Dispatch(mp_pBlockingActors->f_Next()) / [Path = _Job.m_Path]() -> CStr
					{
						auto Data = CFile::fs_ReadFile(Path);

						return CStr((ch8 const *)Data.f_GetArray(), Data.f_GetLen());
					}
				)
			;
		}

		// The read hands the worker back to the queue it was dispatched from, where every
		// worker would then analyze one after the other. Yielding places the analysis on an
		// idle core instead, and the worker stays there for the files that follow.
		co_await g_Yield;

		CCodeFormattingRequest Request;
		Request.m_Source = Snapshot;
		Request.m_Path = _Job.m_Path;
		Request.m_Language = ECodeLanguage::mc_Cpp;
		Request.m_Settings = _Job.m_Settings;
		Request.m_Ranges = _Job.m_ByteRanges;
		Request.m_RangePolicy = _Job.m_RangePolicy;
		if (_Job.m_iFirstLine)
		{
			CTextLineMap Lines(Snapshot);
			auto nPrintable = fg_GetPrintableLineCount(Lines, Snapshot);
			if (_Job.m_iLastLine > nPrintable)
			{
				Result.m_Outcome = EFormatOutcome::mc_Failed;
				Result.m_Report = "{}: --lines {}:{} is outside the file's {} line(s)\n"_f
					<< _Job.m_Path
					<< _Job.m_iFirstLine
					<< _Job.m_iLastLine
					<< nPrintable
				;

				co_return Result;
			}

			auto &Range = Request.m_Ranges.f_Insert();
			Range.m_iOffset = Lines.f_GetLineStart(_Job.m_iFirstLine - 1);
			Range.m_nLength = Lines.f_GetLineEnd(_Job.m_iLastLine - 1) - Range.m_iOffset;
		}

		auto Analysis = fg_AnalyzeCodeFormatting(Request);
		if (Analysis.m_Status != ECodeFormattingStatus::mc_Complete)
		{
			// Selection already proved the file is opted in, so an unsupported result here
			// means the source itself could not be analyzed safely.
			Result.m_Outcome = EFormatOutcome::mc_Failed;
			Result.m_Report = "{}: {}\n"_f << _Job.m_Path << Analysis.m_Explanation;

			co_return Result;
		}

		fg_DescribeDiagnostics(_Job, Analysis.m_Diagnostics, Result);
		if (Analysis.m_Edits.f_IsEmpty() || (_Job.m_Mode == EFormatMode::mc_Report && !Result.m_nReported))
		{
			Result.m_Outcome = EFormatOutcome::mc_Unchanged;

			co_return Result;
		}

		Result.m_Outcome = EFormatOutcome::mc_Changed;
		if (_Job.m_Mode == EFormatMode::mc_Diff)
		{
			Result.m_Patch = fg_BuildFormatPatch(_Job.m_DisplayPath, Snapshot, Analysis.m_Edits);

			co_return Result;
		}

		if (_Job.m_Mode != EFormatMode::mc_Write)
			co_return Result;

		auto Formatted = fg_ApplyCodeFormattingEdits(Snapshot, Analysis.m_Edits);
		auto Written = co_await
			(
				g_Dispatch(mp_pBlockingActors->f_Next()) / [Path = _Job.m_Path, Snapshot, Formatted]() -> CStr
				{
					// Never overwrite an intervening edit: the file must still be the regular
					// file whose bytes were analyzed.
					if (CFile::fs_GetAttributesOnLink(Path) & (EFileAttrib_Link | EFileAttrib_Directory))
						return "is no longer a regular file";

					auto Current = CFile::fs_ReadFile(Path);
					if (CStr((ch8 const *)Current.f_GetArray(), Current.f_GetLen()) != Snapshot)
						return "changed on disk during formatting";

					auto Attributes = CFile::fs_GetAttributes(Path);
					CStr Temporary = Path + ".mtool-format";
					auto Cleanup = g_OnScopeExitCatch / [&Temporary]
						{
							if (CFile::fs_FileExists(Temporary, EFileAttrib_File))
								CFile::fs_DeleteFile(Temporary);
						}
					;
					if (CFile::fs_FileExists(Temporary, EFileAttrib_File))
						CFile::fs_DeleteFile(Temporary);

					CFile::fs_WriteStringToFile(Temporary, Formatted, false);
					CFile::fs_SetAttributes(Temporary, Attributes);
					CFile::fs_RenameFile(Temporary, Path);

					return {};
				}
			)
		;
		if (Written)
		{
			Result.m_Outcome = EFormatOutcome::mc_Failed;
			Result.m_Report += "{}: {}\n"_f << _Job.m_Path << Written;
		}

		co_return Result;
	}

	umint fg_GetDefaultFormatJobs()
	{
		return fg_Min(fg_Max(umint(NSys::fg_Thread_GetVirtualCores()), umint(1)), umint(16));
	}

	TCFuture<CStr> CFormatRootResolver::fp_AskGit(CStr _Directory)
	{
		auto Result = co_await NGit::fg_LaunchGitWithResult({"rev-parse", "--show-toplevel"}, _Directory);
		if (Result.m_ExitCode)
			co_return CStr();

		co_return Result.f_GetStdOut().f_Trim();
	}

	TCFuture<CStr> CFormatRootResolver::f_Resolve(CStr _Directory)
	{
		// Walk up to a directory that is known or being resolved, or that holds a repository
		// of its own. Every directory passed on the way shares that directory's answer.
		TCVector<CStr> Pending;
		auto Walk = CFile::fs_CondensePath(_Directory);
		TCSharedPointer<CEntry> pEntry;
		while (true)
		{
			if (auto pKnown = mp_Entries.f_FindEqual(Walk))
			{
				pEntry = *pKnown;

				break;
			}

			Pending.f_Insert(Walk);
			auto Parent = CFile::fs_CondensePath(CFile::fs_GetPath(Walk));
			if (CFile::fs_FileExists(Walk / ".git") || !Parent || Parent == Walk)
				break;

			Walk = Parent;
		}

		// Publish the pending resolve before awaiting so reentrant resolves can join it.
		bool bAsk = !pEntry;
		if (bAsk)
			pEntry = fg_Construct();

		for (auto const &Known : Pending)
			mp_Entries(Known, pEntry);

		if (!bAsk)
		{
			if (pEntry->m_Result.f_IsSet())
				co_return fg_TempCopy(pEntry->m_Result);

			co_return co_await pEntry->m_Waiters.f_Insert().f_Future();
		}

		auto Result = co_await fp_AskGit(fg_TempCopy(Walk)).f_Wrap();
		pEntry->m_Result = Result;
		if (!Result)
		{
			for (auto const &Known : Pending)
				mp_Entries.f_Remove(Known);
		}

		auto Waiters = fg_Move(pEntry->m_Waiters);
		for (auto &Promise : Waiters)
			Promise.f_SetResult(Result);

		co_return fg_Move(Result);
	}

	CStr fg_NormalizeFormatPath(CStr const &_Path, CStr const &_WorkingDirectory)
	{
		return CFile::fs_CondensePath(CFile::fs_GetFullPath(_Path, _WorkingDirectory));
	}

	// A directory's contents, read on a blocking actor along with the ignore files in it.
	struct CFormatListing
	{
		umint m_iRepository = 0;
		CStr m_Directory;
		TCVector<CFile::CFoundFile> m_Entries;
		bool m_bRepository = false;						// The directory holds a '.git' entry.
		TCOptional<CStr> m_Rules;						// The directory's own .gitignore.
		TCOptional<CStr> m_GlobalExcludes;				// The file core.excludesFile names, at a repository's root.
		TCOptional<CStr> m_Excludes;					// The repository's info/exclude, at its root.
	};

	// One repository the walk has met: the ignore rules gathered on the way down, and a
	// configuration resolver bounded by its root.
	struct CFormatWalkRepository
	{
		CStr m_Root;
		bool m_bGit = false;							// False for the working directory standing in for no repository.
		NGit::CGitIgnore m_Ignore;
		TCActor<NDevelop::CEditorConfigResolver> m_Configurations;
	};

	struct CFormatWalkFound
	{
		CStr m_Path;
		umint m_iRoot = 0;								// Index into the result's roots.
	};

	struct CFormatWalkResult
	{
		TCVector<CFormatWalkFound> m_Found;
		TCVector<CStr> m_Roots;
	};

	// Walks the tree a pattern names, listing directories in parallel on a bounded set of
	// blocking actors. A directory is not entered when git ignores it, by its .gitignore
	// files, its info/exclude, or the excludes file its configuration names, or when a
	// document above it disables formatting for everything below it; a document deeper
	// down that opts files back in is not seen then, which is the price of never reading
	// those trees. The listing order is not the selection order, which the caller settles
	// by sorting.
	struct CFormatWalk : CActor
	{
		CFormatWalk(CStr _Search, CStr _Root, bool _bGit, bool _bRecursive, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors);

		TCFuture<CFormatWalkResult> f_Run();

	protected:
		TCFuture<void> fp_Destroy() override;

	private:
		TCFuture<CFormatListing> fp_List(umint _iRepository, CStr _Directory);
		TCFuture<void> fp_LoadRulesAbove(umint _iRepository, CStr _Directory);
		umint fp_AddRepository(CStr const &_Root, bool _bGit);
		void fp_AddRepositoryExcludes(umint _iRepository, CFormatListing const &_Listing);
		bool fp_MatchesName(CStr const &_Name) const;

		CStr mp_Directory;
		CStr mp_Wildcard;								// Upper case, matched the way the platform matches a find pattern.
		bool mp_bMatchAll = false;
		bool mp_bRecursive = false;
		TCSharedPointer<CSharedRoundRobinBlockingActors> mp_pReaders;	// The run's, shared with the other walks and the resolvers.
		TCVector<CFormatWalkRepository> mp_Repositories;
		NGit::CGitEnvironment mp_GitEnvironment;
	};

	CFormatWalk::CFormatWalk(CStr _Search, CStr _Root, bool _bGit, bool _bRecursive, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors)
		: mp_Directory(CFile::fs_GetPath(_Search))
		, mp_Wildcard(CFile::fs_GetFile(_Search).f_UpperCase())
		, mp_bRecursive(_bRecursive)
		, mp_pReaders(_pBlockingActors)
	{
		mp_bMatchAll = mp_Wildcard == "*";
		mp_GitEnvironment = NGit::CGitEnvironment::fs_FromProcess();
		fp_AddRepository(_Root, _bGit);
	}

	umint CFormatWalk::fp_AddRepository(CStr const &_Root, bool _bGit)
	{
		auto &Repository = mp_Repositories.f_Insert();
		Repository.m_Root = _Root;
		Repository.m_bGit = _bGit;
		Repository.m_Configurations = fg_Construct(_Root, mp_pReaders);

		return mp_Repositories.f_GetLen() - 1;
	}

	// git ranks the excludes file below info/exclude and both below every .gitignore, and
	// rules added later win, so the root's own .gitignore is added after these.
	void CFormatWalk::fp_AddRepositoryExcludes(umint _iRepository, CFormatListing const &_Listing)
	{
		auto &Repository = mp_Repositories[_iRepository];
		if (_Listing.m_GlobalExcludes)
			Repository.m_Ignore.f_AddRules({}, *_Listing.m_GlobalExcludes);

		if (_Listing.m_Excludes)
			Repository.m_Ignore.f_AddRules({}, *_Listing.m_Excludes);
	}

	TCFuture<void> CFormatWalk::fp_Destroy()
	{
		for (auto &Repository : mp_Repositories)
			co_await fg_Move(Repository.m_Configurations).f_Destroy();

		co_return {};
	}

	bool CFormatWalk::fp_MatchesName(CStr const &_Name) const
	{
		if (mp_bMatchAll)
			return true;

		auto Name = _Name.f_UpperCase();

		return fg_StrMatchWildcard(Name.f_GetStr(), mp_Wildcard.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted;
	}

	TCFuture<CFormatListing> CFormatWalk::fp_List(umint _iRepository, CStr _Directory)
	{
		co_return co_await
			(
				g_Dispatch(mp_pReaders->f_Next()) / [iRepository = _iRepository, Directory = fg_Move(_Directory), Environment = mp_GitEnvironment]() -> CFormatListing
				{
					CFormatListing Listing;
					Listing.m_iRepository = iRepository;
					Listing.m_Directory = Directory;
					Listing.m_Entries = CFile::fs_FindFilesEx(Directory / "*", EFileAttrib_File | EFileAttrib_Directory, false, false);
					for (auto const &Entry : Listing.m_Entries)
					{
						auto Name = CFile::fs_GetFile(Entry.m_Path);
						if (Name == ".git")
						{
							Listing.m_bRepository = true;
							auto Excludes = NGit::fg_GetGitRepositoryExcludes(Directory, NGit::fg_GetGitDirectories(Directory), Environment);
							if (Excludes.m_ExcludesFile)
								Listing.m_GlobalExcludes = CFile::fs_ReadStringFromFile(Excludes.m_ExcludesFile, true);

							if (Excludes.m_InfoExclude)
								Listing.m_Excludes = CFile::fs_ReadStringFromFile(Excludes.m_InfoExclude, true);
						}
						else if (Name == ".gitignore" && (Entry.m_Attribs & EFileAttrib_File))
							Listing.m_Rules = CFile::fs_ReadStringFromFile(Entry.m_Path, true);
					}

					return Listing;
				}
			)
		;
	}

	// The rules of the ignore files between a repository's root and the directory the walk
	// starts in apply to that directory, so they are gathered before the first listing.
	TCFuture<void> CFormatWalk::fp_LoadRulesAbove(umint _iRepository, CStr _Directory)
	{
		auto const &Root = mp_Repositories[_iRepository].m_Root;
		TCVector<CStr> Directories;
		for (auto Walk = CFile::fs_GetPath(_Directory); Walk && Walk.f_GetLen() >= Root.f_GetLen(); Walk = CFile::fs_GetPath(Walk))
		{
			Directories.f_Insert(Walk);
			if (Walk == Root)
				break;
		}

		for (umint i = Directories.f_GetLen(); i; --i)
		{
			auto Listing = co_await fp_List(_iRepository, Directories[i - 1]);
			auto &Repository = mp_Repositories[_iRepository];
			fp_AddRepositoryExcludes(_iRepository, Listing);

			if (Listing.m_Rules)
				Repository.m_Ignore.f_AddRules(CFile::fs_MakePathRelative(Listing.m_Directory, Repository.m_Root), *Listing.m_Rules);
		}

		co_return {};
	}

	TCFuture<CFormatWalkResult> CFormatWalk::f_Run()
	{
		CFormatWalkResult Result;
		if (mp_Repositories[0].m_bGit)
			co_await fp_LoadRulesAbove(0, mp_Directory);

		// Listings are awaited in the order they were issued, while the readers keep as many
		// of the later ones in flight as there are readers.
		TCVector<TCFuture<CFormatListing>> Pending;
		Pending.f_Insert(fp_List(0, mp_Directory));
		for (umint iPending = 0; iPending < Pending.f_GetLen(); ++iPending)
		{
			auto Listing = co_await fg_Move(Pending[iPending]);
			auto iRepository = Listing.m_iRepository;
			// The walk's own root is listed here too when it is the start directory, in
			// which case nothing above loaded its excludes.
			if (Listing.m_bRepository)
			{
				if (Listing.m_Directory != mp_Repositories[iRepository].m_Root)
					iRepository = fp_AddRepository(Listing.m_Directory, true);

				fp_AddRepositoryExcludes(iRepository, Listing);
			}

			if (Listing.m_Rules && mp_Repositories[iRepository].m_bGit)
			{
				auto &Repository = mp_Repositories[iRepository];
				auto Relative = CFile::fs_MakePathRelative(Listing.m_Directory, Repository.m_Root);
				Repository.m_Ignore.f_AddRules(Relative == "." ? CStr() : Relative, *Listing.m_Rules);
			}

			for (auto &Entry : Listing.m_Entries)
			{
				if (Entry.m_Attribs & EFileAttrib_Link)
					continue;

				auto Name = CFile::fs_GetFile(Entry.m_Path);
				if (Name == ".git")
					continue;

				bool bDirectory = (Entry.m_Attribs & EFileAttrib_Directory) != 0;
				if (bDirectory && !mp_bRecursive)
					continue;

				if (!bDirectory && !fp_MatchesName(Name))
					continue;

				auto const &Repository = mp_Repositories[iRepository];
				if (Repository.m_bGit && Repository.m_Ignore.f_IsIgnored(CFile::fs_MakePathRelative(Entry.m_Path, Repository.m_Root), bDirectory))
					continue;

				if (!bDirectory)
				{
					Result.m_Found.f_Insert({fg_Move(Entry.m_Path), iRepository});

					continue;
				}

				// Only a document above that speaks for everything below can close a directory:
				// a directory no document mentions may hold a document of its own that opts
				// its files in, as a module's does.
				auto Below = co_await mp_Repositories[iRepository].m_Configurations(&NDevelop::CEditorConfigResolver::f_ResolveBelow, Entry.m_Path);
				bool bSettled = Below.m_Settled.f_FindEqual("malterlib_format") && !Below.m_Uncertain.f_FindEqual("malterlib_format");
				if (bSettled && !CCodeFormattingSettings(Below.m_Properties).f_IsFormattingEnabled())
					continue;

				Pending.f_Insert(fp_List(iRepository, fg_Move(Entry.m_Path)));
			}
		}

		for (auto const &Repository : mp_Repositories)
			Result.m_Roots.f_Insert(Repository.m_Root);

		co_return fg_Move(Result);
	}
}

namespace NMib::NTool::NFormat
{
	// Runs the selected files over a pool of worker actors sized by the host. Every file is
	// queued at once, in turn over the workers, and the results come back in the jobs'
	// order whatever order the files finished in.
	TCFuture<TCVector<CFormatJobResult>> fg_RunFormatJobs(TCVector<CFormatJob> _Jobs, umint _nJobs)
	{
		TCVector<CFormatJobResult> Results;
		if (_Jobs.f_IsEmpty())
			co_return Results;

		auto nWorkers = fg_Min(_nJobs, _Jobs.f_GetLen());
		TCSharedPointer<CSharedRoundRobinBlockingActors> pBlockingActors = fg_Construct(gc_nFormatBlockingActors);
		TCVector<TCActor<CFormatWorker>> Workers;
		for (umint i = 0; i < nWorkers; ++i)
			Workers.f_InsertLast(fg_ConstructActor<CFormatWorker>(pBlockingActors));

		auto DestroyWorkers = co_await fg_AsyncDestroy
			(
				[&Workers]() -> TCFuture<void>
				{
					for (auto &Worker : Workers)
						co_await fg_Move(Worker).f_Destroy();

					co_return {};
				}
			)
		;

		TCFutureVector<CFormatJobResult> Pending;
		Pending.f_SetLen(_Jobs.f_GetLen());
		for (umint iJob = 0; iJob < _Jobs.f_GetLen(); ++iJob)
			Workers[iJob % nWorkers](&CFormatWorker::f_Process, _Jobs[iJob]) > Pending;

		auto Outcomes = co_await fg_AllDoneWrapped(Pending);
		Results.f_SetLen(_Jobs.f_GetLen());
		for (umint iJob = 0; iJob < _Jobs.f_GetLen(); ++iJob)
		{
			auto &Outcome = Outcomes[iJob];
			if (Outcome)
				Results[iJob] = fg_Move(*Outcome);
			else
			{
				Results[iJob].m_Path = _Jobs[iJob].m_Path;
				Results[iJob].m_Outcome = EFormatOutcome::mc_Failed;
				Results[iJob].m_Report = "{}\n"_f << Outcome.f_GetExceptionStr();
			}
		}

		co_return Results;
	}

	// One run over every selection: the walks list their trees in parallel on the shared
	// blocking actors, and a root's files are queued on the workers the moment its walk is
	// done, while the other walks go on. The report is written once everything is done,
	// in path order, so that it reads the same whatever order the walks finished in.
	struct CFormatRun : CActor
	{
		explicit CFormatRun(CFormatOptions _Options);

		TCFuture<CFormatRunResult> f_Run(TCVector<CFormatSelection> _Selections, CFormatSink _Sink);

	protected:
		TCFuture<void> fp_Destroy() override;

	private:
		TCFuture<void> fp_Select(CFormatSelection _Selection);
		void fp_Schedule(CStr const &_Path, CStr const &_Root);

		CFormatOptions mp_Options;
		TCSharedPointer<CSharedRoundRobinBlockingActors> mp_pBlockingActors;
		TCVector<TCActor<CFormatWorker>> mp_Workers;
		umint mp_iNextWorker = 0;
		TCActor<CFormatRootResolver> mp_RootResolver;
		TCSet<CStr> mp_Scheduled;						// A file named by more than one selection is formatted once.
		TCFutureVector<CFormatJobResult> mp_Pending;
	};

	CFormatRun::CFormatRun(CFormatOptions _Options)
		: mp_Options(fg_Move(_Options))
		, mp_pBlockingActors(fg_Construct(gc_nFormatBlockingActors))
		, mp_RootResolver(fg_ConstructActor<CFormatRootResolver>())
	{
		for (umint i = 0; i < fg_Max(mp_Options.m_nJobs, umint(1)); ++i)
			mp_Workers.f_InsertLast(fg_ConstructActor<CFormatWorker>(mp_pBlockingActors));
	}

	TCFuture<void> CFormatRun::fp_Destroy()
	{
		for (auto &Worker : mp_Workers)
			co_await fg_Move(Worker).f_Destroy();

		co_await fg_Move(mp_RootResolver).f_Destroy();

		co_return {};
	}

	// Every candidate is a job; the workers resolve its configuration and report a file
	// the configuration does not opt in as excluded.
	void CFormatRun::fp_Schedule(CStr const &_Path, CStr const &_Root)
	{
		if (mp_Scheduled.f_FindEqual(_Path))
			return;

		mp_Scheduled.f_Insert(_Path);

		CFormatJob Job;
		Job.m_Path = _Path;
		Job.m_Root = _Root;
		Job.m_bResolveSettings = true;
		Job.m_ByteRanges = mp_Options.m_ByteRanges;
		Job.m_iFirstLine = mp_Options.m_iFirstLine;
		Job.m_iLastLine = mp_Options.m_iLastLine;
		Job.m_RangePolicy = mp_Options.m_RangePolicy;
		Job.m_Mode = mp_Options.m_Mode;
		mp_Workers[mp_iNextWorker++ % mp_Workers.f_GetLen()](&CFormatWorker::f_Process, fg_Move(Job)) > mp_Pending;
	}

	// A file named outright is taken as it is; its repository bounds its configuration. A
	// pattern walks its tree, and a directory is entered only when git does not ignore it
	// and the configuration can still opt in a file below it. That is what keeps a
	// dependency's build output or a tracked import cache from being listed file by file.
	TCFuture<void> CFormatRun::fp_Select(CFormatSelection _Selection)
	{
		auto WorkingDirectory = fg_NormalizeFormatPath(_Selection.m_WorkingDirectory, CFile::fs_GetCurrentDirectory());
		for (auto const &File : _Selection.m_Files)
		{
			auto Path = fg_NormalizeFormatPath(File, WorkingDirectory);
			if (CFile::fs_GetAttributesOnLink(Path) & EFileAttrib_Link)
				co_return DMibErrorInstance("Refusing to format the symbolic link '{}'"_f << Path);

			if (!CFile::fs_FileExists(Path, EFileAttrib_File))
				co_return DMibErrorInstance("'{}' is not an existing regular file"_f << Path);

			auto Root = co_await mp_RootResolver(&CFormatRootResolver::f_Resolve, CFile::fs_GetPath(Path));
			fp_Schedule(Path, Root ? Root : WorkingDirectory);
		}

		for (auto const &Pattern : _Selection.m_Patterns)
		{
			auto Search = fg_NormalizeFormatPath(Pattern, WorkingDirectory);
			auto Root = co_await mp_RootResolver(&CFormatRootResolver::f_Resolve, CFile::fs_GetPath(Search));
			TCActor<CFormatWalk> Walk = fg_ConstructActor<CFormatWalk>(Search, Root ? Root : WorkingDirectory, bool(Root), _Selection.m_bRecursive, mp_pBlockingActors);
			auto DestroyWalk = co_await fg_AsyncDestroy(Walk);
			auto Walked = co_await Walk(&CFormatWalk::f_Run);
			for (auto &Found : Walked.m_Found)
				fp_Schedule(Found.m_Path, Walked.m_Roots[Found.m_iRoot]);
		}

		co_return {};
	}

	TCFuture<CFormatRunResult> CFormatRun::f_Run(TCVector<CFormatSelection> _Selections, CFormatSink _Sink)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Running Format");

		CStopwatch Stopwatch{true};
		TCFutureVector<void> Selections;
		for (auto &Selection : _Selections)
			fp_Select(fg_Move(Selection)) > Selections;

		co_await fg_AllDone(Selections);

		if (mp_Scheduled.f_IsEmpty())
			co_return DMibErrorInstance("No files matched the requested selection");

		bool bHasRange = mp_Options.m_iFirstLine || !mp_Options.m_ByteRanges.f_IsEmpty();
		if (bHasRange && mp_Scheduled.f_GetLen() != 1)
			co_return DMibErrorInstance("Range options require exactly one selected file; {} were selected"_f << mp_Scheduled.f_GetLen());

		// The results are reported in path order, so a parallel run reports exactly like
		// --jobs 1, and a file that failed is reported where it would have been.
		auto Outcomes = co_await fg_AllDoneWrapped(mp_Pending);
		TCVector<CFormatJobResult> Results;
		for (auto &Outcome : Outcomes)
		{
			if (Outcome)
				Results.f_Insert(fg_Move(*Outcome));
			else
			{
				auto &Result = Results.f_Insert();
				Result.m_Outcome = EFormatOutcome::mc_Failed;
				Result.m_Report = "{}\n"_f << Outcome.f_GetExceptionStr();
			}
		}

		Results.f_Sort
			(
				[](CFormatJobResult const &_Left, CFormatJobResult const &_Right)
				{
					return _Left.m_Path <=> _Right.m_Path;
				}
			)
		;

		CFormatSummary Summary;
		Summary.m_nSelected = Results.f_GetLen();
		for (auto const &Result : Results)
		{
			if (Result.m_Report)
				_Sink.m_fReport(Result.m_Report);

			if (Result.m_Patch)
				_Sink.m_fPatch(Result.m_Patch);

			Summary.m_nUnresolved += Result.m_nUnresolved;
			switch (Result.m_Outcome)
			{
				case EFormatOutcome::mc_Excluded:
					++Summary.m_nExcluded;

					break;
				case EFormatOutcome::mc_Changed:
					++Summary.m_nChanged;

					break;
				case EFormatOutcome::mc_Failed:
					++Summary.m_nFailed;

					break;
				default:
					++Summary.m_nUnchanged;

					break;
			}
		}

		CStr Action = mp_Options.m_Mode == EFormatMode::mc_Write ? "changed" : "would change";
		CStr Line = "Formatted {} file(s): {} unchanged, {} {}, {} unresolved violation(s), {} failed. Excluded {} file(s). Time: {fe2} s.\n"_f
			<< Summary.m_nSelected
			<< Summary.m_nUnchanged
			<< Summary.m_nChanged
			<< Action
			<< Summary.m_nUnresolved
			<< Summary.m_nFailed
			<< Summary.m_nExcluded
			<< Stopwatch.f_GetTime()
		;
		_Sink.m_fReport(Line);

		if (Summary.m_nFailed)
			co_return DMibErrorInstance("{} file(s) could not be formatted"_f << Summary.m_nFailed);

		CFormatRunResult Run;
		Run.m_Summary = Summary;
		if (Summary.m_nUnresolved || (mp_Options.m_Mode != EFormatMode::mc_Write && Summary.m_nChanged))
			Run.m_ExitCode = 1;

		co_return Run;
	}

	TCFuture<CFormatRunResult> fg_RunFormat(TCVector<CFormatSelection> _Selections, CFormatOptions _Options, CFormatSink _Sink)
	{
		TCActor<CFormatRun> Run = fg_ConstructActor<CFormatRun>(fg_Move(_Options));
		auto DestroyRun = co_await fg_AsyncDestroy(Run);

		co_return co_await Run(&CFormatRun::f_Run, fg_Move(_Selections), fg_Move(_Sink));
	}
}
