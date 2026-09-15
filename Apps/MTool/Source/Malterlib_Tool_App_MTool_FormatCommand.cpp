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

	TCFuture<uint32> fg_PrepareAndRunFormat(CEJsonSorted _Params, TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Preparing Format");

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

		CFormatSink Sink;
		Sink.m_fReport = [_pCommandLine](CStr const &_Text)
			{
				*_pCommandLine %= _Text;
			}
		;
		Sink.m_fPatch = [_pCommandLine](CStr const &_Text)
			{
				*_pCommandLine += _Text;
			}
		;
		CFormatSelection Selection;
		Selection.m_WorkingDirectory = _Params["WorkingDirectory"].f_String();
		Selection.m_Files = fg_Move(Files);
		Selection.m_Patterns = fg_Move(Patterns);
		Selection.m_bRecursive = _Params["Recursive"].f_Boolean();
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
