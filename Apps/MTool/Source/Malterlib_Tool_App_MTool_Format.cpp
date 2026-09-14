// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Format.h"

#include <Mib/Git/Helpers/Launch>
#include <Mib/Git/Ignore>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Container/Map>
#include <Mib/Core/OnScopeExitCatch>
#include <Mib/Time/Stopwatch>

// Actor call arguments and results need externally nameable types, so the command's
// helpers live in a named namespace instead of an anonymous one.
namespace NMib::NTool::NFormat
{
	using namespace NMib::NDevelop;

	umint fg_ParseFormatCount(CStr const &_Value, CStr const &_Name)
	{
		umint Value = 0;
		if (_Value.f_IsEmpty())
			DMibError("Invalid {}: expected a number"_f << _Name);

		for (auto pParse = _Value.f_GetStr(); *pParse; ++pParse)
		{
			auto Char = *pParse;
			if (Char < '0' || Char > '9' || Value > (TCLimitsInt<umint>::mc_Max - umint(Char - '0')) / 10)
				DMibError("Invalid {}: '{}'"_f << _Name << _Value);

			Value = Value * 10 + umint(Char - '0');
		}

		return Value;
	}

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
	CFormatWorker::CFormatWorker(CStr _Root, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors)
		: mp_Root(fg_Move(_Root))
		, mp_pBlockingActors(_pBlockingActors)
	{
		if (mp_Root)
			mp_Configurations = fg_Construct(mp_Root, mp_pBlockingActors);
	}

