// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Format.h"
#include "Malterlib_Tool_App_MTool_Validate.h"

#include <Mib/Git/Helpers/Launch>
#include <Mib/Git/Ignore>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Container/Map>
#include <Mib/Container/Set>
#include <Mib/Core/OnScopeExitCatch>
#include <Mib/Cryptography/RandomID>
#include <Mib/String/Appender>
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
		// A text starting with a NUL reads as empty, which its length does not.
		if (!_Text.f_GetLen())
			return 0;

		auto nLines = _Lines.f_GetLineCount();
		auto const &Last = _Lines.f_GetLine(nLines - 1);
		if (nLines > 1 && !Last.m_nLength && Last.m_Ending == ETextLineEnding::mc_None)
			--nLines;

		return nLines;
	}

	// A path in a patch header as git writes one: quoted with C escapes when it holds a quote, a backslash or a control
	// character, which would otherwise end or split the header.
	CStr fg_QuoteGitPatchPath(CStr const &_Path)
	{
		bool bQuote = false;
		for (auto pChar = _Path.f_GetStr(); *pChar; ++pChar)
			bQuote |= *pChar == '"' || *pChar == '\\' || uch8(*pChar) < 0x20 || *pChar == 0x7F;

		if (!bQuote)
			return _Path;

		CStr Quoted = "\"";
		for (auto pChar = _Path.f_GetStr(); *pChar; ++pChar)
		{
			auto Char = uch8(*pChar);
			if (Char == '"' || Char == '\\')
			{
				Quoted.f_AddChar('\\');
				Quoted.f_AddChar(ch8(Char));
			}
			else if (Char == '\t')
				Quoted += "\\t";
			else if (Char == '\n')
				Quoted += "\\n";
			else if (Char < 0x20 || Char == 0x7F)
			{
				Quoted.f_AddChar('\\');
				Quoted.f_AddChar(ch8('0' + (Char >> 6)));
				Quoted.f_AddChar(ch8('0' + ((Char >> 3) & 7)));
				Quoted.f_AddChar(ch8('0' + (Char & 7)));
			}
			else
				Quoted.f_AddChar(ch8(Char));
		}

		return Quoted + "\"";
	}

	void fg_AppendDiffLines(CStr &o_Patch, CStr const &_Marker, CStr const &_Text, CTextLineMap const &_Lines, umint _iFirst, umint _iLast)
	{
		auto pText = _Text.f_GetStr();

		// A line is appended as the bytes it holds: appended as text, one starting with a NUL would read as empty.
		auto fAppendBytes = [&](ch8 const *_pData, umint _nBytes)
			{
				umint nLength = o_Patch.f_GetLen();
				auto pPatch = o_Patch.f_GetStr(nLength + _nBytes + 1);
				NMemory::fg_MemCopy(pPatch + nLength, _pData, _nBytes);
				pPatch[nLength + _nBytes] = 0;
				o_Patch.f_SetStrLen(nLength + _nBytes);
			}
		;

		for (umint iLine = _iFirst; iLine <= _iLast; ++iLine)
		{
			auto const &Line = _Lines.f_GetLine(iLine);
			o_Patch += _Marker;
			fAppendBytes(pText + Line.m_iOffset, Line.m_nLength);
			// git's records are LF-delimited, so a CRLF line keeps its CR. Lines ending in a lone CR never get here.
			if (Line.m_Ending == ETextLineEnding::mc_CRLF)
				o_Patch += "\r";

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

		TCVector<uint8> TouchedLines;
		TouchedLines.f_SetLen(nPrintable);
		for (auto &Value : TouchedLines)
			Value = 0;

		auto fEditLines = [&](CCodeFormattingEdit const &_Edit, umint &o_iFirst, umint &o_iLast)
			{
				o_iFirst = fg_Min(Lines.f_FindLine(_Edit.m_iOffset), nPrintable - 1);
				o_iLast = fg_Min(Lines.f_FindLine(_Edit.f_GetEnd() > _Edit.m_iOffset ? _Edit.f_GetEnd() - 1 : _Edit.m_iOffset), nPrintable - 1);

				// An edit that removes a line's ending joins the next line to it, which the replacement has to hold.
				auto const &LastLine = Lines.f_GetLine(o_iLast);
				if (_Edit.f_GetEnd() > LastLine.m_iOffset + LastLine.m_nLength && o_iLast + 1 < nPrintable)
					++o_iLast;
			}
		;

		for (auto const &Edit : _Edits)
		{
			umint iFirst = 0;
			umint iLast = 0;
			fEditLines(Edit, iFirst, iLast);
			for (umint i = iFirst; i <= iLast; ++i)
				TouchedLines[i] = 1;
		}

		struct CHunk
		{
			umint m_iFirst = 0;
			umint m_iLast = 0;
		};

		TCVector<CHunk> Hunks;
		for (umint iLine = 0; iLine < nPrintable; ++iLine)
		{
			if (!TouchedLines[iLine])
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

		// A name with a space ends at a tab, as git writes it, so what follows the space is not read as a timestamp.
		CStr NameEnd = _Path.f_Find(" ") >= 0 ? "\t" : "";
		CStr Patch = "--- {}{}\n+++ {}{}\n"_f << fg_QuoteGitPatchPath("a/" + _Path) << NameEnd << fg_QuoteGitPatchPath("b/" + _Path) << NameEnd;
		aint nDelta = 0;
		umint iNextEdit = 0;

		for (auto const &Hunk : Hunks)
		{
			CStr Body;
			umint nOld = 0;
			umint nNew = 0;
			umint iLine = Hunk.m_iFirst;

			while (iLine <= Hunk.m_iLast)
			{
				if (!TouchedLines[iLine])
				{
					fg_AppendDiffLines(Body, " ", _Source, Lines, iLine, iLine);
					++nOld;
					++nNew;
					++iLine;

					continue;
				}

				auto iSegmentLast = iLine;
				while (iSegmentLast + 1 <= Hunk.m_iLast && TouchedLines[iSegmentLast + 1])
					++iSegmentLast;

				auto iStart = Lines.f_GetLineStart(iLine);
				auto iEnd = Lines.f_GetLineEnd(iSegmentLast);
				TCVector<CCodeFormattingEdit> Local;

				// The edits are ordered and every one lies within the segment its lines are in, so the segments take them in
				// turn.
				for (; iNextEdit < _Edits.f_GetLen(); ++iNextEdit)
				{
					auto const &Edit = _Edits[iNextEdit];
					umint iFirst = 0;
					umint iLast = 0;
					fEditLines(Edit, iFirst, iLast);
					if (iFirst > iSegmentLast)
						break;

					if (iFirst < iLine || iLast > iSegmentLast)
						continue;

					auto Rebased = Edit;
					Rebased.m_iOffset -= iStart;
					Local.f_Insert(Rebased);
				}

				// A segment can start with a NUL, which a string built from a pointer would end at.
				CStr Original = fg_StrFromBytes(_Source.f_GetStr() + iStart, iEnd - iStart);
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
	void fg_DescribeDiagnostics(CFormatJob const &_Job, CStr const &_Source, TCVector<CCodeFormattingDiagnostic> const &_Diagnostics, CFormatJobResult &o_Result)
	{
		bool bFixing = _Job.m_Mode == EFormatMode::mc_Write || _Job.m_Mode == EFormatMode::mc_Diff || _Job.m_Mode == EFormatMode::mc_Output;
		for (auto const &Diagnostic : _Diagnostics)
		{
			// A diagnostic is reported where a changed line is any line of its span, not only the one it starts on.
			bool bReported = fg_IsReportedLine(_Job.m_ReportedLines, Diagnostic.m_iLine);
			auto iLine = Diagnostic.m_iLine;
			auto nSource = umint(_Source.f_GetLen());
			auto pSource = _Source.f_GetStr();
			auto iEnd = fg_Min(Diagnostic.m_iOffset + Diagnostic.m_nLength, nSource);
			for (auto i = Diagnostic.m_iOffset; i < iEnd && !bReported; ++i)
			{
				// Lines end in LF, CRLF or a lone CR alike. A line the span only ends in front of is no line of it.
				bool bLineEnd = pSource[i] == '\n' || (pSource[i] == '\r' && (i + 1 >= nSource || pSource[i + 1] != '\n'));
				if (bLineEnd && i + 1 < iEnd)
					bReported = fg_IsReportedLine(_Job.m_ReportedLines, ++iLine);
			}

			if (!bReported)
				continue;

			if (bFixing && Diagnostic.m_bHasAutomaticFix)
				continue;

			++o_Result.m_nReported;
			o_Result.m_nUnresolved += !Diagnostic.m_bHasAutomaticFix;
			o_Result.m_Report += "{}:{}:{}: {}: {}\n"_f << _Job.m_Path << Diagnostic.m_iLine << Diagnostic.m_iColumn << Diagnostic.m_Rule << Diagnostic.m_Explanation;
		}
	}

	// The bytes as they are, NULs included: appending a pointer and a length takes nothing when the first byte is a NUL.
	CStr fg_StrFromBytes(void const *_pData, umint _nBytes)
	{
		CStr Text;
		auto pText = Text.f_GetStr(_nBytes + 1);
		NMemory::fg_MemCopy(pText, _pData, _nBytes);
		pText[_nBytes] = 0;
		Text.f_SetStrLen(_nBytes);

		return Text;
	}

	void fg_NormalizeValidationNuls(CStr &_Text)
	{
		auto pStart = _Text.f_GetStrUniqueWritable();
		auto pEnd = pStart + _Text.f_GetLen();

		for (auto pParse = pStart; pParse != pEnd; ++pParse)
		{
			if (*pParse == 0)
				*pParse = ' ';
		}
	}

	bool fg_ValidateLineLength(CStr const &_Line, CStr const &_AbsolutePath, umint _LineNumber, CCodeFormattingSettings const &_Settings, CStr &o_Report)
	{
		if (!_Settings.m_nMaxColumns)
			return true;

		// The engine owns the shared column model, including tab stops, Unicode accounting,
		// and malformed-byte handling. File-leading BOM removal is handled by the caller.
		umint nColumns = 0;
		if (!fg_MeasureTextColumns(_Line, _Settings.m_nTabWidth, nColumns))
		{
			o_Report += "{}:{}: line length overflows the column counter and exceeds max_line_length = {}\n"_f << _AbsolutePath << _LineNumber << _Settings.m_nMaxColumns;

			return false;
		}

		if (nColumns <= _Settings.m_nMaxColumns)
			return true;

		o_Report += "{}:{}: line length {} exceeds max_line_length = {}\n"_f << _AbsolutePath << _LineNumber << nColumns << _Settings.m_nMaxColumns;

		return false;
	}

	namespace
	{
		// The directive a line holds and nothing else, its comment markers removed, or an empty string.
		CStr fg_PlainFormatDirective(CStr const &_Line)
		{
			auto Text = _Line.f_Trim();
			bool bComment = false;
			for (auto pOpener : {"<!--", "/*", "//", "--", "#", ";"})
			{
				if (Text.f_StartsWith(pOpener))
				{
					Text = Text.f_Extract(NStr::fg_StrLen(pOpener));
					bComment = true;

					break;
				}
			}

			// Only a comment holds a directive; the words alone are text.
			if (!bComment)
				return {};

			for (auto pCloser : {"-->", "*/"})
			{
				if (Text.f_EndsWith(pCloser))
				{
					Text = Text.f_Left(Text.f_GetLen() - NStr::fg_StrLen(pCloser));
					break;
				}
			}

			Text = Text.f_Trim();
			if (Text == "malterlib-format off" || Text == "malterlib-format on")
				return Text;

			return {};
		}
	}

	// A file the engine does not format turns the line length check off and on with the engine's directives, written
	// in its own comment syntax on lines of their own: '# malterlib-format off'. The directives' lines and the lines
	// between them are not checked, and an unmatched or nested directive is a violation.
	bool CPlainFormatDirectives::f_IsDisabled(CStr const &_Line, CStr const &_AbsolutePath, umint _LineNumber, CStr &o_Report, umint &o_nErrors)
	{
		auto Directive = fg_PlainFormatDirective(_Line);
		if (Directive == "malterlib-format off")
		{
			if (m_iOffLine)
			{
				o_Report += "{}:{}: nested 'malterlib-format off' directive\n"_f << _AbsolutePath << _LineNumber;
				++o_nErrors;
			}
			else
				m_iOffLine = _LineNumber;

			return true;
		}

		if (Directive == "malterlib-format on")
		{
			if (!m_iOffLine)
			{
				o_Report += "{}:{}: 'malterlib-format on' has no matching 'malterlib-format off'\n"_f << _AbsolutePath << _LineNumber;
				++o_nErrors;
			}

			m_iOffLine = 0;

			return true;
		}

		return m_iOffLine != 0;
	}

	void CPlainFormatDirectives::f_Finish(CStr const &_AbsolutePath, CStr &o_Report, umint &o_nErrors)
	{
		if (!m_iOffLine)
			return;

		o_Report += "{}:{}: 'malterlib-format off' has no matching 'malterlib-format on'\n"_f << _AbsolutePath << m_iOffLine;
		++o_nErrors;

		m_iOffLine = 0;
	}

	struct CFormatRead
	{
		CStr m_Contents;
		bool m_bEmpty = true;
		bool m_bNul = false;     // A NUL among the first 8000 bytes.
		bool m_bDecoded = false; // Read as UTF-16 or UTF-32 text, whose signature the decoding took out.
	};

	// Every job is queued at once and a job that awaits lets the worker start the next, so a
	// blocking actor checked out per job would mean a thread per file. The run's shared set
	// bounds that to its capacity, whatever the number of workers and resolvers.
	CFormatWorker::CFormatWorker
		(
			TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors
			, TCActor<CFormatStyleResolver> const &_StyleResolver
			, TCActor<CFormatAnalyzers> const &_Analyzers
		)
		: mp_pBlockingActors(_pBlockingActors)
		, mp_StyleResolver(_StyleResolver)
		, mp_Analyzers(_Analyzers)
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

	bool CFormatJob::f_IsCancelled() const
	{
		return (m_pCancelled && m_pCancelled->f_Load()) || (m_pStopped && m_pStopped->f_Load());
	}

	TCFuture<CFormatJobResult> CFormatWorker::f_Process(CFormatJob _Job)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Formatting '{}'"_f << _Job.m_Path));

		CFormatJobResult Result;
		Result.m_Path = _Job.m_Path;

		// A job queued behind others may only start once the run is cancelled.
		if (_Job.f_IsCancelled())
			co_return Result;

		if (_Job.m_bResolveSettings)
		{
			// The opt-in property decides what is formatted.
			auto pConfigurations = mp_Configurations.f_FindEqual(_Job.m_Root);
			if (!pConfigurations)
			{
				pConfigurations = &mp_Configurations[_Job.m_Root];
				*pConfigurations = fg_Construct(_Job.m_Root, mp_pBlockingActors);
			}

			auto Properties = co_await (*pConfigurations)(&NDevelop::CEditorConfigResolver::f_Resolve, _Job.m_Path);
			_Job.m_Settings = CCodeFormattingSettings(Properties);

			bool bLineLength = _Job.m_bValidateLineLength && _Job.m_Settings.m_nMaxColumns;
			if (!_Job.m_Settings.f_IsFormattingEnabled() && !bLineLength)
			{
				Result.m_Outcome = EFormatOutcome::mc_Excluded;

				co_return Result;
			}

			_Job.m_DisplayPath = CFile::fs_MakePathRelative(_Job.m_Path, _Job.m_PatchRoot ? _Job.m_PatchRoot : _Job.m_Root);
		}

		auto Snapshot = _Job.m_Source;
		bool bEmpty = !_Job.m_bHasSource;
		bool bNul = false;
		bool bDecoded = false;

		// A file the attributes call binary is excluded from an audit unread.
		if (_Job.m_bValidateLineLength && _Job.m_TextAttribute == NGit::EGitTextAttribute::mc_Binary)
		{
			Result.m_Outcome = EFormatOutcome::mc_Excluded;

			co_return Result;
		}

		if (!_Job.m_bHasSource)
		{
			// A string treats a leading NUL as its end, so an audit, which may meet a text
			// file with NULs, judges binariness on the bytes and, when it only checks line
			// lengths, reads them with NULs as spaces, which occupy one column just as they
			// do. A file the engine formats keeps its bytes, as a plain format run reads it.
			bool bNormalizeNuls = _Job.m_bValidateLineLength && !_Job.m_Settings.f_IsFormattingEnabled();
			auto Read = co_await
				(
					g_Dispatch(mp_pBlockingActors->f_Next()) / [Path = _Job.m_Path, bNormalizeNuls]() -> CFormatRead
					{
						auto Data = CFile::fs_ReadFile(Path);

						CFormatRead Read;
						Read.m_bEmpty = Data.f_IsEmpty();
						auto nCheck = fg_Min(umint(Data.f_GetLen()), umint(8000));
						for (umint i = 0; i < nCheck && !Read.m_bNul; ++i)
							Read.m_bNul = !Data[i];

						// A UTF-16 or UTF-32 signature names an encoding whose NULs are halves of characters: the text is decoded
						// at the file boundary as any read of a text file decodes it.
						auto nData = Data.f_GetLen();
						bool bUtf16 = nData >= 2 && ((Data[0] == 0xFF && Data[1] == 0xFE) || (Data[0] == 0xFE && Data[1] == 0xFF));
						bool bUtf32 = nData >= 4 && Data[0] == 0x00 && Data[1] == 0x00 && Data[2] == 0xFE && Data[3] == 0xFF;
						if (bNormalizeNuls && (bUtf16 || bUtf32))
						{
							// A U+0000 the text holds would end the decoded string, so it is checked as a space, as a NUL in bytes
							// read as text is.
							bool bUtf32Little = nData >= 4 && Data[0] == 0xFF && Data[1] == 0xFE && Data[2] == 0x00 && Data[3] == 0x00;
							bool bBigEndian = Data[0] == 0x00 || Data[0] == 0xFE;
							umint nUnit = bUtf32 || bUtf32Little ? 4 : 2;
							for (umint iUnit = nUnit; iUnit + nUnit <= nData; iUnit += nUnit)
							{
								bool bNul = true;
								for (umint iByte = 0; iByte < nUnit; ++iByte)
									bNul = bNul && !Data[iUnit + iByte];

								if (bNul)
									Data[bBigEndian ? iUnit + nUnit - 1 : iUnit] = ' ';
							}

							NStream::CBinaryStreamMemory<> Stream;
							Stream.f_Open(fg_Move(Data));
							Read.m_Contents = fg_ReadTextStream(Stream);
							Read.m_bDecoded = true;

							return Read;
						}

						if (bNormalizeNuls)
						{
							for (auto &Byte : Data)
							{
								if (!Byte)
									Byte = ' ';
							}
						}

						Read.m_Contents = fg_StrFromBytes(Data.f_GetArray(), Data.f_GetLen());

						return Read;
					}
				)
			;

			Snapshot = fg_Move(Read.m_Contents);
			bEmpty = Read.m_bEmpty;
			bNul = Read.m_bNul;
			bDecoded = Read.m_bDecoded;
		}

		if (_Job.m_bValidateLineLength)
		{
			// The attribute decides text or binary where it speaks and git's rule, a NUL
			// among the first 8000 bytes, where it does not; an empty file has no line to
			// check either way.
			bool bBinary = _Job.m_TextAttribute == NGit::EGitTextAttribute::mc_Binary || (_Job.m_TextAttribute == NGit::EGitTextAttribute::mc_Unspecified && bNul);
			if (bBinary || bEmpty)
			{
				Result.m_Outcome = EFormatOutcome::mc_Excluded;

				co_return Result;
			}

			if (!_Job.m_Settings.f_IsFormattingEnabled())
			{
				umint iLine = 0;
				CPlainFormatDirectives Directives;

				// The file's UTF-8 signature is no column of its first line. Decoded text has had its signature taken out, and a
				// U+FEFF left at its start is a character of the text.
				for (auto const &Line : (bDecoded ? Snapshot : Snapshot.f_RemovePrefix("\xEF\xBB\xBF")).f_SplitLine())
				{
					++iLine;
					if (!Directives.f_IsDisabled(Line, _Job.m_Path, iLine, Result.m_Report, Result.m_nLineErrors))
						Result.m_nLineErrors += !fg_ValidateLineLength(Line, _Job.m_Path, iLine, _Job.m_Settings, Result.m_Report);
				}

				Directives.f_Finish(_Job.m_Path, Result.m_Report, Result.m_nLineErrors);

				co_return Result;
			}
		}

		Result.m_bFormatted = true;

		CCodeFormattingRequest Request;
		Request.m_Source = Snapshot;
		Request.m_Path = _Job.m_Path;
		Request.m_Settings = _Job.m_Settings;
		Request.m_Ranges = _Job.m_ByteRanges;
		Request.m_RangePolicy = _Job.m_RangePolicy;

		auto Directory = CFile::fs_GetPath(_Job.m_Path);
		if (_Job.m_bHasStyle)
			Request.m_pStyle = _Job.m_pStyle;
		else if (auto pKnown = mp_Styles.f_FindEqual(Directory))
			Request.m_pStyle = *pKnown;
		else
		{
			Request.m_pStyle = co_await mp_StyleResolver(&CFormatStyleResolver::f_Resolve, Directory);
			mp_Styles(Directory, Request.m_pStyle);
		}

		if (_Job.m_iFirstLine)
		{
			CTextLineMap Lines(Snapshot);
			auto nPrintable = fg_GetPrintableLineCount(Lines, Snapshot);
			if (_Job.m_iLastLine > nPrintable)
			{
				Result.m_Outcome = EFormatOutcome::mc_Failed;
				Result.m_Report = "{}: --lines {}:{} is outside the file's {} line(s)\n"_f << _Job.m_Path << _Job.m_iFirstLine << _Job.m_iLastLine << nPrintable;

				co_return Result;
			}

			auto &Range = Request.m_Ranges.f_Insert();
			Range.m_iOffset = Lines.f_GetLineStart(_Job.m_iFirstLine - 1);
			Range.m_nLength = Lines.f_GetLineEnd(_Job.m_iLastLine - 1) - Range.m_iOffset;
		}

		// The analysis runs off the threads the workers, the pool and the resolvers take turns on: a file analyzed for
		// seconds there would hold every job queued behind it.
		auto Analyzed = co_await mp_Analyzers(&CFormatAnalyzers::f_Analyze, fg_Move(Request)).f_Wrap();
		if (!Analyzed)
		{
			Result.m_Outcome = EFormatOutcome::mc_Failed;
			Result.m_Report = "{}: {}\n"_f << _Job.m_Path << Analyzed.f_GetExceptionStr();

			co_return Result;
		}

		auto &Analysis = Analyzed->m_Result;
		Result.m_Seconds = Analyzed->m_Seconds;
		if (Analysis.m_Status != ECodeFormattingStatus::mc_Complete)
		{
			// Selection already proved the file is opted in, so an unsupported result here
			// means the source itself could not be analyzed safely.
			Result.m_Outcome = EFormatOutcome::mc_Failed;
			Result.m_Report = "{}: {}\n"_f << _Job.m_Path << Analysis.m_Explanation;

			co_return Result;
		}

		fg_DescribeDiagnostics(_Job, Snapshot, Analysis.m_Diagnostics, Result);
		if (Analysis.m_Edits.f_IsEmpty() || (_Job.m_Mode == EFormatMode::mc_Report && !Result.m_nReported))
		{
			Result.m_Outcome = EFormatOutcome::mc_Unchanged;

			co_return Result;
		}

		Result.m_Outcome = EFormatOutcome::mc_Changed;

		if (_Job.m_Mode == EFormatMode::mc_Diff)
		{
			// git reads a lone CR as part of a line, so lines that end in one have no records a patch could show.
			auto fHasLoneCR = [](CStr const &_Text)
				{
					auto pText = _Text.f_GetStr();
					for (umint i = 0; i < _Text.f_GetLen(); ++i)
					{
						if (pText[i] == '\r' && (i + 1 == _Text.f_GetLen() || pText[i + 1] != '\n'))
							return true;
					}

					return false;
				}
			;

			if (fHasLoneCR(Snapshot) || fHasLoneCR(fg_ApplyCodeFormattingEdits(Snapshot, Analysis.m_Edits)))
			{
				Result.m_Outcome = EFormatOutcome::mc_Failed;
				Result.m_Report += "{}: --diff cannot show lines that end in a lone CR\n"_f << _Job.m_Path;

				co_return Result;
			}

			Result.m_Patch = fg_BuildFormatPatch(_Job.m_DisplayPath, Snapshot, Analysis.m_Edits);

			co_return Result;
		}

		if (_Job.m_Mode == EFormatMode::mc_Output)
		{
			Result.m_Formatted = fg_ApplyCodeFormattingEdits(Snapshot, Analysis.m_Edits);

			co_return Result;
		}

		if (_Job.m_Mode != EFormatMode::mc_Write)
			co_return Result;

		if (_Job.f_IsCancelled())
		{
			Result.m_Outcome = EFormatOutcome::mc_Unchanged;

			co_return Result;
		}

		struct CWritten
		{
			CStr m_Error;
			bool m_bCancelled = false;
		};

		auto Formatted = fg_ApplyCodeFormattingEdits(Snapshot, Analysis.m_Edits);
		auto Written = co_await
			(
				g_Dispatch(mp_pBlockingActors->f_Next()) / [Path = _Job.m_Path, Snapshot, Formatted, pCancelled = _Job.m_pCancelled, pStopped = _Job.m_pStopped]() -> CWritten
				{
					auto fCancelled = [&]
						{
							return (pCancelled && pCancelled->f_Load()) || (pStopped && pStopped->f_Load());
						}
					;

					// A write queued behind others may only start once the run is cancelled.
					if (fCancelled())
						return {.m_bCancelled = true};

					// Never overwrite an intervening edit: the file must still be the regular file whose bytes were analyzed,
					// checked once the replacement is staged, so only an edit between the last read and the rename can be lost.
					auto fChanged = [&]() -> CStr
						{
							if (!CFile::fs_FileExists(Path, EFileAttrib_File) || CFile::fs_FileExists(Path, EFileAttrib_Link | EFileAttrib_Directory))
								return "is no longer a regular file";

							auto Current = CFile::fs_ReadFile(Path);
							if (Current.f_GetLen() != Snapshot.f_GetLen() || NMemory::fg_MemCmp(Current.f_GetArray(), (uint8 const *)Snapshot.f_GetStr(), Current.f_GetLen()) != 0)
								return "changed on disk during formatting";

							return {};
						}
					;

					if (auto Changed = fChanged())
						return {.m_Error = Changed};

					auto Attributes = CFile::fs_GetAttributes(Path);

					// The staging file is this run's own: a fresh name, created only where none exists, so nothing a user keeps
					// beside the file is overwritten or removed.
					CStr Temporary = "{}.mtool-format-{}"_f << Path << NCryptography::fg_FastRandomID(12);
					bool bCreated = false;

					auto Cleanup = g_OnScopeExitCatch / [&Temporary, &bCreated]
						{
							if (bCreated && CFile::fs_FileExists(Temporary, EFileAttrib_File))
								CFile::fs_DeleteFile(Temporary);
						}
					;

					{
						CFile File;
						// Readable by its owner alone until it takes the file's permissions, which can be as private.
						File.f_Open(Temporary, EFileOpen_Write | EFileOpen_DontOpenExisting, EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_UserWrite);
						bCreated = true;
						File.f_Write(Formatted.f_GetStr(), Formatted.f_GetLen());
						File.f_Close();
					}

					// The permission bits only apply when marked valid. Out of scope by decision: an access control list, extended
					// attributes and the owner are not carried over to the replacement, which git keeps none of in a checkout.
					CFile::fs_SetAttributes(Temporary, Attributes | EFileAttrib_UnixAttributesValid);
					if (auto Changed = fChanged())
						return {.m_Error = Changed};

					if (fCancelled())
						return {.m_bCancelled = true};

					CFile::fs_RenameFile(Temporary, Path);
					bCreated = false;

					return {};
				}
			)
		;
		if (Written.m_bCancelled)
			Result.m_Outcome = EFormatOutcome::mc_Unchanged;
		else if (Written.m_Error)
		{
			Result.m_Outcome = EFormatOutcome::mc_Failed;
			Result.m_Report += "{}: {}\n"_f << _Job.m_Path << Written.m_Error;
		}

		co_return Result;
	}

	umint fg_GetDefaultFormatJobs()
	{
		return fg_Min(fg_Max(umint(NSys::fg_Thread_GetVirtualCores()), umint(1)), umint(16));
	}

	// The root is spelled from the directory, as the files below it are: git names the top level by its physical path, which
	// differs where a symbolic link leads to the repository, as '/tmp' does on macOS.
	TCFuture<CStr> CFormatRootResolver::fp_AskGit(CStr _Directory)
	{
		// A git that cannot be launched finds no repository, as one that finds none does.
		auto Launched = co_await NGit::fg_LaunchGitWithResult({"rev-parse", "--show-cdup"}, _Directory).f_Wrap();
		if (!Launched || Launched->m_ExitCode)
			co_return CStr();

		auto &Result = *Launched;

		auto Up = Result.f_GetStdOut().f_Trim();
		co_return Up ? CFile::fs_CondensePath(_Directory / Up) : _Directory;
	}

	CFormatRootResolver::CFormatRootResolver(TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors)
		: mp_pBlockingActors(_pBlockingActors)
	{
	}

	TCFuture<CStr> CFormatRootResolver::f_Resolve(CStr _Directory)
	{
		// Walk up to a directory that is known or being resolved, or to the top. Every directory passed on the way up to the
		// first that holds a repository of its own shares that directory's answer.
		TCVector<CStr> Pending;
		auto Walk = CFile::fs_CondensePath(_Directory);
		CStr Known; // Empty when the walk reached the top.

		while (true)
		{
			if (mp_Entries.f_FindEqual(Walk))
			{
				Known = Walk;

				break;
			}

			Pending.f_Insert(Walk);

			auto Parent = CFile::fs_CondensePath(CFile::fs_GetPath(Walk));
			if (!Parent || Parent == Walk)
				break;

			Walk = Parent;
		}

		if (!Pending.f_IsEmpty())
		{
			auto iRepository = co_await
				(
					g_Dispatch(mp_pBlockingActors->f_Next()) / [Pending]() -> umint
					{
						for (umint iDirectory = 0; iDirectory < Pending.f_GetLen(); ++iDirectory)
						{
							if (CFile::fs_FileExists(Pending[iDirectory] / ".git"))
								return iDirectory;
						}

						return Pending.f_GetLen();
					}
				)
			;

			if (iRepository < Pending.f_GetLen())
			{
				Pending.f_SetLen(iRepository + 1);
				Known.f_Clear();
			}

			// Resolves that went on meanwhile may have answered a directory passed.
			for (umint iDirectory = 0; iDirectory < Pending.f_GetLen(); ++iDirectory)
			{
				if (mp_Entries.f_FindEqual(Pending[iDirectory]))
				{
					Known = Pending[iDirectory];
					Pending.f_SetLen(iDirectory);

					break;
				}
			}
		}

		TCSharedPointer<CEntry> pEntry;
		if (Known)
		{
			auto pKnown = mp_Entries.f_FindEqual(Known);
			if (!pKnown)
				co_return co_await f_Resolve(fg_Move(_Directory)); // Its resolve failed meanwhile, which a later one retries.

			pEntry = *pKnown;
		}

		Walk = Pending.f_IsEmpty() ? CStr() : Pending.f_GetLast();

		// Publish the pending resolve before awaiting so reentrant resolves can join it.
		bool bAsk = !pEntry;
		if (bAsk)
			pEntry = fg_Construct();

		for (auto const &Directory : Pending)
			mp_Entries(Directory, pEntry);

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
			for (auto const &Directory : Pending)
				mp_Entries.f_Remove(Directory);
		}

		auto Waiters = fg_Move(pEntry->m_Waiters);
		for (auto &Promise : Waiters)
			Promise.f_SetResult(Result);

		co_return fg_Move(Result);
	}

	CFormatStyleResolver::CFormatStyleResolver(TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors, TCSharedPointer<CFormatStyleSnapshot> const &_pSnapshot)
		: mp_pBlockingActors(_pBlockingActors)
		, mp_pSnapshot(_pSnapshot)
	{
	}

	CStr const *CFormatStyleResolver::fp_FindInSnapshot(CStr const &_Path, bool &o_bInSnapshot) const
	{
		o_bInSnapshot = mp_pSnapshot && _Path.f_StartsWith(mp_pSnapshot->m_Root + "/");
		if (!o_bInSnapshot)
			return nullptr;

		return mp_pSnapshot->m_Documents.f_FindEqual(_Path);
	}

	enum class EFormatDocumentPresence
	{
		mc_Unknown // Asked of the file system.
		, mc_Present
		, mc_Absent
	};

	// The document named '_Name' in the directory or the nearest directory above it, or empty. Every directory passed on the
	// way shares the answer.
	TCFuture<CStr> CFormatStyleResolver::fp_FindDocument(CStr _Directory, CStr _Name)
	{
		bool bFallback = _Name != ".malterlib-format";
		TCVector<CStr> Pending;
		auto Walk = CFile::fs_CondensePath(_Directory);
		CStr Found;

		while (true)
		{
			if (auto pKnown = (bFallback ? mp_Fallbacks : mp_Documents).f_FindEqual(Walk))
			{
				Found = *pKnown;

				break;
			}

			Pending.f_Insert(Walk);

			auto Parent = CFile::fs_CondensePath(CFile::fs_GetPath(Walk));
			if (!Parent || Parent == Walk)
				break;

			Walk = Parent;
		}

		if (!Pending.f_IsEmpty())
		{
			// Per directory passed, whether the snapshot holds the document, or that the file system is asked.
			TCVector<CStr> Candidates;
			TCVector<EFormatDocumentPresence> Presence;
			bool bAsk = false;
			for (auto const &Directory : Pending)
			{
				auto &Candidate = Candidates.f_Insert(Directory / _Name);
				bool bInSnapshot = false;
				bool bSnapshotHolds = fp_FindInSnapshot(Candidate, bInSnapshot) != nullptr;
				Presence.f_Insert(!bInSnapshot ? EFormatDocumentPresence::mc_Unknown : bSnapshotHolds ? EFormatDocumentPresence::mc_Present : EFormatDocumentPresence::mc_Absent);
				bAsk = bAsk || !bInSnapshot;
			}

			auto fFindFirst = [](TCVector<CStr> const &_Candidates, TCVector<EFormatDocumentPresence> const &_Presence) -> umint
				{
					for (umint iCandidate = 0; iCandidate < _Candidates.f_GetLen(); ++iCandidate)
					{
						auto Presence = _Presence[iCandidate];
						bool bPresent = Presence == EFormatDocumentPresence::mc_Unknown ? CFile::fs_FileExists(_Candidates[iCandidate]) : Presence == EFormatDocumentPresence::mc_Present;
						if (bPresent)
							return iCandidate;
					}

					return _Candidates.f_GetLen();
				}
			;

			umint iFound = 0;
			if (bAsk)
			{
				iFound = co_await
					(
						g_Dispatch(mp_pBlockingActors->f_Next()) / [Candidates, Presence, fFindFirst]() -> umint
						{
							return fFindFirst(Candidates, Presence);
						}
					)
				;
			}
			else
				iFound = fFindFirst(Candidates, Presence);

			if (iFound < Pending.f_GetLen())
			{
				Pending.f_SetLen(iFound + 1);
				Found = Candidates[iFound];
			}

			// Finds that went on meanwhile may have answered a directory passed.
			auto &Cache = bFallback ? mp_Fallbacks : mp_Documents;
			for (umint iDirectory = 0; iDirectory < Pending.f_GetLen(); ++iDirectory)
			{
				if (auto pKnown = Cache.f_FindEqual(Pending[iDirectory]))
				{
					Found = *pKnown;
					Pending.f_SetLen(iDirectory);

					break;
				}
			}
		}

		auto &Cache = bFallback ? mp_Fallbacks : mp_Documents;
		for (auto const &Directory : Pending)
			Cache(Directory, Found);

		co_return fg_Move(Found);
	}

	TCFuture<TCSharedPointer<CCodeFormattingStyle const>> CFormatStyleResolver::f_Resolve(CStr _Directory)
	{
		auto Document = co_await fp_FindDocument(_Directory, ".malterlib-format");
		if (!Document)
			Document = co_await fp_FindDocument(_Directory, "Malterlib/Core/.malterlib-format");

		if (!Document)
			co_return TCSharedPointer<CCodeFormattingStyle const>();

		co_return co_await fp_Load(fg_Move(Document), {});
	}

	// Loads a document and the chain it extends, each file once. A document that is being
	// loaded is joined rather than read again, and one that extends itself is an error
	// rather than a wait on its own result. A pending load can belong to another resolve
	// that started elsewhere in the same cycle, so the bases pending loads wait on are
	// followed as well as this resolve's own chain.
	TCFuture<TCSharedPointer<CCodeFormattingStyle const>> CFormatStyleResolver::fp_Load(CStr _Path, TCVector<CStr> _Chain)
	{
		auto fInChain = [&](CStr const &_Candidate)
			{
				for (auto const &Loading : _Chain)
				{
					if (Loading == _Candidate)
						return true;
				}

				return false;
			}
		;

		if (fInChain(_Path))
			co_return DMibErrorInstance("'{}' extends itself"_f << _Path);

		if (auto pKnown = mp_Loads.f_FindEqual(_Path))
		{
			auto pLoad = *pKnown;
			if (pLoad->m_Result.f_IsSet())
				co_return fg_TempCopy(pLoad->m_Result);

			for (umint nSteps = mp_Loads.f_GetLen(), iStep = 0; iStep < nSteps; ++iStep)
			{
				auto pWaiting = mp_Loads.f_FindEqual((*pKnown)->m_Extending);
				if (!pWaiting || (*pWaiting)->m_Result.f_IsSet())
					break;

				auto &Extending = (*pKnown)->m_Extending;
				if (fInChain(Extending))
					co_return DMibErrorInstance("'{}' extends itself"_f << Extending);

				pKnown = pWaiting;
			}

			co_return co_await pLoad->m_Waiters.f_Insert().f_Future();
		}

		TCSharedPointer<CLoad> pLoad = fg_Construct();
		mp_Loads(_Path, pLoad);

		auto Result = co_await fp_Read(_Path, fg_Move(_Chain)).f_Wrap();

		pLoad->m_Result = Result;
		if (!Result)
			mp_Loads.f_Remove(_Path);

		auto Waiters = fg_Move(pLoad->m_Waiters);
		for (auto &Promise : Waiters)
			Promise.f_SetResult(Result);

		co_return fg_Move(Result);
	}

	TCFuture<TCSharedPointer<CCodeFormattingStyle const>> CFormatStyleResolver::fp_Read(CStr _Path, TCVector<CStr> _Chain)
	{
		CStr Yaml;
		bool bInSnapshot = false;
		if (auto pSnapshot = fp_FindInSnapshot(_Path, bInSnapshot))
			Yaml = *pSnapshot;
		else if (bInSnapshot)
		{
			// A document another names to extend, whatever its name, is read from the snapshot too.
			auto pBlob = mp_pSnapshot->m_Blobs.f_FindEqual(_Path);
			if (!pBlob)
				co_return DMibErrorInstance("'{}' is not in the validated snapshot"_f << _Path);

			Yaml = (co_await NGit::fg_LaunchGit({"cat-file", "blob", *pBlob}, mp_pSnapshot->m_GitDirectory, mp_pSnapshot->m_GitEnvironment)).f_RemovePrefix("\xEF\xBB\xBF");
		}
		else
		{
			Yaml = co_await
				(
					g_Dispatch(mp_pBlockingActors->f_Next()) / [_Path]() -> CStr
					{
						return CFile::fs_ReadStringFromFile(_Path, true);
					}
					% ("Reading '{}'"_f << _Path)
				)
			;
		}

		CCodeFormattingStyleDocument Document;
		{
			auto CaptureScope = co_await (g_CaptureExceptions % ("Reading '{}'"_f << _Path));
			Document = CCodeFormattingStyleDocument::fs_Parse(Yaml);
		}

		CCodeFormattingStyle Style;
		if (Document.m_Extends)
		{
			auto Base = CFile::fs_CondensePath(CFile::fs_GetFullPath(Document.m_Extends, CFile::fs_GetPath(_Path)));
			_Chain.f_Insert(_Path);
			if (auto pLoad = mp_Loads.f_FindEqual(_Path))
				(*pLoad)->m_Extending = Base;

			if (auto pBase = co_await fp_Load(fg_Move(Base), fg_Move(_Chain)))
				Style = *pBase;
		}

		Style.f_Apply(Document);

		co_return TCSharedPointer<CCodeFormattingStyle const>(fg_Construct(fg_Move(Style)));
	}

	CStr fg_NormalizeFormatPath(CStr const &_Path, CStr const &_WorkingDirectory)
	{
		return CFile::fs_CondensePath(CFile::fs_GetFullPath(_Path, _WorkingDirectory));
	}

	// Outside a repository the configuration of a selection is bounded by the working directory, or where the selection
	// lies outside it, by the deepest directory holding both.
	CStr fg_GetFormatBoundary(CStr const &_WorkingDirectory, CStr const &_Directory)
	{
		if (_Directory == _WorkingDirectory || _Directory.f_StartsWith(_WorkingDirectory + "/"))
			return _WorkingDirectory;

		auto Common = CFile::fs_GetCommonPath(_WorkingDirectory, _Directory);
		return Common ? CStr(Common.f_RemoveSuffix("/")) : CStr("/");
	}

	// Whether the name matches one of the upper case wildcards, matched the way the platform matches a find pattern.
	bool fg_MatchesFormatWildcards(CStr const &_Name, TCVector<CStr> const &_Wildcards, bool _bMatchAll)
	{
		if (_bMatchAll)
			return true;

		auto Name = _Name.f_UpperCase();
		for (auto const &Wildcard : _Wildcards)
		{
			if (fg_StrMatchWildcard(Name.f_GetStr(), Wildcard.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted)
				return true;
		}

		return false;
	}

	// A directory's contents, read on a blocking actor along with the ignore files in it.
	struct CFormatListing
	{
		umint m_iRepository = 0;
		CStr m_Directory;
		TCVector<CFile::CFoundFile> m_Entries;
		TCVector<uint64> m_Sizes;            // Per entry, the size of a file the walk's patterns match, or zero.
		TCVector<bool> m_HoldsRepository;    // Per entry, a directory holding a '.git' entry.
		bool m_bRepository = false;          // The directory holds a '.git' entry.
		TCOptional<CStr> m_Rules;            // The directory's own .gitignore.
		TCOptional<CStr> m_GlobalExcludes;   // The file core.excludesFile names, at a repository's root.
		TCOptional<CStr> m_Excludes;         // The repository's info/exclude, at its root.
		TCOptional<CStr> m_Attributes;       // The directory's own .gitattributes.
		TCOptional<CStr> m_InfoAttributes;   // The repository's info/attributes, at its root.
		TCOptional<CStr> m_GlobalAttributes; // The file core.attributesFile names, at a repository's root.
		bool m_bIgnoreCase = false;          // The repository's core.ignoreCase, at its root.
	};

	// One repository the walk has met: the ignore rules gathered on the way down, and a
	// configuration resolver bounded by its root.
	struct CFormatWalkRepository
	{
		CStr m_Root;
		bool m_bGit = false;         // False for the working directory standing in for no repository.
		bool m_bIgnoreCase = false;  // The repository's core.ignoreCase.
		NGit::CGitIgnore m_Ignore;
		NGit::CGitAttributes m_Attributes;
		TCSet<CStr> m_Tracked;       // The paths git tracks below those asked about, relative to the root, and the directories holding them.
		TCSet<CStr> m_Asked;         // The ignored paths git was asked about, relative to the root.
		TCActor<NDevelop::CEditorConfigResolver> m_Configurations;
	};

	struct CFormatWalkFound
	{
		CStr m_Path;
		uint64 m_Size = 0;
		umint m_iRoot = 0; // Index into the result's roots.
		NGit::EGitTextAttribute m_TextAttribute = NGit::EGitTextAttribute::mc_Unspecified;
	};

	struct CFormatWalkResult
	{
		TCVector<CFormatWalkFound> m_Found;
		TCVector<CStr> m_Roots;
	};

	// Walks the tree the patterns of one directory name, once for all of them, listing directories in parallel on a
	// bounded set of blocking actors. A directory is not entered when git ignores it, by its .gitignore files, its
	// info/exclude, or the excludes file its configuration names, or when a document above it disables formatting for
	// everything below it; a document deeper down that opts files back in is not seen then, which is the price of never
	// reading those trees. A repository checked out in an ignored directory is entered on request, with its own rules.
	// The listing order is not the selection order, which the caller settles by sorting.
	struct CFormatWalk : CActor
	{
		CFormatWalk
			(
				CStr _Directory
				, TCVector<CStr> _Wildcards
				, CStr _Root
				, bool _bGit
				, bool _bRecursive
				, bool _bNestedRepositories
				, bool _bOwnRepository
				, bool _bLineLength
				, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors
			)
		;

		TCFuture<CFormatWalkResult> f_Run();

	protected:
		TCFuture<void> fp_Destroy() override;

	private:
		TCFuture<CFormatListing> fp_List(umint _iRepository, CStr _Directory);
		TCFuture<void> fp_LoadRulesAbove(umint _iRepository, CStr _Directory);
		TCFuture<bool> fp_IsIgnored(umint _iRepository, CStr _Path, bool _bDirectory);
		bool fp_MayBeIgnored(umint _iRepository, CStr const &_Path, bool _bDirectory) const;
		TCFuture<void> fp_AskTracked(umint _iRepository, TCVector<CStr> _Relative);
		umint fp_AddRepository(CStr const &_Root, bool _bGit);
		void fp_AddRepositoryExcludes(umint _iRepository, CFormatListing const &_Listing);
		void fp_AddDirectoryRules(umint _iRepository, CFormatListing const &_Listing);
		bool fp_MatchesName(CStr const &_Name) const;

		CStr mp_Directory;
		TCVector<CStr> mp_Wildcards;                                  // Upper case, matched the way the platform matches a find pattern.
		bool mp_bMatchAll = false;
		bool mp_bRecursive = false;
		bool mp_bNestedRepositories = false;
		bool mp_bOwnRepository = false;
		bool mp_bLineLength = false;                                  // A max_line_length below keeps a directory open too.
		TCSharedPointer<CSharedRoundRobinBlockingActors> mp_pReaders; // The run's, shared with the other walks and the resolvers.
		TCVector<CFormatWalkRepository> mp_Repositories;
		TCSet<CStr> mp_TrackedOnly;                                   // Directories git ignores, entered for the paths in them it tracks.
		NGit::CGitEnvironment mp_GitEnvironment;
	};

	CFormatWalk::CFormatWalk
		(
			CStr _Directory
			, TCVector<CStr> _Wildcards
			, CStr _Root
			, bool _bGit
			, bool _bRecursive
			, bool _bNestedRepositories
			, bool _bOwnRepository
			, bool _bLineLength
			, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors
		)
		: mp_Directory(fg_Move(_Directory))
		, mp_bRecursive(_bRecursive)
		, mp_bNestedRepositories(_bNestedRepositories)
		, mp_bOwnRepository(_bOwnRepository)
		, mp_bLineLength(_bLineLength)
		, mp_pReaders(_pBlockingActors)
	{
		for (auto &Wildcard : _Wildcards)
		{
			mp_bMatchAll |= Wildcard == "*";
			mp_Wildcards.f_Insert(Wildcard.f_UpperCase());
		}

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
	// rules added later win, so the root's own .gitignore is added after these. Its
	// info/attributes outrank every .gitattributes instead.
	void CFormatWalk::fp_AddRepositoryExcludes(umint _iRepository, CFormatListing const &_Listing)
	{
		auto &Repository = mp_Repositories[_iRepository];
		if (_Listing.m_bRepository)
			Repository.m_bIgnoreCase = _Listing.m_bIgnoreCase;

		if (_Listing.m_GlobalExcludes)
			Repository.m_Ignore.f_AddRules({}, *_Listing.m_GlobalExcludes, Repository.m_bIgnoreCase);

		if (_Listing.m_Excludes)
			Repository.m_Ignore.f_AddRules({}, *_Listing.m_Excludes, Repository.m_bIgnoreCase);

		if (_Listing.m_InfoAttributes)
			Repository.m_Attributes.f_AddInfoRules(*_Listing.m_InfoAttributes, Repository.m_bIgnoreCase);

		// Added before any .gitattributes, every one of them takes precedence.
		if (_Listing.m_GlobalAttributes)
			Repository.m_Attributes.f_AddRules({}, *_Listing.m_GlobalAttributes, Repository.m_bIgnoreCase);
	}

	// A directory's own .gitignore and .gitattributes, relative to the repository root.
	void CFormatWalk::fp_AddDirectoryRules(umint _iRepository, CFormatListing const &_Listing)
	{
		auto &Repository = mp_Repositories[_iRepository];
		if (!Repository.m_bGit)
			return;

		auto Relative = CFile::fs_MakePathRelative(_Listing.m_Directory, Repository.m_Root);
		if (Relative == ".")
			Relative = {};

		if (_Listing.m_Rules)
			Repository.m_Ignore.f_AddRules(Relative, *_Listing.m_Rules, Repository.m_bIgnoreCase);

		if (_Listing.m_Attributes)
			Repository.m_Attributes.f_AddRules(Relative, *_Listing.m_Attributes, Repository.m_bIgnoreCase);
	}

	TCFuture<void> CFormatWalk::fp_Destroy()
	{
		for (auto &Repository : mp_Repositories)
			co_await fg_Move(Repository.m_Configurations).f_Destroy();

		co_return {};
	}

	bool CFormatWalk::fp_MatchesName(CStr const &_Name) const
	{
		return fg_MatchesFormatWildcards(_Name, mp_Wildcards, mp_bMatchAll);
	}

	TCFuture<CFormatListing> CFormatWalk::fp_List(umint _iRepository, CStr _Directory)
	{
		co_return co_await
			(
				g_Dispatch(mp_pReaders->f_Next())
				/ [iRepository = _iRepository, Directory = fg_Move(_Directory), Environment = mp_GitEnvironment, Wildcards = mp_Wildcards, bMatchAll = mp_bMatchAll]() -> CFormatListing
				{
					CFormatListing Listing;
					Listing.m_iRepository = iRepository;
					Listing.m_Directory = Directory;
					// A directory that cannot be listed would read as empty, and leave its files unchecked unnoticed.
					if (CFile::fs_CheckFileRights(Directory, EFileRight_Read) == ECheckFileRights_NoAccess)
						DMibError("'{}' cannot be listed"_f << Directory);

					Listing.m_Entries = CFile::fs_FindFilesEx(Directory / "*", EFileAttrib_File | EFileAttrib_Directory, false, false);
					Listing.m_Sizes.f_SetLen(Listing.m_Entries.f_GetLen());
					Listing.m_HoldsRepository.f_SetLen(Listing.m_Entries.f_GetLen());

					for (umint iEntry = 0; iEntry < Listing.m_Entries.f_GetLen(); ++iEntry)
					{
						auto const &Entry = Listing.m_Entries[iEntry];
						auto Name = CFile::fs_GetFile(Entry.m_Path);
						Listing.m_Sizes[iEntry] = 0;
						if ((Entry.m_Attribs & EFileAttrib_File) && fg_MatchesFormatWildcards(Name, Wildcards, bMatchAll))
							Listing.m_Sizes[iEntry] = CFile::fs_GetFileSize(Entry.m_Path);

						Listing.m_HoldsRepository[iEntry] = (Entry.m_Attribs & EFileAttrib_Directory) && !(Entry.m_Attribs & EFileAttrib_Link) && CFile::fs_FileExists(Entry.m_Path / ".git");

						if (Name == ".git")
						{
							Listing.m_bRepository = true;

							auto Excludes = NGit::fg_GetGitRepositoryExcludes(Directory, NGit::fg_GetGitDirectories(Directory), Environment);
							Listing.m_bIgnoreCase = Excludes.m_bIgnoreCase;
							if (Excludes.m_ExcludesFile)
								Listing.m_GlobalExcludes = CFile::fs_ReadStringFromFile(Excludes.m_ExcludesFile, true);

							if (Excludes.m_InfoExclude)
								Listing.m_Excludes = CFile::fs_ReadStringFromFile(Excludes.m_InfoExclude, true);

							if (Excludes.m_InfoAttributes)
								Listing.m_InfoAttributes = CFile::fs_ReadStringFromFile(Excludes.m_InfoAttributes, true);

							if (Excludes.m_AttributesFile)
								Listing.m_GlobalAttributes = CFile::fs_ReadStringFromFile(Excludes.m_AttributesFile, true);
						}
						// git reads no rules file of the working tree through a symbolic link. Its fallback to the index for a tracked
						// .gitattributes deleted from the working tree is out of scope by decision: it needs git on every walk for a
						// checkout in the middle of a change.
						else if (Name == ".gitignore" && (Entry.m_Attribs & EFileAttrib_File) && !(Entry.m_Attribs & EFileAttrib_Link))
							Listing.m_Rules = CFile::fs_ReadStringFromFile(Entry.m_Path, true);
						else if (Name == ".gitattributes" && (Entry.m_Attribs & EFileAttrib_File) && !(Entry.m_Attribs & EFileAttrib_Link))
							Listing.m_Attributes = CFile::fs_ReadStringFromFile(Entry.m_Path, true);
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
			fp_AddRepositoryExcludes(_iRepository, Listing);
			fp_AddDirectoryRules(_iRepository, Listing);
		}

		co_return {};
	}

	// Whether a rule ignores the path, or it stands in a directory ignored but entered for what it tracks. Only such a path
	// can be ignored, which the walk asks for every path it lists, so it is answered without suspending.
	bool CFormatWalk::fp_MayBeIgnored(umint _iRepository, CStr const &_Path, bool _bDirectory) const
	{
		auto const &Repository = mp_Repositories[_iRepository];
		if (!Repository.m_bGit)
			return false;

		if (!mp_TrackedOnly.f_IsEmpty() && mp_TrackedOnly.f_FindEqual(CFile::fs_GetPath(_Path)))
			return true;

		return Repository.m_Ignore.f_IsIgnored(CFile::fs_MakePathRelative(_Path, Repository.m_Root), _bDirectory);
	}

	// The paths git tracks at or below each path, which an index as large as a vendored tree takes git a while to read. Only
	// the ignored paths the walk meets are asked about, which is a handful per repository, all of a listing's at once.
	TCFuture<void> CFormatWalk::fp_AskTracked(umint _iRepository, TCVector<CStr> _Relative)
	{
		auto &Repository = mp_Repositories[_iRepository];
		TCVector<CStr> Unasked;
		for (auto &Relative : _Relative)
		{
			if (!Repository.m_Asked.f_FindEqual(Relative))
				Unasked.f_Insert(fg_Move(Relative));
		}

		if (Unasked.f_IsEmpty())
			co_return {};

		// 'ls-files' reads no paths from its input, so they go on its command line, which Windows bounds at 32767 characters
		// quoting included: a launch takes paths up to a quarter of that.
		constexpr umint c_nCommandLineCharacters = 8192;
		auto Root = Repository.m_Root;
		CStr Output;
		for (umint iPath = 0; iPath < Unasked.f_GetLen();)
		{
			TCVector<CStr> Params = {"--literal-pathspecs", "-c", "core.quotePath=true", "ls-files", "--cached", "--"};
			umint nCharacters = 0;
			do
			{
				nCharacters += Unasked[iPath].f_GetLen() + 1;
				Params.f_Insert(Unasked[iPath++]);
			}
			while (iPath < Unasked.f_GetLen() && nCharacters + Unasked[iPath].f_GetLen() + 1 <= c_nCommandLineCharacters);

			Output += co_await NGit::fg_LaunchGit(fg_Move(Params), Root);
		}

		// The repository may have been asked meanwhile, which only adds to the same sets.
		auto &Asked = mp_Repositories[_iRepository];
		for (auto const &Name : Output.f_SplitLine<true>())
		{
			for (auto Path = NValidate::fg_UnquoteGitPath(Name); Path && !Asked.m_Tracked.f_FindEqual(Path); Path = CFile::fs_GetPath(Path))
				Asked.m_Tracked[Path];
		}

		for (auto const &Relative : Unasked)
			Asked.m_Asked[Relative];

		co_return {};
	}

	// Whether git ignores the path, which it does for one it tracks under no rule. In a directory ignored but entered for what
	// it tracks every path is ignored that git does not track, which the directory's own question answered.
	TCFuture<bool> CFormatWalk::fp_IsIgnored(umint _iRepository, CStr _Path, bool _bDirectory)
	{
		if (!fp_MayBeIgnored(_iRepository, _Path, _bDirectory))
			co_return false;

		auto Relative = CFile::fs_MakePathRelative(_Path, mp_Repositories[_iRepository].m_Root);
		bool bInIgnored = !mp_TrackedOnly.f_IsEmpty() && mp_TrackedOnly.f_FindEqual(CFile::fs_GetPath(_Path));
		if (!bInIgnored)
			co_await fp_AskTracked(_iRepository, {Relative});

		if (!mp_Repositories[_iRepository].m_Tracked.f_FindEqual(Relative))
			co_return true;

		if (_bDirectory)
			mp_TrackedOnly[_Path];

		co_return false;
	}

	TCFuture<CFormatWalkResult> CFormatWalk::f_Run()
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Walking '{}'"_f << mp_Directory));

		CFormatWalkResult Result;

		if (mp_Repositories[0].m_bGit)
		{
			co_await fp_LoadRulesAbove(0, mp_Directory);

			// The directory the walk starts in is entered only where git does not ignore it or a directory above it, as one
			// met on the way down is, a repository of its own checked out there excepted when asked for.
			auto Root = mp_Repositories[0].m_Root;
			bool bIgnored = false;
			for (auto Directory = mp_Directory; Directory.f_GetLen() > Root.f_GetLen() && !bIgnored; Directory = CFile::fs_GetPath(Directory))
				bIgnored = co_await fp_IsIgnored(0, Directory, true);

			// Below a directory entered for what it tracks, only that is walked.
			if (!mp_TrackedOnly.f_IsEmpty())
				mp_TrackedOnly[mp_Directory];

			if (bIgnored)
			{
				bool bHoldsRepository = false;
				if (mp_bNestedRepositories)
				{
					bHoldsRepository = co_await
						(
							g_Dispatch(mp_pReaders->f_Next()) / [Directory = mp_Directory]() -> bool
							{
								return CFile::fs_FileExists(Directory / ".git");
							}
						)
					;
				}

				if (!bHoldsRepository)
				{
					Result.m_Roots.f_Insert(mp_Repositories[0].m_Root);

					co_return fg_Move(Result);
				}
			}
		}

		// Listings are awaited in the order they were issued, while the readers keep as many
		// of the later ones in flight as there are readers.
		TCVector<TCFuture<CFormatListing>> Pending;
		Pending.f_Insert(fp_List(0, mp_Directory));

		// A listing holding ignored entries waits for git to say which of them it tracks, while the walk goes on with the others.
		struct CAsking
		{
			CFormatListing m_Listing;
			TCFuture<void> m_Asked;
		};

		TCVector<CAsking> Asking;
		umint iPending = 0;
		umint iAsking = 0;
		while (iPending < Pending.f_GetLen() || iAsking < Asking.f_GetLen())
		{
			CFormatListing Listing;
			if (iPending < Pending.f_GetLen())
			{
				Listing = co_await fg_Move(Pending[iPending++]);
				auto iListingRepository = Listing.m_iRepository;

				// The walk's own root is listed here too when it is the start directory, in
				// which case nothing above loaded its excludes.
				if (Listing.m_bRepository)
				{
					// A repository entered where the one around it tracks only some paths is a tree of its own, which its own
					// rules and index judge.
					if (Listing.m_Directory != mp_Repositories[iListingRepository].m_Root)
					{
						iListingRepository = fp_AddRepository(Listing.m_Directory, true);
						mp_TrackedOnly.f_Remove(Listing.m_Directory);
					}

					fp_AddRepositoryExcludes(iListingRepository, Listing);
				}

				Listing.m_iRepository = iListingRepository;
				fp_AddDirectoryRules(iListingRepository, Listing);

				// The ignored entries are asked about together, unless the directory was entered for what it tracks, which its
				// own question answered.
				auto const &Repository = mp_Repositories[iListingRepository];
				if (Repository.m_bGit && (mp_TrackedOnly.f_IsEmpty() || !mp_TrackedOnly.f_FindEqual(Listing.m_Directory)))
				{
					TCVector<CStr> Ignored;
					for (auto const &Entry : Listing.m_Entries)
					{
						bool bDirectory = (Entry.m_Attribs & EFileAttrib_Directory) != 0;
						auto Name = CFile::fs_GetFile(Entry.m_Path);
						if ((Entry.m_Attribs & EFileAttrib_Link) || Name == ".git" || (bDirectory && !mp_bRecursive) || (!bDirectory && !fp_MatchesName(Name)))
							continue;

						if (fp_MayBeIgnored(iListingRepository, Entry.m_Path, bDirectory))
							Ignored.f_Insert(CFile::fs_MakePathRelative(Entry.m_Path, Repository.m_Root));
					}

					if (!Ignored.f_IsEmpty())
					{
						auto &Asked = Asking.f_Insert();
						Asked.m_Asked = fp_AskTracked(iListingRepository, fg_Move(Ignored));
						Asked.m_Listing = fg_Move(Listing);

						continue;
					}
				}
			}
			else
			{
				auto &Asked = Asking[iAsking++];
				co_await fg_Move(Asked.m_Asked);
				Listing = fg_Move(Asked.m_Listing);
			}

			auto iRepository = Listing.m_iRepository;

			TCVector<CStr> Directories;
			TCVector<bool> DirectoriesHoldRepository;
			for (auto &Entry : Listing.m_Entries)
			{
				bool bHoldsRepository = Listing.m_HoldsRepository[umint(&Entry - Listing.m_Entries.f_GetArray())];

				if (Entry.m_Attribs & EFileAttrib_Link)
					continue;

				auto Name = CFile::fs_GetFile(Entry.m_Path);
				if (Name == ".git")
					continue;

				// A FIFO, socket or device is listed as a file and read as one, which blocks on a FIFO. Out of scope by decision:
				// the framework reports no file type that tells them apart, and the trees formatted hold none.
				bool bDirectory = (Entry.m_Attribs & EFileAttrib_Directory) != 0;
				if (bDirectory && !mp_bRecursive)
					continue;

				if (!bDirectory && !fp_MatchesName(Name))
					continue;

				if (fp_MayBeIgnored(iRepository, Entry.m_Path, bDirectory) && co_await fp_IsIgnored(iRepository, Entry.m_Path, bDirectory))
				{
					// A repository checked out where the one around it ignores it is a tree of its own, entered on request.
					if (!bDirectory || !mp_bNestedRepositories || !bHoldsRepository)
						continue;
				}

				if (bDirectory && mp_bOwnRepository && bHoldsRepository)
					continue;

				if (!bDirectory)
				{
					auto const &Repository = mp_Repositories[iRepository];
					auto &Found = Result.m_Found.f_Insert();
					Found.m_Size = Listing.m_Sizes[umint(&Entry - Listing.m_Entries.f_GetArray())];
					Found.m_iRoot = iRepository;
					if (Repository.m_bGit)
						Found.m_TextAttribute = Repository.m_Attributes.f_GetTextAttribute(CFile::fs_MakePathRelative(Entry.m_Path, Repository.m_Root));

					Found.m_Path = fg_Move(Entry.m_Path);

					continue;
				}

				Directories.f_Insert(fg_Move(Entry.m_Path));
				DirectoriesHoldRepository.f_Insert(bHoldsRepository);
			}

			// Only a document above that speaks for everything below can close a directory: a directory no document mentions
			// may hold a document of its own that opts its files in, as a module's does. One below a closed directory that opts
			// back in is out of scope by decision: finding it means listing every directory below, where the files no format
			// reaches are, which makes the walk of a tree of import caches many times slower. An audit also keeps it open while a
			// max_line_length may apply below. The directories of a listing are resolved together, so the walk waits on the
			// resolver once per listing rather than once per directory.
			TCFutureVector<NDevelop::CEditorConfigSubtreeProperties> Resolving;
			for (auto const &Directory : Directories)
				mp_Repositories[iRepository].m_Configurations(&NDevelop::CEditorConfigResolver::f_ResolveBelow, Directory) > Resolving;

			auto Resolved = co_await fg_AllDone(Resolving);
			for (umint iDirectory = 0; iDirectory < Directories.f_GetLen(); ++iDirectory)
			{
				auto const &Below = Resolved[iDirectory];

				auto fSettledOff = [&](CStr const &_Key, bool _bOff)
					{
						return Below.m_Settled.f_FindEqual(_Key) && !Below.m_Uncertain.f_FindEqual(_Key) && _bOff;
					}
				;

				CCodeFormattingSettings Settings(Below.m_Properties);
				bool bClosed = fSettledOff("malterlib_format", !Settings.f_IsFormattingEnabled());
				if (mp_bLineLength)
					bClosed = bClosed && fSettledOff("max_line_length", !Settings.m_nMaxColumns);

				// A repository checked out below is configured by its own documents, which the ones above do not speak for.
				if (bClosed && !DirectoriesHoldRepository[iDirectory])
					continue;

				Pending.f_Insert(fp_List(iRepository, fg_Move(Directories[iDirectory])));
			}
		}

		for (auto const &Repository : mp_Repositories)
			Result.m_Roots.f_Insert(Repository.m_Root);

		co_return fg_Move(Result);
	}
}

namespace NMib::NTool::NFormat
{
	CFormatWorkerPool::CFormatWorkerPool(umint _nWorkers, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors)
	{
		mp_StyleResolver = fg_ConstructActor<CFormatStyleResolver>(_pBlockingActors);
		mp_Analyzers = fg_ConstructActor<CFormatAnalyzers>(fg_Max(_nWorkers, umint(1)));

		for (umint i = 0; i < fg_Max(_nWorkers, umint(1)); ++i)
		{
			mp_Workers.f_InsertLast(fg_ConstructActor<CFormatWorker>(_pBlockingActors, mp_StyleResolver, mp_Analyzers));
			mp_RunningJobs.f_Insert(umint(0));
		}
	}

	TCFuture<void> CFormatWorkerPool::fp_Destroy()
	{
		// A job ending as the workers go would start the next one queued on them.
		mp_bDestroying = true;
		while (!mp_Queue.f_IsEmpty())
			mp_Queue.f_PopFirst().m_Promise.f_SetException(DMibErrorInstance("Formatting was stopped before this file was started"));

		for (auto &Worker : mp_Workers)
			co_await fg_Move(Worker).f_Destroy();

		co_await fg_Move(mp_StyleResolver).f_Destroy();
		co_await fg_Move(mp_Analyzers).f_Destroy();

		co_return {};
	}

	// The concurrency manager deals its high CPU actors to a thread in turn, so asking once per queue collects each of them.
	CFormatAnalyzers::CFormatAnalyzers(umint _nMaxConcurrent)
	{
		auto nQueues = fg_ConcurrencyManager().f_GetNumQueues(EPriority_NormalHighCPU);
		for (umint i = 0; i < nQueues && mp_Actors.f_GetLen() < _nMaxConcurrent; ++i)
		{
			auto const &Actor = fg_ConcurrentActorHighCPU();
			bool bKnown = false;
			for (auto const &Known : mp_Actors)
				bKnown = bKnown || Known.f_Get() == Actor.f_Get();

			if (bKnown)
				continue;

			mp_Idle.f_Insert(mp_Actors.f_GetLen());
			mp_Actors.f_Insert(Actor);
		}
	}

	TCFuture<CFormatAnalysis> CFormatAnalyzers::fp_Run(umint _iActor, CCodeFormattingRequest _Request)
	{
		auto Release = g_OnScopeExit / [this, _iActor]
			{
				if (mp_bDestroying)
					return;

				if (mp_Queue.f_IsEmpty())
				{
					mp_Idle.f_Insert(_iActor);

					return;
				}

				auto Queued = mp_Queue.f_PopFirst();
				fp_Run(_iActor, fg_Move(Queued.m_Request)) > fg_Move(Queued.m_Promise);
			}
		;

		co_return co_await
			(
				g_Dispatch(mp_Actors[_iActor]) / [Request = fg_Move(_Request)]() -> CFormatAnalysis
				{
					CStopwatch Stopwatch{true};
					CFormatAnalysis Analysis;
					Analysis.m_Result = fg_AnalyzeCodeFormatting(Request);
					Analysis.m_Seconds = Stopwatch.f_GetTime();

					return Analysis;
				}
			)
		;
	}

	// An analysis ending as the actor goes would start the next one queued on it.
	TCFuture<void> CFormatAnalyzers::fp_Destroy()
	{
		mp_bDestroying = true;
		while (!mp_Queue.f_IsEmpty())
			mp_Queue.f_PopFirst().m_Promise.f_SetException(DMibErrorInstance("Formatting was stopped before this file was analyzed"));

		co_return {};
	}

	TCFuture<CFormatAnalysis> CFormatAnalyzers::f_Analyze(CCodeFormattingRequest _Request)
	{
		if (!mp_Idle.f_IsEmpty())
		{
			auto iActor = mp_Idle.f_GetLast();
			mp_Idle.f_Remove(mp_Idle.f_GetLen() - 1);

			co_return co_await fp_Run(iActor, fg_Move(_Request));
		}

		TCPromise<CFormatAnalysis> Promise;
		auto Future = Promise.f_Future();
		mp_Queue.f_Insert(CQueuedAnalysis{fg_Move(_Request), fg_Move(Promise)});

		co_return co_await fg_Move(Future);
	}

	// A worker holds a few jobs at most, reading files while others are analyzed, and takes the next job in the order they
	// came as soon as it finishes one: a job handed to a worker on arrival would start its read at once, holding every file of
	// a tree in memory, and leave the workers dealt the large files running long after the others.
	constexpr umint gc_nJobsPerWorker = 8;

	umint CFormatWorkerPool::fp_FindFreeWorker() const
	{
		umint iBest = mp_Workers.f_GetLen();
		for (umint iWorker = 0; iWorker < mp_Workers.f_GetLen(); ++iWorker)
		{
			auto nRunning = mp_RunningJobs[iWorker];
			if (nRunning < gc_nJobsPerWorker && (iBest == mp_Workers.f_GetLen() || nRunning < mp_RunningJobs[iBest]))
				iBest = iWorker;
		}

		return iBest;
	}

	void CFormatWorkerPool::fp_StartQueued()
	{
		while (!mp_bDestroying && !mp_Queue.f_IsEmpty())
		{
			auto iWorker = fp_FindFreeWorker();
			if (iWorker == mp_Workers.f_GetLen())
				return;

			auto Queued = mp_Queue.f_PopFirst();
			fp_Run(iWorker, fg_Move(Queued.m_Job)) > fg_Move(Queued.m_Promise);
		}
	}

	TCFuture<CFormatJobResult> CFormatWorkerPool::fp_Run(umint _iWorker, CFormatJob _Job)
	{
		++mp_RunningJobs[_iWorker];

		auto Release = g_OnScopeExit / [this, _iWorker]
			{
				--mp_RunningJobs[_iWorker];
				fp_StartQueued();
			}
		;

		co_return co_await mp_Workers[_iWorker](&CFormatWorker::f_Process, fg_Move(_Job));
	}

	TCFuture<CFormatJobResult> CFormatWorkerPool::f_Process(CFormatJob _Job)
	{
		auto iWorker = fp_FindFreeWorker();
		if (iWorker < mp_Workers.f_GetLen() && mp_Queue.f_IsEmpty())
			co_return co_await fp_Run(iWorker, fg_Move(_Job));

		TCPromise<CFormatJobResult> Promise;
		auto Future = Promise.f_Future();
		mp_Queue.f_Insert(CQueuedJob{fg_Move(_Job), fg_Move(Promise)});

		co_return co_await fg_Move(Future);
	}

	TCActor<CFormatWorkerPool> fg_ConstructFormatWorkerPool(umint _nWorkers)
	{
		TCSharedPointer<CSharedRoundRobinBlockingActors> pBlockingActors = fg_Construct(gc_nFormatBlockingActors);

		return fg_ConstructActor<CFormatWorkerPool>(_nWorkers, pBlockingActors);
	}

	TCFuture<TCVector<CFormatJobResult>> fg_RunFormatJobs(TCActor<CFormatWorkerPool> _Workers, TCVector<CFormatJob> _Jobs)
	{
		TCFutureVector<CFormatJobResult> Pending;
		for (auto &Job : _Jobs)
			_Workers(&CFormatWorkerPool::f_Process, Job) > Pending;

		auto Outcomes = co_await fg_AllDoneWrapped(Pending);

		TCVector<CFormatJobResult> Results;
		Results.f_SetLen(_Jobs.f_GetLen());

		for (umint iJob = 0; iJob < _Jobs.f_GetLen(); ++iJob)
		{
			auto &Outcome = Outcomes[iJob];
			auto &Result = Results[iJob];
			if (Outcome)
				Result = fg_Move(*Outcome);
			else
			{
				Result.m_Path = _Jobs[iJob].m_Path;
				Result.m_Outcome = EFormatOutcome::mc_Failed;
				Result.m_Report = "{}\n"_f << Outcome.f_GetExceptionStr();
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
		TCFuture<CStr> fp_FindLink(CStr _Path, CStr _Boundary);
		bool fp_HasRange() const;
		void fp_Schedule(CStr const &_Path, CStr const &_Root, CStr const &_WorkingDirectory, NGit::EGitTextAttribute _TextAttribute);

		CFormatOptions mp_Options;
		TCSharedPointer<CSharedRoundRobinBlockingActors> mp_pBlockingActors;
		TCActor<CFormatWorkerPool> mp_Workers;
		TCActor<CFormatRootResolver> mp_RootResolver;
		TCSet<CStr> mp_Scheduled;                                          // A file named by more than one selection is formatted once.
		TCFutureVector<CFormatJobResult> mp_Pending;
		TCVector<CFormatJob> mp_Deferred;                                  // Started once the selection is complete.
		TCSharedPointer<TCAtomic<bool>> mp_pStopped = fg_Construct(false); // Set when a selection fails.
	};

	CFormatRun::CFormatRun(CFormatOptions _Options)
		: mp_Options(fg_Move(_Options))
		, mp_pBlockingActors(fg_Construct(gc_nFormatBlockingActors))
		, mp_Workers(fg_ConstructActor<CFormatWorkerPool>(mp_Options.m_nJobs, mp_pBlockingActors))
		, mp_RootResolver(fg_ConstructActor<CFormatRootResolver>(mp_pBlockingActors))
	{
	}

	TCFuture<void> CFormatRun::fp_Destroy()
	{
		co_await fg_Move(mp_Workers).f_Destroy();
		co_await fg_Move(mp_RootResolver).f_Destroy();

		co_return {};
	}

	// The first symbolic link of the path and the directories above it, up to the boundary, or empty. The boundary itself
	// counts too, wherever below it the path lies: a link to a repository, whose root git then spells as the link, or a
	// directory '-C' names through one.
	TCFuture<CStr> CFormatRun::fp_FindLink(CStr _Path, CStr _Boundary)
	{
		co_return co_await
			(
				g_Dispatch(mp_pBlockingActors->f_Next()) / [_Path, _Boundary]() -> CStr
				{
					for (auto Walked = _Path; Walked.f_GetLen() > _Boundary.f_GetLen(); Walked = CFile::fs_GetPath(Walked))
					{
						if (CFile::fs_FileExists(Walked, EFileAttrib_Link))
							return Walked;
					}

					if (_Boundary && CFile::fs_FileExists(_Boundary, EFileAttrib_Link))
						return _Boundary;

					return {};
				}
			)
		;
	}

	// Every candidate is a job; the workers resolve its configuration and report a file
	// the configuration does not opt in as excluded. A patch names a file from its repository's root, as git does, unless
	// the working directory holds the repository below it: then from the working directory, so that repositories side by
	// side do not name their files alike.
	void CFormatRun::fp_Schedule(CStr const &_Path, CStr const &_Root, CStr const &_WorkingDirectory, NGit::EGitTextAttribute _TextAttribute)
	{
		// Out of scope by decision: a file named twice in different case on a file system that ignores case is two jobs, the
		// second of which finds the file changed by the first and fails without writing.
		if (mp_Scheduled.f_FindEqual(_Path) || (mp_Options.m_pCancelled && mp_Options.m_pCancelled->f_Load()))
			return;

		mp_Scheduled.f_Insert(_Path);

		CFormatJob Job;
		Job.m_Path = _Path;
		Job.m_Root = _Root;
		Job.m_bResolveSettings = true;
		Job.m_bValidateLineLength = mp_Options.m_bValidateLineLength;
		Job.m_TextAttribute = _TextAttribute;
		Job.m_ByteRanges = mp_Options.m_ByteRanges;
		Job.m_iFirstLine = mp_Options.m_iFirstLine;
		Job.m_iLastLine = mp_Options.m_iLastLine;
		Job.m_RangePolicy = mp_Options.m_RangePolicy;
		Job.m_Mode = mp_Options.m_Mode;
		Job.m_pCancelled = mp_Options.m_pCancelled;
		Job.m_pStopped = mp_pStopped;
		if (mp_Options.m_PatchRoot)
			Job.m_PatchRoot = mp_Options.m_PatchRoot;
		else if (_Root.f_StartsWith(_WorkingDirectory + "/"))
			Job.m_PatchRoot = _WorkingDirectory;

		// A range applies to one file, which is known only once every selection is.
		if (fp_HasRange())
			mp_Deferred.f_Insert(fg_Move(Job));
		else
			mp_Workers(&CFormatWorkerPool::f_Process, fg_Move(Job)) > mp_Pending;
	}

	bool CFormatRun::fp_HasRange() const
	{
		return mp_Options.m_iFirstLine || !mp_Options.m_ByteRanges.f_IsEmpty();
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
			bool bFile = co_await
				(
					g_Dispatch(mp_pBlockingActors->f_Next()) / [Path]() -> bool
					{
						return CFile::fs_FileExists(Path, EFileAttrib_File);
					}
				)
			;
			if (!bFile)
				co_return DMibErrorInstance("'{}' is not an existing regular file"_f << Path);

			auto Root = co_await mp_RootResolver(&CFormatRootResolver::f_Resolve, CFile::fs_GetPath(Path));
			auto Boundary = Root ? Root : fg_GetFormatBoundary(WorkingDirectory, CFile::fs_GetPath(Path));

			// A file reached through a linked directory lies outside the tree whose configuration it would be formatted by.
			if (auto Link = co_await fp_FindLink(Path, Boundary))
			{
				if (Link == Path)
					co_return DMibErrorInstance("Refusing to format the symbolic link '{}'"_f << Path);

				co_return DMibErrorInstance("Refusing to format '{}' through the symbolic link '{}'"_f << Path << Link);
			}

			fp_Schedule(Path, Boundary, WorkingDirectory, NGit::EGitTextAttribute::mc_Unspecified);
		}

		// The patterns of one directory share its walk: '*.c,*.h' lists each directory once, not once a pattern.
		TCMap<CStr, TCVector<CStr>> Wildcards;
		for (auto const &Pattern : _Selection.m_Patterns)
		{
			auto Search = fg_NormalizeFormatPath(Pattern, WorkingDirectory);
			Wildcards[CFile::fs_GetPath(Search)].f_Insert(CFile::fs_GetFile(Search));
		}

		for (auto &DirectoryWildcards : Wildcards)
		{
			auto const &Directory = Wildcards.fs_GetKey(DirectoryWildcards);
			auto Root = co_await mp_RootResolver(&CFormatRootResolver::f_Resolve, Directory);

			// The walk follows no symbolic link, the directories a pattern names to start in included.
			auto Boundary = Root ? Root : fg_GetFormatBoundary(WorkingDirectory, Directory);
			if (auto Link = co_await fp_FindLink(Directory, Boundary))
				co_return DMibErrorInstance("Refusing to walk the symbolic link '{}'"_f << Link);

			TCActor<CFormatWalk> Walk = fg_ConstructActor<CFormatWalk>
				(
					Directory
					, fg_Move(DirectoryWildcards)
					, Boundary
					, bool(Root)
					, _Selection.m_bRecursive
					, _Selection.m_bNestedRepositories
					, _Selection.m_bOwnRepository
					, mp_Options.m_bValidateLineLength
					, mp_pBlockingActors
				)
			;
			auto DestroyWalk = co_await fg_AsyncDestroy(Walk);
			auto Walked = co_await Walk(&CFormatWalk::f_Run);

			// The largest files are formatted first, so the files left when the workers run out of work are the small ones.
			Walked.m_Found.f_Sort
				(
					[](CFormatWalkFound const &_Left, CFormatWalkFound const &_Right)
					{
						if (_Left.m_Size != _Right.m_Size)
							return _Right.m_Size <=> _Left.m_Size;

						return _Left.m_Path <=> _Right.m_Path;
					}
				)
			;

			for (auto &Found : Walked.m_Found)
				fp_Schedule(Found.m_Path, Walked.m_Roots[Found.m_iRoot], WorkingDirectory, Found.m_TextAttribute);
		}

		co_return {};
	}

	TCFuture<CFormatRunResult> CFormatRun::f_Run(TCVector<CFormatSelection> _Selections, CFormatSink _Sink)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Running Format");

		TCFutureVector<void> Selections;
		for (auto &Selection : _Selections)
			fp_Select(fg_Move(Selection)) > Selections;

		// A failed selection stops the files not yet started; those that were are reported before the failure is.
		CExceptionPointer pSelectionError;
		for (auto &Selected : co_await fg_AllDoneWrapped(Selections))
		{
			if (!Selected && !pSelectionError)
				pSelectionError = Selected.f_GetException();
		}

		if (pSelectionError)
			mp_pStopped->f_Exchange(true);

		if (!pSelectionError)
		{
			if (mp_Scheduled.f_IsEmpty() && mp_Options.m_bRequireFiles)
				co_return DMibErrorInstance("No files matched the requested selection");

			if (fp_HasRange() && mp_Scheduled.f_GetLen() != 1)
				co_return DMibErrorInstance("Range options require exactly one selected file; {} were selected"_f << mp_Scheduled.f_GetLen());

			for (auto &Job : mp_Deferred)
				mp_Workers(&CFormatWorkerPool::f_Process, fg_Move(Job)) > mp_Pending;
		}

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
		for (auto const &Result : Results)
		{
			if (Result.m_Report)
				_Sink.m_fReport(Result.m_Report);

			if (Result.m_Patch)
				_Sink.m_fPatch(Result.m_Patch);

			fg_AddFormatResult(Result, Summary);
		}

		if (mp_Options.m_nSlowest && !Results.f_IsEmpty())
		{
			Results.f_Sort
				(
					[](CFormatJobResult const &_Left, CFormatJobResult const &_Right)
					{
						if (_Left.m_Seconds != _Right.m_Seconds)
							return _Right.m_Seconds < _Left.m_Seconds ? COrdering_Strong::less : COrdering_Strong::greater;

						return _Left.m_Path <=> _Right.m_Path;
					}
				)
			;

			CStr Slowest = "The files that took the longest:\n";
			for (umint iResult = 0; iResult < Results.f_GetLen() && iResult < mp_Options.m_nSlowest; ++iResult)
			{
				auto const &Result = Results[iResult];
				Slowest += "{fe2,sj8} s  {}\n"_f << Result.m_Seconds << Result.m_Path;
			}

			_Sink.m_fReport(Slowest);
		}

		if (pSelectionError)
		{
			for (auto const &Result : Results)
			{
				if (Result.m_Outcome == EFormatOutcome::mc_Changed && mp_Options.m_Mode == EFormatMode::mc_Write)
					_Sink.m_fReport("'{}' was changed before the selection failed\n"_f << Result.m_Path);
			}

			co_return fg_Move(pSelectionError);
		}

		CFormatRunResult Run;
		Run.m_Summary = Summary;
		if (Summary.m_nUnresolved || (mp_Options.m_Mode != EFormatMode::mc_Write && Summary.m_nChanged))
			Run.m_ExitCode = 1;

		co_return Run;
	}

	void fg_AddFormatResult(CFormatJobResult const &_Result, CFormatSummary &o_Summary)
	{
		++o_Summary.m_nSelected;
		o_Summary.m_nUnresolved += _Result.m_nUnresolved;
		o_Summary.m_nLineErrors += _Result.m_nLineErrors;

		if (_Result.m_bFormatted)
		{
			++o_Summary.m_nFormatFiles;
			o_Summary.m_nReported += _Result.m_nReported;
		}

		switch (_Result.m_Outcome)
		{
		case EFormatOutcome::mc_Excluded: ++o_Summary.m_nExcluded; break;
		case EFormatOutcome::mc_Changed: ++o_Summary.m_nChanged; break;
		case EFormatOutcome::mc_Failed: ++o_Summary.m_nFailed; break;
		default: ++o_Summary.m_nUnchanged; break;
		}
	}

	// The snapshot is formatted as the file at the path, so its configuration resolves within the repository the path's
	// directory belongs to, which has to exist; the file need not.
	TCFuture<CFormatSnapshotResult> fg_FormatSnapshot(CStr _WorkingDirectory, CStr _Path, CStr _Source, CFormatOptions _Options)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Formatting a snapshot");

		auto WorkingDirectory = fg_NormalizeFormatPath(_WorkingDirectory, CFile::fs_GetCurrentDirectory());
		auto Path = fg_NormalizeFormatPath(_Path, WorkingDirectory);
		auto Directory = CFile::fs_GetPath(Path);
		TCSharedPointer<CSharedRoundRobinBlockingActors> pBlockingActors = fg_Construct(gc_nFormatBlockingActors);
		bool bDirectory = co_await
			(
				g_Dispatch(pBlockingActors->f_Next()) / [Directory]() -> bool
				{
					return CFile::fs_FileExists(Directory, EFileAttrib_Directory);
				}
			)
		;
		if (!bDirectory)
			co_return DMibErrorInstance("'{}' is not an existing directory"_f << Directory);

		TCActor<CFormatRootResolver> RootResolver = fg_ConstructActor<CFormatRootResolver>(pBlockingActors);
		auto DestroyRootResolver = co_await fg_AsyncDestroy(RootResolver);
		auto Root = co_await RootResolver(&CFormatRootResolver::f_Resolve, Directory);

		TCActor<CFormatWorkerPool> Workers = fg_ConstructActor<CFormatWorkerPool>(umint(1), pBlockingActors);
		auto DestroyWorkers = co_await fg_AsyncDestroy(Workers);

		CFormatJob Job;
		Job.m_Path = Path;
		Job.m_Root = Root ? Root : fg_GetFormatBoundary(WorkingDirectory, Directory);
		Job.m_bResolveSettings = true;
		Job.m_Source = fg_Move(_Source);
		Job.m_bHasSource = true;
		Job.m_ByteRanges = fg_Move(_Options.m_ByteRanges);
		Job.m_iFirstLine = _Options.m_iFirstLine;
		Job.m_iLastLine = _Options.m_iLastLine;
		Job.m_RangePolicy = _Options.m_RangePolicy;
		Job.m_Mode = EFormatMode::mc_Output;

		CFormatSnapshotResult Snapshot;
		Snapshot.m_Result = co_await Workers(&CFormatWorkerPool::f_Process, fg_Move(Job));
		fg_AddFormatResult(Snapshot.m_Result, Snapshot.m_Summary);

		co_return fg_Move(Snapshot);
	}

	CStr fg_DescribeFormatSummary(CFormatSummary const &_Summary, EFormatMode _Mode, fp64 _Seconds)
	{
		CStr Action = _Mode == EFormatMode::mc_Write ? "changed" : "would change";

		return "Formatted {} file(s): {} unchanged, {} {}, {} unresolved violation(s), {} failed. Excluded {} file(s). Time: {fe2} s.\n"_f
			<< _Summary.m_nSelected
			<< _Summary.m_nUnchanged
			<< _Summary.m_nChanged
			<< Action
			<< _Summary.m_nUnresolved
			<< _Summary.m_nFailed
			<< _Summary.m_nExcluded
			<< _Seconds
		;
	}

	TCFuture<CFormatRunResult> fg_RunFormat(TCVector<CFormatSelection> _Selections, CFormatOptions _Options, CFormatSink _Sink)
	{
		TCActor<CFormatRun> Run = fg_ConstructActor<CFormatRun>(fg_Move(_Options));
		auto DestroyRun = co_await fg_AsyncDestroy(Run);

		co_return co_await Run(&CFormatRun::f_Run, fg_Move(_Selections), fg_Move(_Sink));
	}
}
