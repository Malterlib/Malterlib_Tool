// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Format.h"

#include <Mib/Time/Stopwatch>

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

	// All of stdin, as an editor pipes an unsaved buffer.
	TCFuture<CStr> fg_ReadFormatStdIn(TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		struct CState
		{
			CByteVector m_Input;
			TCPromise<void> m_Done;
			bool m_bDone = false;
		};

		TCSharedPointer<CState> pState = fg_Construct();
		auto Done = pState->m_Done.f_Future();
		auto Subscription = co_await _pCommandLine->f_RegisterForStdInBinary
			(
				g_ActorFunctor / [pState](NProcess::EStdInReaderOutputType _Type, CIOByteVector _Input, CStr _Error) -> TCFuture<void>
				{
					if (pState->m_bDone)
						co_return {};

					switch (_Type)
					{
					case NProcess::EStdInReaderOutputType_StdIn: pState->m_Input.f_Insert(_Input.f_GetArray(), _Input.f_GetLen()); break;
					case NProcess::EStdInReaderOutputType_GeneralError:
						pState->m_bDone = true;
						pState->m_Done.f_SetException(DMibErrorInstance("Reading stdin failed: {}"_f << _Error));

						break;
					case NProcess::EStdInReaderOutputType_EndOfFile:
						pState->m_bDone = true;
						pState->m_Done.f_SetResult();

						break;
					}

					co_return {};
				}
				, NProcess::EStdInReaderFlag_None
			)
		;

		auto Input = fg_Move(Subscription).f_GetSubscription();
		auto Result = co_await fg_Move(Done).f_Wrap();
		co_await Input->f_Destroy();

		if (!Result)
			co_return Result.f_GetException();

		co_return fg_StrFromBytes(pState->m_Input.f_GetArray(), pState->m_Input.f_GetLen());
	}

	enum class EFormatRequest
	{
		mc_Fragment
		, mc_File
		, mc_Executable // What the path of this program is, which tells who keeps it running when to start it again.
	};

	// A line of --server's input, read.
	struct CFormatRequest
	{
		EFormatRequest m_Request = EFormatRequest::mc_Fragment;
		CEJsonSorted m_ID; // Given back with the answer.
		bool m_bHasID = false;
		CStr m_Error;      // Why the line is no request.

		CFormatFragment m_Fragment;
		CStr m_Directory; // Its style reads the fragment's names.
		bool m_bIndentWithTabs = true;
		umint m_nIndentSize = 4;

		CStr m_Path; // The file whose configuration formats the source.
		CStr m_Source;
		bool m_bRange = false;
		umint m_iOffset = 0;
		umint m_nLength = 0;
		bool m_bStrictRange = false;
	};

	// A request's count: a number that is not negative, and one the program can count to.
	bool fg_GetFormatRequestCount(CEJsonSorted const &_Value, umint &o_Count)
	{
		if (!_Value.f_IsInteger() || _Value.f_Integer() < 0 || uint64(_Value.f_Integer()) > uint64(TCLimitsInt<umint>::mc_Max))
			return false;

		o_Count = umint(_Value.f_Integer());

		return true;
	}

	// A line is a JSON object, whose "request" is "fragment", "file" or "executable".
	TCFuture<CFormatRequest> fg_ParseFormatRequest(CStr _Line, CStr _WorkingDirectory)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Reading a request");

		auto Json = CEJsonSorted::fs_FromString(_Line);
		if (!Json.f_IsObject())
			co_return DMibErrorInstance("A request is a JSON object");

		CFormatRequest Request;
		Request.m_Directory = _WorkingDirectory;

		if (auto pID = Json.f_GetMember("id"))
		{
			if (!pID->f_IsString() && !pID->f_IsInteger())
			{
				Request.m_Error = "The member 'id' is a string or an integer";

				co_return fg_Move(Request);
			}

			Request.m_ID = *pID;
			Request.m_bHasID = true;
		}

		auto pRequest = Json.f_GetMember("request");
		if (!pRequest || !pRequest->f_IsString())
		{
			Request.m_Error = "A request has a string member 'request'";

			co_return fg_Move(Request);
		}

		auto const &Name = pRequest->f_String();
		if (Name == "executable")
		{
			Request.m_Request = EFormatRequest::mc_Executable;

			co_return fg_Move(Request);
		}

		if (Name == "file")
		{
			Request.m_Request = EFormatRequest::mc_File;

			auto pPath = Json.f_GetMember("path");
			auto pSource = Json.f_GetMember("source");
			auto pOffset = Json.f_GetMember("offset");
			auto pLength = Json.f_GetMember("length");
			auto pStrictRange = Json.f_GetMember("strictRange");
			if (!pPath || !pPath->f_IsString() || !pPath->f_String())
				Request.m_Error = "A file has a string member 'path'";
			else if (!pSource || !pSource->f_IsString())
				Request.m_Error = "A file has a string member 'source'";
			else if (bool(pOffset) != bool(pLength))
				Request.m_Error = "The members 'offset' and 'length' are given together";
			else if (pOffset && (!fg_GetFormatRequestCount(*pOffset, Request.m_iOffset) || !fg_GetFormatRequestCount(*pLength, Request.m_nLength)))
				Request.m_Error = "The members 'offset' and 'length' are numbers that are not negative";
			else if (pStrictRange && !pStrictRange->f_IsBoolean())
				Request.m_Error = "The member 'strictRange' is true or false";
			else
			{
				Request.m_Path = pPath->f_String();
				Request.m_Source = pSource->f_String();
				Request.m_bRange = pOffset != nullptr;
				Request.m_bStrictRange = pStrictRange && pStrictRange->f_Boolean();
			}

			co_return fg_Move(Request);
		}

		if (Name != "fragment")
		{
			Request.m_Error = "Unknown request '{}': expected 'fragment', 'file' or 'executable'"_f << Name;

			co_return fg_Move(Request);
		}

		auto pText = Json.f_GetMember("text");
		if (!pText || !pText->f_IsString())
		{
			Request.m_Error = "A fragment has a string member 'text'";

			co_return fg_Move(Request);
		}

		Request.m_Fragment.m_Text = pText->f_String();

		if (auto pKind = Json.f_GetMember("kind"))
		{
			auto Kind = pKind->f_IsString() ? pKind->f_String() : CStr();
			if (Kind == "type")
				Request.m_Fragment.m_Kind = EFormatFragmentKind::mc_Type;
			else if (Kind == "signature")
				Request.m_Fragment.m_Kind = EFormatFragmentKind::mc_Signature;
			else if (Kind == "expression")
				Request.m_Fragment.m_Kind = EFormatFragmentKind::mc_Expression;
			else
			{
				Request.m_Error = "Unknown kind '{}': expected 'type', 'signature' or 'expression'"_f << Kind;

				co_return fg_Move(Request);
			}
		}

		if (auto pMaxColumns = Json.f_GetMember("maxColumns"))
		{
			if (!fg_GetFormatRequestCount(*pMaxColumns, Request.m_Fragment.m_nMaxColumns))
			{
				Request.m_Error = "The member 'maxColumns' is a number that is not negative";

				co_return fg_Move(Request);
			}
		}

		if (auto pDirectory = Json.f_GetMember("directory"))
		{
			if (!pDirectory->f_IsString())
			{
				Request.m_Error = "The member 'directory' is a string";

				co_return fg_Move(Request);
			}

			// One that isn't whole is one of the working directory, as a file's path is.
			Request.m_Directory = CFile::fs_GetFullPath(pDirectory->f_String(), CFile::fs_GetFullPath(_WorkingDirectory, CFile::fs_GetCurrentDirectory()));
		}

		if (auto pIndentStyle = Json.f_GetMember("indentStyle"))
		{
			if (!pIndentStyle->f_IsString() || (pIndentStyle->f_String() != "tab" && pIndentStyle->f_String() != "space"))
			{
				Request.m_Error = "The member 'indentStyle' is 'tab' or 'space'";

				co_return fg_Move(Request);
			}

			Request.m_bIndentWithTabs = pIndentStyle->f_String() == "tab";
		}

		if (auto pIndentSize = Json.f_GetMember("indentSize"))
		{
			if (!pIndentSize->f_IsInteger() || pIndentSize->f_Integer() < 1 || pIndentSize->f_Integer() > 1000)
			{
				Request.m_Error = "The member 'indentSize' is a number from 1 to 1000";

				co_return fg_Move(Request);
			}

			Request.m_nIndentSize = umint(pIndentSize->f_Integer());
		}

		co_return fg_Move(Request);
	}

	// The answer to a file's request: its source formatted as the file is, or why it isn't.
	TCFuture<CEJsonSorted> fg_AnswerFormatFile(CStr _WorkingDirectory, CFormatRequest _Request, TCSharedPointer<CFormatRootCache> _pRootCache)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Formatting a file's source");

		CEJsonSorted Answer;
		if (_Request.m_bHasID)
			Answer["id"] = _Request.m_ID;

		CFormatOptions Options;
		Options.m_nJobs = fg_GetDefaultFormatJobs();
		Options.m_RangePolicy = _Request.m_bStrictRange ? ECodeRangePolicy::mc_Strict : ECodeRangePolicy::mc_Expand;
		if (_Request.m_bRange)
		{
			if (_Request.m_iOffset > TCLimitsInt<umint>::mc_Max - _Request.m_nLength)
				co_return DMibErrorInstance("The members 'offset' and 'length' overflow the addressable range");

			auto &Range = Options.m_ByteRanges.f_Insert();
			Range.m_iOffset = _Request.m_iOffset;
			Range.m_nLength = _Request.m_nLength;
		}

		auto Source = _Request.m_Source;
		auto Snapshot = co_await fg_FormatSnapshot(fg_Move(_WorkingDirectory), fg_Move(_Request.m_Path), fg_Move(_Request.m_Source), fg_Move(Options), fg_Move(_pRootCache));
		auto const &Result = Snapshot.m_Result;
		if (CStr Report = Result.m_Report.f_Trim())
			Answer["report"] = Report;

		if (Result.m_Outcome == EFormatOutcome::mc_Failed)
			Answer["error"] = "The source could not be formatted";
		else if (Result.m_Outcome == EFormatOutcome::mc_Excluded)
			Answer["excluded"] = true;
		else
		{
			// A source that stays as it is isn't written back.
			if (Result.m_Outcome == EFormatOutcome::mc_Changed && Result.m_Formatted != Source)
				Answer["text"] = Result.m_Formatted;
			else
				Answer["unchanged"] = true;

			if (Result.m_nUnresolved)
				Answer["unresolved"] = true;
		}

		co_return fg_Move(Answer);
	}

	// A line of --server's input.
	struct CFormatRequestLine
	{
		CStr m_Text;
		bool m_bNul = false; // It holds a NUL character, which no request can.
	};

	// The lines of the bytes, which end in a line end, without the blank ones.
	TCVector<CFormatRequestLine> fg_SplitFormatRequestLines(uint8 const *_pBytes, umint _nBytes)
	{
		TCVector<CFormatRequestLine> Lines;
		for (umint iStart = 0; iStart < _nBytes;)
		{
			umint iEnd = iStart;
			bool bNul = false;
			for (; iEnd < _nBytes && _pBytes[iEnd] != '\n'; ++iEnd)
				bNul = bNul || !_pBytes[iEnd];

			CStr Text = bNul ? CStr() : fg_StrFromBytes(_pBytes + iStart, iEnd - iStart);
			if (bNul || !CStr(Text.f_Trim()).f_IsEmpty())
			{
				auto &Line = Lines.f_Insert();
				Line.m_Text = fg_Move(Text);
				Line.m_bNul = bNul;
			}

			iStart = iEnd + 1;
		}

		return Lines;
	}

	// The answer to a line of --server's input.
	TCFuture<CEJsonSorted> fg_AnswerFormatRequest(CFormatRequestLine _Line, CStr _WorkingDirectory, TCSharedPointer<CFormatRootCache> _pRootCache)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Answering a request");

		CEJsonSorted Answer;
		if (_Line.m_bNul)
		{
			Answer["error"] = "A request can't hold a NUL character";

			co_return fg_Move(Answer);
		}

		auto Request = co_await fg_ParseFormatRequest(fg_Move(_Line.m_Text), _WorkingDirectory).f_Wrap();
		if (!Request)
		{
			Answer["error"] = Request.f_GetExceptionStr();

			co_return fg_Move(Answer);
		}

		if (Request->m_bHasID)
			Answer["id"] = Request->m_ID;

		if (Request->m_Error)
			Answer["error"] = Request->m_Error;
		else if (Request->m_Request == EFormatRequest::mc_Executable)
			Answer["executable"] = CFile::fs_GetProgramPath();
		else if (Request->m_Request == EFormatRequest::mc_File)
		{
			// What stops the formatter is the error of the file.
			auto Formatted = co_await fg_AnswerFormatFile(fg_Move(_WorkingDirectory), fg_Move(*Request), fg_Move(_pRootCache)).f_Wrap();
			if (Formatted)
				Answer = fg_Move(*Formatted);
			else
				Answer["error"] = Formatted.f_GetExceptionStr();
		}
		else
		{
			CCodeFormattingSettings Settings;
			Settings.m_bIndentWithTabs = Request->m_bIndentWithTabs;
			Settings.m_nIndentSize = Request->m_nIndentSize;
			Settings.m_nTabWidth = Request->m_nIndentSize;

			// What stops the formatter, like a style that can't be read, is the error of the fragment.
			TCVector<CFormatFragment> Fragments;
			Fragments.f_Insert(fg_Move(Request->m_Fragment));
			auto Results = co_await fg_FormatFragments(fg_Move(Request->m_Directory), fg_Move(Settings), fg_Move(Fragments)).f_Wrap();
			if (!Results)
				Answer["error"] = Results.f_GetExceptionStr();
			else if ((*Results)[0].m_Error)
				Answer["error"] = (*Results)[0].m_Error;
			else
				Answer["text"] = (*Results)[0].m_Text;
		}

		co_return fg_Move(Answer);
	}

	// Answers the requests that stdin gives, a JSON object on each line, with a JSON object on a line of stdout for each,
	// in the same order: each is answered by itself, and its answer written before the next is read. The lines are
	// answered as they come, until stdin ends, so that who formats again and again, like an editor for its files and its
	// hovers, keeps one program running for it.
	TCFuture<uint32> fg_RunFormatServer(CEJsonSorted _Params, TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Serving formatting requests");

		bool bSelection = !_Params["File"].f_Array().f_IsEmpty() || !_Params["Pattern"].f_Array().f_IsEmpty() || _Params["Recursive"].f_Boolean();
		bool bRange = _Params.f_GetMember("Lines") || _Params.f_GetMember("Offset") || _Params.f_GetMember("Length") || _Params["StrictRange"].f_Boolean();
		if (bSelection || bRange || _Params["Check"].f_Boolean() || _Params["Diff"].f_Boolean() || _Params["Stdin"].f_Boolean())
			co_return DMibErrorInstance("--server only combines with --working-directory");

		CStr WorkingDirectory = _Params["WorkingDirectory"].f_String();

		// The configuration is read for each request, but git is asked for a repository's root once.
		TCSharedPointer<CFormatRootCache> pRootCache = fg_Construct();

		struct CState
		{
			CByteVector m_Input; // What has been read and not answered yet.
			bool m_bEnded = false;
			CStr m_Error;
			TCVector<TCPromise<void>> m_Waiters;
		};

		TCSharedPointer<CState> pState = fg_Construct();
		auto Subscription = co_await _pCommandLine->f_RegisterForStdInBinary
			(
				g_ActorFunctor / [pState](NProcess::EStdInReaderOutputType _Type, CIOByteVector _Input, CStr _Error) -> TCFuture<void>
				{
					if (pState->m_bEnded)
						co_return {};

					switch (_Type)
					{
					case NProcess::EStdInReaderOutputType_StdIn: pState->m_Input.f_Insert(_Input.f_GetArray(), _Input.f_GetLen()); break;
					case NProcess::EStdInReaderOutputType_GeneralError:
						pState->m_bEnded = true;
						pState->m_Error = "Reading stdin failed: {}"_f << _Error;

						break;
					case NProcess::EStdInReaderOutputType_EndOfFile: pState->m_bEnded = true; break;
					}

					auto Waiters = fg_Move(pState->m_Waiters);
					for (auto &Waiter : Waiters)
						Waiter.f_SetResult();

					co_return {};
				}
				, NProcess::EStdInReaderFlag_None
			)
		;

		auto Input = fg_Move(Subscription).f_GetSubscription();
		while (true)
		{
			// The lines that are complete, and at the end of stdin the one it ends in.
			bool bEnded = pState->m_bEnded;
			auto *pBytes = pState->m_Input.f_GetArray();
			umint nBytes = pState->m_Input.f_GetLen();
			umint nComplete = nBytes;
			if (!bEnded)
			{
				while (nComplete && pBytes[nComplete - 1] != '\n')
					--nComplete;
			}

			auto Lines = fg_SplitFormatRequestLines(pBytes, nComplete);
			CByteVector Rest;
			Rest.f_Insert(pBytes + nComplete, nBytes - nComplete);
			pState->m_Input = fg_Move(Rest);

			for (auto &Line : Lines)
			{
				auto Answer = co_await fg_AnswerFormatRequest(fg_Move(Line), WorkingDirectory, pRootCache).f_Wrap();
				CStr Output;
				if (Answer)
					Output = Answer->f_ToString(nullptr).f_Trim();
				else
				{
					CEJsonSorted Error;
					Error["error"] = Answer.f_GetExceptionStr();
					Output = Error.f_ToString(nullptr).f_Trim();
				}

				// Written as bytes: text written to a console would end at a NUL a source holds.
				Output += "\n";
				NContainer::CIOByteVector Bytes;
				Bytes.f_Insert((uint8 const *)Output.f_GetStr(), Output.f_GetLen());
				co_await _pCommandLine->f_StdOutBinary(fg_Move(Bytes));
			}

			if (bEnded)
				break;

			// More can have been read while the lines were answered.
			bool bLine = false;
			for (auto Byte : pState->m_Input)
				bLine = bLine || Byte == '\n';

			if (!pState->m_bEnded && !bLine)
				co_await pState->m_Waiters.f_Insert().f_Future();
		}

		co_await Input->f_Destroy();

		if (pState->m_Error)
			co_return DMibErrorInstance(pState->m_Error);

		co_return 0;
	}

	TCFuture<uint32> fg_PrepareAndRunFormat(CEJsonSorted _Params, TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Preparing Format");

		if (_Params["Server"].f_Boolean())
			co_return co_await fg_RunFormatServer(_Params, _pCommandLine);

		CStopwatch Stopwatch{true};
		CFormatOptions Options;

		bool bCheck = _Params["Check"].f_Boolean();
		bool bDiff = _Params["Diff"].f_Boolean();
		if (bCheck && bDiff)
			co_return DMibErrorInstance("--check and --diff cannot be combined");

		if (bCheck)
			Options.m_Mode = EFormatMode::mc_Check;
		else if (bDiff)
			Options.m_Mode = EFormatMode::mc_Diff;

		if (_Params["Jobs"].f_Integer() < 0 || _Params["Slowest"].f_Integer() < 0)
			co_return DMibErrorInstance("--jobs and --slowest cannot be negative");

		auto nJobs = umint(_Params["Jobs"].f_Integer());
		if (!nJobs)
			nJobs = fg_GetDefaultFormatJobs();

		Options.m_nJobs = nJobs;
		Options.m_RangePolicy = _Params["StrictRange"].f_Boolean() ? ECodeRangePolicy::mc_Strict : ECodeRangePolicy::mc_Expand;
		Options.m_nSlowest = umint(_Params["Slowest"].f_Integer());

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

		if (_Params["Stdin"].f_Boolean())
		{
			if (Files.f_GetLen() != 1 || !Patterns.f_IsEmpty())
				co_return DMibErrorInstance("--stdin requires exactly one --file and no --pattern");

			if (bCheck || bDiff)
				co_return DMibErrorInstance("--stdin cannot be combined with --check or --diff");

			auto Source = co_await fg_ReadFormatStdIn(_pCommandLine);
			auto Snapshot = co_await fg_FormatSnapshot(_Params["WorkingDirectory"].f_String(), Files[0], Source, fg_Move(Options));
			auto const &Result = Snapshot.m_Result;
			if (Result.m_Report)
				*_pCommandLine %= Result.m_Report;

			*_pCommandLine %= fg_DescribeFormatSummary(Snapshot.m_Summary, EFormatMode::mc_Output, Stopwatch.f_GetTime());

			if (Result.m_Outcome == EFormatOutcome::mc_Failed)
				co_return DMibErrorInstance("The source could not be formatted");

			// The editor tells the user, and keeps the buffer.
			if (Result.m_Outcome == EFormatOutcome::mc_Excluded)
				co_return 3;

			// Written as bytes: a NUL the buffer holds, in a comment, would end text written to a console.
			auto const &Output = Result.m_Outcome == EFormatOutcome::mc_Changed ? Result.m_Formatted : Source;
			NContainer::CIOByteVector Bytes;
			Bytes.f_Insert((uint8 const *)Output.f_GetStr(), Output.f_GetLen());
			co_await _pCommandLine->f_StdOutBinary(fg_Move(Bytes));

			co_return Result.m_nUnresolved ? 1 : 0;
		}

		CFormatSink Sink;
		Sink.m_fReport = [_pCommandLine](CStr const &_Text)
			{
				*_pCommandLine %= _Text;
			}
		;
		// A patch is written as bytes, as the buffer is: a NUL in a changed line would end text written to a console.
		Sink.m_fPatch = [_pCommandLine](CStr const &_Text)
			{
				NContainer::CByteVector Bytes;
				Bytes.f_Insert((uint8 const *)_Text.f_GetStr(), _Text.f_GetLen());
				*_pCommandLine += Bytes;
			}
		;

		CFormatSelection Selection;
		Selection.m_WorkingDirectory = _Params["WorkingDirectory"].f_String();
		Selection.m_Files = fg_Move(Files);
		Selection.m_Patterns = fg_Move(Patterns);
		Selection.m_bRecursive = _Params["Recursive"].f_Boolean();
		Selection.m_bNestedRepositories = _Params["NestedRepositories"].f_Boolean();

		auto Mode = Options.m_Mode;
		auto Run = co_await fg_RunFormat({fg_Move(Selection)}, fg_Move(Options), fg_Move(Sink));
		*_pCommandLine %= fg_DescribeFormatSummary(Run.m_Summary, Mode, Stopwatch.f_GetTime());

		if (Run.m_Summary.m_nFailed)
			co_return DMibErrorInstance("{} file(s) could not be formatted"_f << Run.m_Summary.m_nFailed);

		co_return Run.m_ExitCode;
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
	;
};

