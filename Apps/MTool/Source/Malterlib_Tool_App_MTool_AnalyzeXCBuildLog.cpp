// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_AnalyzeXCBuildLog.h"

void CTool_AnalyzeXCBuildLog::f_Register
	(
		TCActor<CDistributedToolAppActor> const &_ToolActor
		, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
		, CDistributedAppCommandLineSpecification &o_CommandLine
		, NStr::CStr const &_ClassName
	)
{
	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["analyze-xc-build-log"]
				, "Description"_o= "Analyze log to see why the files were rebuilt.\n"
				, "Category"_o= "Analyze"
				, "Options"_o=
				{
					"WorkingDirectory?"_o=
					{
						"Names"_o= _o["--working-directory"]
						, "Default"_o= NFile::CFile::fs_GetCurrentDirectory()
						, "Description"_o= "The current directory.\n"
						"One of: Build, Clean or ReBuild\n"
					}
				}
				, "Parameters"_o=
				{
				}
			}
			, [](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				auto CaptureScope = co_await g_CaptureExceptions;

				auto &WorkingDirectory = _Params.f_GetMember("WorkingDirectory")->f_String();
				auto ManifestDir = WorkingDirectory / "current-manifest.swiftbuilddata";

				*_pCommandLine += "WorkingDirectory: {}\n"_f << WorkingDirectory;
				*_pCommandLine += "ManifestDir: {}\n"_f << ManifestDir;

				auto ManifestFile = ManifestDir / "manifest.json";
				auto Manifest = CEJsonOrdered::fs_FromString(CFile::fs_ReadStringFromFile(ManifestFile, true), ManifestFile);

				CFile::fs_WriteStringToFile(WorkingDirectory / "manifest-pretty.json", Manifest.f_ToString(), false);
				CFile::fs_WriteStringToFile(WorkingDirectory / "manifest-pretty.json.color", Manifest.f_ToStringColored(_pCommandLine->m_AnsiFlags), false);

				auto TraceFile = WorkingDirectory / "completed-build.trace";

				CStr TraceContents = CFile::fs_ReadStringFromFile(TraceFile, true);

				auto *pParse = TraceContents.f_GetStr();
				fg_ParseWhiteSpace(pParse);
				if (*pParse != '[')
					co_return DMibErrorInstance("Expected trace to start with '[': {}"_f << CStr(pParse, 20));
				++pParse;
				fg_ParseWhiteSpace(pParse);

				struct CRule
				{
					CStr m_Description;
					CStr m_NeedsToRunReason;
					CStr m_InputOutdated;
					TCVector<CStr> m_DeferredOn;
				};

				TCMap<CStr, zmint> Frequencies;
				TCMap<CStr, CRule> Rules;
				while (*pParse)
				{
					fg_ParseWhiteSpace(pParse);
					if (*pParse == '{')
					{
						++pParse;
						TCVector<CStr> Values;
						while (*pParse)
						{
							fg_ParseWhiteSpace(pParse);
							if (fg_CharIsNumber(*pParse))
							{
								auto pParseStart = pParse;
								fg_ParseAlphaNumeric(pParse);
								Values.f_Insert(CStr(CInitByRange(), pParseStart, pParse));
							}
							else if (*pParse == '"')
							{
								auto pParseStart = pParse;
								NStr::fg_ParseEscape<'"'>(pParse, '"');

								Values.f_Insert(fg_RemoveEscape(CStr(CInitByRange(), pParseStart, pParse)));
							}
							else if (*pParse == ',')
								++pParse;
							else if (*pParse == '}')
							{
								++pParse;
								fg_ParseWhiteSpace(pParse);
								if (*pParse == ',')
								{
									fg_ParseWhiteSpace(pParse);
									++pParse;
								}
								else
									co_return DMibErrorInstance("Expected ',' after line: {}"_f << CStr(pParse, 20));

								break;
							}
							else
								co_return DMibErrorInstance("Expected '\"': {}"_f << CStr(pParse, 20));
						}
						if (Values.f_GetLen() > 0)
						{
							auto &Command = Values[0];
							++Frequencies[Command];

							if (Command == "new-rule")
								Rules[Values[1]].m_Description = Values[2];
							else if (Command == "rule-scanning-deferred-on-input")
								Rules[Values[2]].m_DeferredOn.f_Insert(Values[1]);
							else if (Command == "rule-needs-to-run")
							{
								auto &Rule = Rules[Values[1]];

								if (Values[2] == "input-rebuilt")
									Rule.m_InputOutdated = Values[3];

								Rule.m_NeedsToRunReason = Values[2];
							}
						}
						//*_pCommandLine += "{vs}\n"_f << Values;
					}
					else if (*pParse == ']')
						break;
					else
						co_return DMibErrorInstance("Expected line to start with '{{': {}"_f << CStr(pParse, 20));
				}

				*_pCommandLine += "Frequencies: {}\n"_f << Frequencies;

				for (auto &Rule : Rules)
				{
					if (Rule.m_NeedsToRunReason.f_IsEmpty())
						continue;

					*_pCommandLine += "{}: {} DeferredOn: {vs} Outdated: {}\n"_f << Rule.m_Description << Rule.m_NeedsToRunReason << Rule.m_DeferredOn << Rule.m_InputOutdated;
				}

				fg_ParseWhiteSpace(pParse);
				if (*pParse != ']')
					co_return DMibErrorInstance("Expected trace to end with ']': {}"_f << CStr(pParse, 20));

				//CFile::fs_WriteStringToFile(WorkingDirectory / "trace-pretty.json", Trace.f_ToString(), false);
				//CFile::fs_WriteStringToFile(WorkingDirectory / "trace-pretty.json.color", Trace.f_ToStringColored(_pCommandLine->m_AnsiFlags), false);

				co_return 0;
			}
		)
	;
}

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_AnalyzeXCBuildLog);
