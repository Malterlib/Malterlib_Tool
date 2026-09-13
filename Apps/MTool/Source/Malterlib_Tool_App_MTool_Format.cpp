// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Format.h"

#include <Mib/Git/Helpers/Launch>
#include <Mib/Concurrency/AsyncDestroy>
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
	void fg_DescribeDiagnostics(CFormatJob const &_Job, TCVector<CCodeFormattingDiagnostic> const &_Diagnostics, CFormatJobResult &o_Result)
	{
		for (auto const &Diagnostic : _Diagnostics)
		{
			if (!fg_IsReportedLine(_Job.m_ReportedLines, Diagnostic.m_iLine))
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

	TCFuture<CFormatJobResult> CFormatWorker::f_Process(CFormatJob _Job)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Formatting '{}'"_f << _Job.m_Path));

		CFormatJobResult Result;
		Result.m_Path = _Job.m_Path;
		auto BlockingActor = fg_BlockingActor();
		auto Snapshot = _Job.m_Source;
		if (!_Job.m_bHasSource)
		{
			Snapshot = co_await
				(
					g_Dispatch(BlockingActor) / [Path = _Job.m_Path]() -> CStr
					{
						auto Data = CFile::fs_ReadFile(Path);

						return CStr((ch8 const *)Data.f_GetArray(), Data.f_GetLen());
					}
				)
			;
		}

		CCodeFormattingRequest Request;
		Request.m_Source = Snapshot;
		Request.m_Path = _Job.m_Path;
		Request.m_Language = fg_DetectCodeLanguage(_Job.m_Path);
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
				g_Dispatch(BlockingActor) / [Path = _Job.m_Path, Snapshot, Formatted]() -> CStr
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

	TCFuture<CStr> fg_ResolveFormatRoot(CStr _Path)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Resolving the configuration boundary for '{}'"_f << _Path));

		// The containing repository bounds configuration discovery, matching Validate.
		auto Directory = CFile::fs_FileExists(_Path, EFileAttrib_Directory) ? _Path : CFile::fs_GetPath(_Path);
		auto Result = co_await NGit::fg_LaunchGitWithResult({"rev-parse", "--show-toplevel"}, Directory);
		if (Result.m_ExitCode)
			co_return CStr();

		co_return Result.f_GetStdOut().f_Trim();
	}

	CStr fg_NormalizeFormatPath(CStr const &_Path, CStr const &_WorkingDirectory)
	{
		return CFile::fs_CondensePath(CFile::fs_GetFullPath(_Path, _WorkingDirectory));
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

	// Runs the selected files across a bounded window of worker actors. The window keeps
	// several files in flight without holding every snapshot in memory at once.
	TCFuture<TCVector<CFormatJobResult>> fg_RunFormatJobs(TCVector<CFormatJob> _Jobs, umint _nJobs)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Running formatting jobs");

		TCVector<CFormatJobResult> Results;
		if (_Jobs.f_IsEmpty())
			co_return Results;

		auto nWorkers = fg_Min(_nJobs, _Jobs.f_GetLen());
		TCVector<TCActor<CFormatWorker>> Workers;
		for (umint i = 0; i < nWorkers; ++i)
			Workers.f_InsertLast(fg_ConstructActor<CFormatWorker>());

		auto DestroyWorkers = co_await fg_AsyncDestroy
			(
				[&Workers]() -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Destroying formatting workers");
					for (auto &Worker : Workers)
						co_await fg_Move(Worker).f_Destroy();

					co_return {};
				}
			)
		;

		TCVector<TCFuture<CFormatJobResult>> InFlight;
		umint nDispatched = 0;
		while (nDispatched < _Jobs.f_GetLen() && InFlight.f_GetLen() < nWorkers)
		{
			InFlight.f_InsertLast(Workers[InFlight.f_GetLen()](&CFormatWorker::f_Process, _Jobs[nDispatched]));
			++nDispatched;
		}

		Results.f_SetLen(_Jobs.f_GetLen());
		for (umint iJob = 0; iJob < _Jobs.f_GetLen(); ++iJob)
		{
			auto iSlot = iJob % InFlight.f_GetLen();
			auto Result = co_await fg_Move(InFlight[iSlot]).f_Wrap();
			if (Result)
				Results[iJob] = *Result;
			else
			{
				Results[iJob].m_Path = _Jobs[iJob].m_Path;
				Results[iJob].m_Outcome = EFormatOutcome::mc_Failed;
				Results[iJob].m_Report = "{}\n"_f << Result.f_GetExceptionStr();
			}

			if (nDispatched < _Jobs.f_GetLen())
			{
				InFlight[iSlot] = Workers[iSlot](&CFormatWorker::f_Process, _Jobs[nDispatched]);
				++nDispatched;
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
		TCVector<CStr> Candidates;
		TCSet<CStr> Seen;
		auto fSelect = [&](CStr const &_Path)
			{
				auto Path = fg_NormalizeFormatPath(_Path, WorkingDirectory);
				if (Seen.f_FindEqual(Path))
					return;

				Seen.f_Insert(Path);
				Candidates.f_Insert(Path);
			}
		;

		for (auto const &File : _Files)
		{
			auto Path = fg_NormalizeFormatPath(File, WorkingDirectory);
			if (CFile::fs_GetAttributesOnLink(Path) & EFileAttrib_Link)
				co_return DMibErrorInstance("Refusing to format the symbolic link '{}'"_f << Path);

			if (!CFile::fs_FileExists(Path, EFileAttrib_File))
				co_return DMibErrorInstance("'{}' is not an existing regular file"_f << Path);

			fSelect(Path);
		}

		for (auto const &Pattern : _Patterns)
		{
			auto Search = fg_NormalizeFormatPath(Pattern, WorkingDirectory);
			// Directory symbolic links are not traversed, so a pattern cannot escape its tree.
			for (auto const &Found : CFile::fs_FindFilesEx(Search, EFileAttrib_File, _bRecursive, false))
			{
				if (Found.m_Attribs & (EFileAttrib_Link | EFileAttrib_Directory))
					continue;

				fSelect(Found.m_Path);
			}
		}

		if (Candidates.f_IsEmpty())
			co_return DMibErrorInstance("No files matched the requested selection");

		// Directory enumeration order is not stable, so selection order is, keeping
		// diagnostics identical between runs and between job counts.
		Candidates.f_Sort();

		bool bHasRange = _Options.m_iFirstLine || !_Options.m_ByteRanges.f_IsEmpty();
		if (bHasRange && Candidates.f_GetLen() != 1)
			co_return DMibErrorInstance("Range options require exactly one selected file; {} were selected"_f << Candidates.f_GetLen());

		// Configuration discovery is bounded by each file's own repository, so a selection
		// spanning nested repositories resolves every file against its own root.
		TCVector<CStr> Roots;
		TCVector<TCVector<CStr>> Grouped;
		for (auto const &Path : Candidates)
		{
			auto Root = co_await fg_ResolveFormatRoot(Path);
			if (!Root)
				Root = WorkingDirectory;

			auto Relative = CFile::fs_MakePathRelative(Path, Root);
			if (!Relative || Relative.f_StartsWith(".."))
				co_return DMibErrorInstance("'{}' is outside the configuration root '{}'; use --working-directory to supply a suitable root"_f << Path << Root);

			umint iRoot = 0;
			while (iRoot < Roots.f_GetLen() && Roots[iRoot] != Root)
				++iRoot;

			if (iRoot == Roots.f_GetLen())
			{
				Roots.f_Insert(Root);
				Grouped.f_Insert();
			}

			Grouped[iRoot].f_Insert(Path);
		}

		CFormatSummary Summary;
		Summary.m_nSelected = Candidates.f_GetLen();
		for (umint iRoot = 0; iRoot < Roots.f_GetLen(); ++iRoot)
		{
			TCActor<NDevelop::CEditorConfigResolver> Configurations = fg_Construct(Roots[iRoot]);
			auto DestroyConfigurations = co_await fg_AsyncDestroy(Configurations);

			TCVector<CFormatJob> Jobs;
			for (auto const &Path : Grouped[iRoot])
			{
				auto Properties = co_await Configurations(&NDevelop::CEditorConfigResolver::f_Resolve, Path);
				CCodeFormattingSettings Settings(Properties);
				if (!Settings.f_IsFormattingEnabled() || fg_DetectCodeLanguage(Path) != ECodeLanguage::mc_Cpp)
				{
					++Summary.m_nExcluded;
					continue;
				}

				auto &Job = Jobs.f_Insert();
				Job.m_Path = Path;
				Job.m_DisplayPath = CFile::fs_MakePathRelative(Path, Roots[iRoot]);
				Job.m_Settings = Settings;
				Job.m_ByteRanges = _Options.m_ByteRanges;
				Job.m_iFirstLine = _Options.m_iFirstLine;
				Job.m_iLastLine = _Options.m_iLastLine;
				Job.m_RangePolicy = _Options.m_RangePolicy;
				Job.m_Mode = _Options.m_Mode;
			}

			// The coordinator owns ordering, so parallel runs report exactly like --jobs 1.
			for (auto const &Result : co_await fg_RunFormatJobs(fg_Move(Jobs), _Options.m_nJobs))
			{
				if (Result.m_Report)
					*_pCommandLine %= Result.m_Report;

				if (Result.m_Patch)
					*_pCommandLine += Result.m_Patch;

				Summary.m_nUnresolved += Result.m_nUnresolved;
				switch (Result.m_Outcome)
				{
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
							, "Description"_o= "Also match patterns in descendant directories.\n"
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