void CTool_Format::f_Register
	(
		TCActor<CDistributedToolAppActor> const &_ToolActor
		, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
		, CDistributedAppCommandLineSpecification &o_CommandLine
		, NStr::CStr const &_ClassName
	)
{
	if (fg_IsMalterlib() || fg_IsCMake() || fg_IsLibTool())
		return;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["Format"]
				, "Description"_o= "Format sources that opt in with malterlib_format = malterlib, malterlib-c or malterlib-buildsystem in .editorconfig.\n"
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
					, "NestedRepositories?"_o=
					{
						"Names"_o= _o["--nested-repositories"]
						, "Default"_o= false
						, "Description"_o= "With --recursive, also enter the repositories checked out in directories the repository around them ignores, each with its own ignore rules.\n"
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
					, "Slowest?"_o=
					{
						"Names"_o= _o["--slowest"]
						, "Default"_o= 0
						, "Description"_o= "After the run, list this many of the files the formatting engine took the longest on, with its times.\n"
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
					, "Stdin?"_o=
					{
						"Names"_o= _o["--stdin"]
						, "Default"_o= false
						, "Description"_o= "Format the source read from stdin as the one --file would be, with its configuration, and write the result to stdout. The file is"
						" neither read nor written. Exits with 3, writing nothing, when the file does not opt in.\n"
					}
					, "StrictRange?"_o=
					{
						"Names"_o= _o["--strict-range"]
						, "Default"_o= false
						, "Description"_o= "Never modify bytes outside the requested range; report units that would need one instead.\n"
					}
					, "Server?"_o=
					{
						"Names"_o= _o["--server"]
						, "Default"_o= false
						, "Description"_o= "Format for a program that keeps this one running, like an editor. Stdin gives a JSON object on each line, a request, and"
						" stdout gets a JSON object on a line for each, in the same order, when the line has been read, until stdin ends. A request's \"id\", a"
						" string or an integer, is given back with its answer, and what can't be answered gets an \"error\". A string with a NUL character"
						" can't be given.\n"
						"{\"request\": \"fragment\", \"text\": ...} formats code that is no file, like the types and signatures of compiler diagnostics, and is"
						" answered with its \"text\". Optional are \"kind\", \"type\", \"signature\" or \"expression\", told from the text when left out;"
						" \"maxColumns\", the columns the fragment is split within, none when left out; \"directory\", whose style reads the names, instead of"
						" --working-directory; and \"indentStyle\", \"tab\" or \"space\", and \"indentSize\", the indentation of the lines of a split fragment.\n"
						"{\"request\": \"file\", \"path\": ..., \"source\": ...} formats the source as the file would be, with its configuration, without"
						" reading or writing the file, optionally only the selection of \"offset\" and \"length\" in bytes, with \"strictRange\". The answer has"
						" the formatted \"text\", or \"unchanged\", with \"unresolved\" when violations are left that have no automatic fix; or \"excluded\""
						" for a file that does not opt in; and the \"report\" of what was found.\n"
						"{\"request\": \"executable\"} is answered with the path of this program.\n"
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

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_Format);