	// The resolver is an actor of its own, and its destruction is awaited here so that the
	// worker's own destruction, and the run, do not end before it.
	TCFuture<void> CFormatWorker::fp_Destroy()
	{
		if (mp_Configurations)
			co_await fg_Move(mp_Configurations).f_Destroy();

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
			auto Properties = co_await mp_Configurations(&NDevelop::CEditorConfigResolver::f_Resolve, _Job.m_Path);
			_Job.m_Settings = CCodeFormattingSettings(Properties);
			if (!_Job.m_Settings.f_IsFormattingEnabled())
			{
				Result.m_Outcome = EFormatOutcome::mc_Excluded;

				co_return Result;
			}

			_Job.m_DisplayPath = CFile::fs_MakePathRelative(_Job.m_Path, mp_Root);
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

	struct CFormatCandidate
	{
		CStr m_Path;
		umint m_iRoot = 0;
	};

	// A directory's contents, read on a blocking actor along with the ignore files in it.
	struct CFormatListing
	{
		umint m_iRepository = 0;
		CStr m_Directory;
		TCVector<CFile::CFoundFile> m_Entries;
		bool m_bRepository = false;						// The directory holds a '.git' entry.
		TCOptional<CStr> m_Rules;						// The directory's own .gitignore.
		TCOptional<CStr> m_Excludes;					// The repository's .git/info/exclude, at its root.
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
	// blocking actors. A directory is not entered when git ignores it, or when a document
	// above it disables formatting for everything below it; a document deeper down that
	// opts files back in is not seen then, which is the price of never reading those
	// trees. The listing order is not the selection order, which the caller settles by
	// sorting.
	struct CFormatWalk : CActor
	{
		CFormatWalk(CStr _Search, CStr _Root, bool _bGit, bool _bRecursive, umint _nJobs);

		TCFuture<CFormatWalkResult> f_Run();

	protected:
		TCFuture<void> fp_Destroy() override;

	private:
		TCFuture<CFormatListing> fp_List(umint _iRepository, CStr _Directory);
		TCFuture<void> fp_LoadRulesAbove(umint _iRepository, CStr _Directory);
		umint fp_AddRepository(CStr const &_Root, bool _bGit);
		bool fp_MatchesName(CStr const &_Name) const;

		CStr mp_Directory;
		CStr mp_Wildcard;								// Upper case, matched the way the platform matches a find pattern.
		bool mp_bMatchAll = false;
		bool mp_bRecursive = false;
		TCSharedPointer<CSharedRoundRobinBlockingActors> mp_pReaders;	// Shared with the repositories' resolvers.
		TCVector<CFormatWalkRepository> mp_Repositories;
	};

	CFormatWalk::CFormatWalk(CStr _Search, CStr _Root, bool _bGit, bool _bRecursive, umint _nJobs)
		: mp_Directory(CFile::fs_GetPath(_Search))
		, mp_Wildcard(CFile::fs_GetFile(_Search).f_UpperCase())
		, mp_bRecursive(_bRecursive)
	{
		mp_bMatchAll = mp_Wildcard == "*";
		mp_pReaders = fg_Construct(_nJobs);
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
				g_Dispatch(mp_pReaders->f_Next()) / [iRepository = _iRepository, Directory = fg_Move(_Directory)]() -> CFormatListing
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
							auto Excludes = Entry.m_Path / "info/exclude";
							if ((Entry.m_Attribs & EFileAttrib_Directory) && CFile::fs_FileExists(Excludes, EFileAttrib_File))
								Listing.m_Excludes = CFile::fs_ReadStringFromFile(Excludes, true);
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
			if (Listing.m_Excludes)
				Repository.m_Ignore.f_AddRules({}, *Listing.m_Excludes);

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
			if (Listing.m_bRepository && Listing.m_Directory != mp_Repositories[iRepository].m_Root)
			{
				iRepository = fp_AddRepository(Listing.m_Directory, true);
				if (Listing.m_Excludes)
					mp_Repositories[iRepository].m_Ignore.f_AddRules({}, *Listing.m_Excludes);
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
	struct CFormatSummary
	{
		umint m_nSelected = 0;
		umint m_nExcluded = 0;
		umint m_nUnchanged = 0;
		umint m_nChanged = 0;
		umint m_nUnresolved = 0;
		umint m_nFailed = 0;
	};

	struct CFormatOptions
	{
		EFormatMode m_Mode = EFormatMode::mc_Write;
		umint m_nJobs = 1;
		umint m_iFirstLine = 0;
		umint m_iLastLine = 0;
		TCVector<CCodeFormattingRange> m_ByteRanges;
		ECodeRangePolicy m_RangePolicy = ECodeRangePolicy::mc_Expand;
	};

	// Runs the selected files over a pool of worker actors sized by the host. Every file is
	// queued at once, in turn over the workers, and the results come back in the jobs'
	// order whatever order the files finished in.
	TCFuture<TCVector<CFormatJobResult>> fg_RunFormatJobs(TCVector<CFormatJob> _Jobs, umint _nJobs, CStr _Root)
	{
		TCVector<CFormatJobResult> Results;
		if (_Jobs.f_IsEmpty())
			co_return Results;

		auto nWorkers = fg_Min(_nJobs, _Jobs.f_GetLen());
		TCSharedPointer<CSharedRoundRobinBlockingActors> pBlockingActors = fg_Construct(nWorkers);
		TCVector<TCActor<CFormatWorker>> Workers;
		for (umint i = 0; i < nWorkers; ++i)
			Workers.f_InsertLast(fg_ConstructActor<CFormatWorker>(_Root, pBlockingActors));

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

	TCFuture<uint32> fg_RunFormat
		(
			CStr _WorkingDirectory
			, TCVector<CStr> _Files
			, TCVector<CStr> _Patterns
			, bool _bRecursive
			, CFormatOptions _Options
			, TCSharedPointer<CCommandLineControl> _pCommandLine
		)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Running Format");

		CStopwatch Stopwatch{true};
		auto WorkingDirectory = fg_NormalizeFormatPath(_WorkingDirectory, CFile::fs_GetCurrentDirectory());
		bool bHasRange = _Options.m_iFirstLine || !_Options.m_ByteRanges.f_IsEmpty();
		TCVector<CFormatCandidate> Candidates;
		TCVector<CStr> Roots;
		auto fRootIndex = [&](CStr const &_Root)
			{
				umint iRoot = 0;
				while (iRoot < Roots.f_GetLen() && Roots[iRoot] != _Root)
					++iRoot;

				if (iRoot == Roots.f_GetLen())
					Roots.f_Insert(_Root);

				return iRoot;
			}
		;

		// A file named outright is taken as it is; its repository bounds its configuration.
		TCActor<CFormatRootResolver> RootResolver = fg_ConstructActor<CFormatRootResolver>();
		auto DestroyRootResolver = co_await fg_AsyncDestroy(RootResolver);
		for (auto const &File : _Files)
		{
			auto Path = fg_NormalizeFormatPath(File, WorkingDirectory);
			if (CFile::fs_GetAttributesOnLink(Path) & EFileAttrib_Link)
				co_return DMibErrorInstance("Refusing to format the symbolic link '{}'"_f << Path);

			if (!CFile::fs_FileExists(Path, EFileAttrib_File))
				co_return DMibErrorInstance("'{}' is not an existing regular file"_f << Path);

			auto Root = co_await RootResolver(&CFormatRootResolver::f_Resolve, CFile::fs_GetPath(Path));
			if (!Root)
				Root = WorkingDirectory;

			Candidates.f_Insert({Path, fRootIndex(Root)});
		}

		// A pattern walks its tree, and a directory is entered only when git does not ignore
		// it and the configuration can still opt in a file below it. That is what keeps a
		// dependency's build output or a tracked import cache from being listed file by file.
		for (auto const &Pattern : _Patterns)
		{
			auto Search = fg_NormalizeFormatPath(Pattern, WorkingDirectory);
			auto Root = co_await RootResolver(&CFormatRootResolver::f_Resolve, CFile::fs_GetPath(Search));
			TCActor<CFormatWalk> Walk = fg_ConstructActor<CFormatWalk>(Search, Root ? Root : WorkingDirectory, bool(Root), _bRecursive, _Options.m_nJobs);
			auto DestroyWalk = co_await fg_AsyncDestroy(Walk);
			auto Walked = co_await Walk(&CFormatWalk::f_Run);
			for (auto &Found : Walked.m_Found)
				Candidates.f_Insert({fg_Move(Found.m_Path), fRootIndex(Walked.m_Roots[Found.m_iRoot])});
		}

		if (Candidates.f_IsEmpty())
			co_return DMibErrorInstance("No files matched the requested selection");

		// Directory enumeration order is not stable, so selection order is, keeping
		// diagnostics identical between runs and between job counts. A file named by
		// more than one selector is formatted once.
		Candidates.f_Sort
			(
				[](CFormatCandidate const &_Left, CFormatCandidate const &_Right)
				{
					return _Left.m_Path <=> _Right.m_Path;
				}
			)
		;
		umint nUnique = 0;
		for (umint i = 0; i < Candidates.f_GetLen(); ++i)
		{
			if (nUnique && Candidates[nUnique - 1].m_Path == Candidates[i].m_Path)
				continue;

			if (nUnique != i)
				Candidates[nUnique] = fg_Move(Candidates[i]);

			++nUnique;
		}

		Candidates.f_SetLen(nUnique);
		if (bHasRange && Candidates.f_GetLen() != 1)
			co_return DMibErrorInstance("Range options require exactly one selected file; {} were selected"_f << Candidates.f_GetLen());

		TCVector<TCVector<CStr>> Grouped;
		Grouped.f_SetLen(Roots.f_GetLen());
		for (auto &Candidate : Candidates)
			Grouped[Candidate.m_iRoot].f_Insert(fg_Move(Candidate.m_Path));

		CFormatSummary Summary;
		Summary.m_nSelected = Candidates.f_GetLen();
		for (umint iRoot = 0; iRoot < Roots.f_GetLen(); ++iRoot)
		{
			// Every candidate is a job; the workers resolve its configuration and report a
			// file the configuration does not opt in as excluded.
			TCVector<CFormatJob> Jobs;
			for (auto const &Path : Grouped[iRoot])
			{
				auto &Job = Jobs.f_Insert();
				Job.m_Path = Path;
				Job.m_bResolveSettings = true;
				Job.m_ByteRanges = _Options.m_ByteRanges;
				Job.m_iFirstLine = _Options.m_iFirstLine;
				Job.m_iLastLine = _Options.m_iLastLine;
				Job.m_RangePolicy = _Options.m_RangePolicy;
				Job.m_Mode = _Options.m_Mode;
			}

			// The coordinator owns ordering, so parallel runs report exactly like --jobs 1.
			for (auto const &Result : co_await fg_RunFormatJobs(fg_Move(Jobs), _Options.m_nJobs, Roots[iRoot]))
			{
				if (Result.m_Report)
					*_pCommandLine %= Result.m_Report;

				if (Result.m_Patch)
					*_pCommandLine += Result.m_Patch;

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
		}

		CStr Action = _Options.m_Mode == EFormatMode::mc_Write ? "changed" : "would change";
		*_pCommandLine %= "Formatted {} file(s): {} unchanged, {} {}, {} unresolved violation(s), {} failed. Excluded {} file(s). Time: {fe2} s.\n"_f
			<< Summary.m_nSelected
			<< Summary.m_nUnchanged
			<< Summary.m_nChanged
			<< Action
			<< Summary.m_nUnresolved
			<< Summary.m_nFailed
			<< Summary.m_nExcluded
			<< Stopwatch.f_GetTime()
		;

		if (Summary.m_nFailed)
			co_return DMibErrorInstance("{} file(s) could not be formatted"_f << Summary.m_nFailed);

		if (Summary.m_nUnresolved)
			co_return 1;

		if (_Options.m_Mode != EFormatMode::mc_Write && Summary.m_nChanged)
			co_return 1;

		co_return 0;
	}

	TCFuture<uint32> fg_PrepareAndRunFormat(CEJsonSorted _Params, TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Preparing Format");

		CFormatOptions Options;
		bool bCheck = _Params["Check"].f_Boolean();
		bool bDiff = _Params["Diff"].f_Boolean();
		if (bCheck && bDiff)
			co_return DMibErrorInstance("--check and --diff cannot be combined");

		if (bCheck)
			Options.m_Mode = EFormatMode::mc_Check;
		else if (bDiff)
			Options.m_Mode = EFormatMode::mc_Diff;

		auto nJobs = umint(_Params["Jobs"].f_Integer());
		if (!nJobs)
			nJobs = fg_GetDefaultFormatJobs();

		Options.m_nJobs = nJobs;
		Options.m_RangePolicy = _Params["StrictRange"].f_Boolean() ? ECodeRangePolicy::mc_Strict : ECodeRangePolicy::mc_Expand;

		auto pLines = _Params.f_GetMember("Lines");
		auto pOffset = _Params.f_GetMember("Offset");
		auto pLength = _Params.f_GetMember("Length");
		if (pLines && (pOffset || pLength))
			co_return DMibErrorInstance("--lines cannot be combined with --offset or --length");

		if (bool(pOffset) != bool(pLength))
			co_return DMibErrorInstance("--offset and --length must be given together");

		if (pLines)
		{
			auto Value = pLines->f_String();
			auto iSeparator = Value.f_Find(":");
			if (iSeparator < 0)
				co_return DMibErrorInstance("Invalid --lines '{}': expected FIRST:LAST"_f << Value);

			Options.m_iFirstLine = fg_ParseFormatCount(Value.f_Left(iSeparator), "--lines");
			Options.m_iLastLine = fg_ParseFormatCount(Value.f_Extract(iSeparator + 1), "--lines");
			if (!Options.m_iFirstLine || Options.m_iLastLine < Options.m_iFirstLine)
				co_return DMibErrorInstance("Invalid --lines '{}': expected one-based, non-decreasing line numbers"_f << Value);
		}

		if (pOffset)
		{
			auto &Range = Options.m_ByteRanges.f_Insert();
			Range.m_iOffset = fg_ParseFormatCount(pOffset->f_String(), "--offset");
			Range.m_nLength = fg_ParseFormatCount(pLength->f_String(), "--length");
			if (Range.m_iOffset > TCLimitsInt<umint>::mc_Max - Range.m_nLength)
				co_return DMibErrorInstance("--offset and --length overflow the addressable range");
		}

		TCVector<CStr> Files;
		for (auto const &File : _Params["File"].f_Array())
			Files.f_Insert(File.f_String());

		TCVector<CStr> Patterns;
		for (auto const &Pattern : _Params["Pattern"].f_Array())
			Patterns.f_Insert(Pattern.f_String());

		if (Files.f_IsEmpty() && Patterns.f_IsEmpty())
			co_return DMibErrorInstance("Format requires at least one --file or --pattern selector");

		if ((Options.m_iFirstLine || !Options.m_ByteRanges.f_IsEmpty()) && (Files.f_GetLen() != 1 || !Patterns.f_IsEmpty()))
			co_return DMibErrorInstance("Range options require exactly one --file and no --pattern");

		co_return co_await fg_RunFormat
			(
				_Params["WorkingDirectory"].f_String(), fg_Move(Files), fg_Move(Patterns), _Params["Recursive"].f_Boolean()
				, fg_Move(Options), fg_Move(_pCommandLine)
			)
		;
	}
}

struct CTool_Format : CDistributedTool
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
					"Names"_o= _o["Format"]
					, "Description"_o= "Format sources that opt in with malterlib_format = malterlib in .editorconfig.\n"
					, "Category"_o= "Validation"
					, "Options"_o=
					{
						"WorkingDirectory?"_o=
						{
							"Names"_o= _o["--working-directory", "-C"]
							, "Default"_o= CFile::fs_GetCurrentDirectory()
							, "Description"_o= "Directory that relative files and patterns resolve against.\n"
						}
						, "File?"_o=
						{
							"Names"_o= _o["--file", "-f"]
							, "Type"_o= _o[""]
							, "Default"_o= _o[]
							, "Description"_o= "Format these files, given as one path or a comma-separated list.\n"
						}
						, "Pattern?"_o=
						{
							"Names"_o= _o["--pattern", "-p"]
							, "Type"_o= _o[""]
							, "Default"_o= _o[]
							, "Description"_o= "Format files matching these wildcards, given as one pattern or a comma-separated list. Quote patterns so the shell does not expand them.\n"
						}
						, "Recursive?"_o=
						{
							"Names"_o= _o["--recursive", "-r"]
							, "Default"_o= false
							, "Description"_o=
								"Also match patterns in descendant directories. A directory git ignores, or one a configuration document disables formatting under, is not entered.\n"
						}
						, "Check?"_o=
						{
							"Names"_o= _o["--check"]
							, "Default"_o= false
							, "Description"_o= "Report formatting violations without writing any file.\n"
						}
						, "Diff?"_o=
						{
							"Names"_o= _o["--diff"]
							, "Default"_o= false
							, "Description"_o= "Print the proposed patch without writing any file.\n"
						}
						, "Jobs?"_o=
						{
							"Names"_o= _o["--jobs", "-j"]
							, "Default"_o= 0
							, "Description"_o= "Maximum number of files formatted concurrently. Zero selects a bounded host-capacity default.\n"
						}
						, "Lines?"_o=
						{
							"Names"_o= _o["--lines"]
							, "Type"_o= ""
							, "Description"_o= "Format only these one-based inclusive source lines, written as FIRST:LAST.\n"
						}
						, "Offset?"_o=
						{
							"Names"_o= _o["--offset"]
							, "Type"_o= ""
							, "Description"_o= "Format only the selection starting at this zero-based byte offset. Requires --length.\n"
						}
						, "Length?"_o=
						{
							"Names"_o= _o["--length"]
							, "Type"_o= ""
							, "Description"_o= "Byte length of the --offset selection. A zero length formats the unit at the cursor.\n"
						}
						, "StrictRange?"_o=
						{
							"Names"_o= _o["--strict-range"]
							, "Default"_o= false
							, "Description"_o= "Never modify bytes outside the requested range; report units that would need one instead.\n"
						}
					}
				}
				, [](CEJsonSorted const _Params, TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					auto Prepared = co_await NTool::NFormat::fg_PrepareAndRunFormat(_Params, _pCommandLine).f_Wrap();
					if (Prepared)
						co_return *Prepared;

					// Input, configuration, and worker failures are operational errors, which
					// stay distinguishable from formatting violations.
					*_pCommandLine %= "{}\n"_f << Prepared.f_GetExceptionStr();

					co_return 2;
				}
			)
		;
	}
};

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_Format);
