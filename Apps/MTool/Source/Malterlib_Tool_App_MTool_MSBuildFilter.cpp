// Copyright © 2025 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib/CommandLine/Source/Malterlib_CommandLine_AnsiEncoding.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Process/StdInActor>
#include <Mib/Container/Map>
#include <Mib/Container/Vector>
#include <Mib/String/String>
#include <Mib/CommandLine/AnsiEncodingParse>

struct CTool_MSBuildFilter : public CDistributedTool, public CAllowUnsafeThis
{
public:
	struct CAnsiProperties
	{
		auto operator <=> (CAnsiProperties const &_Right) const = default;

		TCOptional<CAnsiEncodingParse::CBackgroundColor> m_BackgroundColor;
		TCOptional<CAnsiEncodingParse::CForegroundColor> m_ForegroundColor;
		TCOptional<CAnsiEncodingParse::CBold> m_Bold;
		TCOptional<CAnsiEncodingParse::CItalic> m_Italic;
	};

	struct CProjectInfo
	{
		NContainer::TCVector<NStr::CStr> m_BufferedLines;
		CStr m_ProjectName;
		bool m_bIsCompleted = false;
		bool m_bHasBeenOutput = false;
		bool m_bLastWasLink = false;
	};

	struct CConfig
	{
		NStorage::TCSharedPointer<CCommandLineControl> m_pCommandLine;
		bool m_bVerbose = false;
		bool m_bPassThrough = false;
		bool m_bOrder = false;
		bool m_bFilterColored = false;
	};

	struct CProjectsState : public CAllowUnsafeThis
	{
		CProjectsState(CConfig &&_Config)
			: m_Config(fg_Move(_Config))
		{
		}

		CConfig m_Config;
		NContainer::TCMap<mint, CProjectInfo> m_ProjectMap;
		mint m_iLastOutput = 0;
		mint m_iLastProject = 0;

		TCFuture<void> f_OutputProject(CProjectInfo &&_Project);

		CStr m_BufferedInput;
		mint m_BufferedInputParsedChars = 0;

		CAnsiProperties m_AnsiProperties;
		CAnsiEncodingParse::CParseState m_AnsiParseState;
		bool m_bEndOfFileReceived = false;

		CSequencer m_InputSequencer{"Input sequencer"};

		CStr f_AddInput(CIOByteVector &&_Input, bool _bFlush)
		{
			m_BufferedInput.f_AddStr((ch8 const *)_Input.f_GetArray(), _Input.f_GetLen());

			NStr::CStr FinishedInput;

			if (_bFlush)
			{
				FinishedInput = fg_Move(m_BufferedInput);
				m_BufferedInputParsedChars = 0;
			}
			else
			{
				auto &OutputBuffer = m_BufferedInput;
				auto *pParse = OutputBuffer.f_GetStr();
				auto *pFinishedOutput = pParse;
				auto *pStartParse = pParse;
				pParse += m_BufferedInputParsedChars;
				while (*pParse)
				{
					NStr::fg_ParseToEndOfLine(pParse);
					auto *pEndOfLine = pParse;
					NStr::fg_ParseEndOfLine(pParse);
					if (pParse != pEndOfLine)
						pFinishedOutput = pParse;
					else
						break;
				}
				mint nFinishedChars = pFinishedOutput - pStartParse;
				if (!nFinishedChars)
				{
					m_BufferedInputParsedChars = pParse - pStartParse;
					return {};
				}
				FinishedInput = OutputBuffer.f_Extract(0, nFinishedChars);
				fg_StrDelete(OutputBuffer, 0, nFinishedChars);
				m_BufferedInputParsedChars = 0;
			}

			return FinishedInput;
		}

		TCFuture<void> f_OutputProject(mint _iProject)
		{
			auto *pProject = m_ProjectMap.f_FindEqual(_iProject);
			if (!pProject)
				co_return {};

			auto &ProjectInfo = *pProject;

			ProjectInfo.m_bHasBeenOutput = true;

			bool bAllEmpty = true;
			for (auto const &BufferedLine : ProjectInfo.m_BufferedLines)
			{
				if (!BufferedLine.f_Trim().f_IsEmpty())
				{
					bAllEmpty = false;
					break;
				}
			}

			if (bAllEmpty)
				co_return {};

			co_await m_Config.m_pCommandLine->f_StdErr("\n=== {} ({}) ===\n"_f << ProjectInfo.m_ProjectName << m_iLastProject);

			mint nBufferedLines = 0;
			CStr Output;

			for (auto const &BufferedLine : ProjectInfo.m_BufferedLines)
			{
				Output += "{}\n"_f << BufferedLine;
				if (++nBufferedLines == 128)
				{
					co_await m_Config.m_pCommandLine->f_StdOut(fg_Move(Output));
					Output.f_Clear();
					nBufferedLines = 0;
				}
			}

			if (Output)
				co_await m_Config.m_pCommandLine->f_StdOut(fg_Move(Output));

			co_return {};
		}

		TCFuture<void> f_ProcessInput(CIOByteVector _Input, bool _bFlush)
		{
			auto &pCommandLine = m_Config.m_pCommandLine;
			auto bVerbose = m_Config.m_bVerbose;
			auto bPassThrough = m_Config.m_bPassThrough;
			auto bOrder = m_Config.m_bOrder;
			auto bFilterColored = m_Config.m_bFilterColored;

			CStr ToProcess = f_AddInput(fg_Move(_Input), _bFlush);

			if (!ToProcess)
				co_return {};

			auto Lines = ToProcess.f_SplitLine();

			CAnsiEncoding AnsiEncoding(EAnsiEncodingFlag_Color);

			for (auto &Line : Lines)
			{
				if (bPassThrough)
				{
					co_await pCommandLine->f_StdOut("{}\n"_f << fg_Move(Line));

					continue;
				}
				CStr OutputLine;
				CStr CleanLine;
				bool bDidOutput = false;

				bool bSetProperties = false;

				auto fApplyFormatting = [&]
					{
						if (bSetProperties)
						{
							OutputLine += AnsiEncoding.f_Default();
							bSetProperties = false;
						}

						if (m_AnsiProperties.m_Bold && m_AnsiProperties.m_Bold->m_bEnabled)
						{
							OutputLine += AnsiEncoding.f_Bold();
							bSetProperties = true;
						}

						if (m_AnsiProperties.m_Italic && m_AnsiProperties.m_Bold->m_bEnabled)
						{
							OutputLine += AnsiEncoding.f_Italic();
							bSetProperties = true;
						}

						if (m_AnsiProperties.m_BackgroundColor && m_AnsiProperties.m_BackgroundColor->m_bEnabled)
						{
							OutputLine += AnsiEncoding.f_BackgroundRGBFormat
								(
									m_AnsiProperties.m_BackgroundColor->m_Red
									, m_AnsiProperties.m_BackgroundColor->m_Green
									, m_AnsiProperties.m_BackgroundColor->m_Blue
								)
							;
							bSetProperties = true;
						}

						if (m_AnsiProperties.m_ForegroundColor && m_AnsiProperties.m_ForegroundColor->m_bEnabled)
						{
							OutputLine += AnsiEncoding.f_ForegroundRGBFormat
								(
									m_AnsiProperties.m_ForegroundColor->m_Red
									, m_AnsiProperties.m_ForegroundColor->m_Green
									, m_AnsiProperties.m_ForegroundColor->m_Blue
								)
							;
							bSetProperties = true;
						}
					}
				;
				bool bBelongsInProject = false;

				TCOptional<CAnsiEncodingParse::CForegroundColor> Color;

				CAnsiEncodingParse::fs_Parse
					(
						Line
						, [&](auto const &_String) -> bool
						{
							fApplyFormatting();
							mint nSpaces = 0;
							do
							{
								if (bDidOutput)
									break;

								auto *pParse = _String.f_GetStr();
								auto *pSpaceStart = pParse;
								while (*pParse && *pParse == ' ')
									++pParse;

								nSpaces = pParse - pSpaceStart;
								if (!nSpaces)
									break;

								if (nSpaces >= 7)
								{
									bBelongsInProject = true;
									CStrPtr ToOutput(_String.f_GetStr() + 7, fg_StrLen(_String.f_GetStr() + 7));
									OutputLine += ToOutput;
									if (CleanLine.f_IsEmpty() && !ToOutput.f_IsEmpty())
										Color = m_AnsiProperties.m_ForegroundColor;
									CleanLine += ToOutput;
									bDidOutput = true;
									return true;
								}
								else if (nSpaces > 5)
									break;

								auto pNumberStart = pParse;

								while (*pParse && fg_CharIsNumber(*pParse))
									++pParse;

								if (pNumberStart == pParse)
									break;

								if (*pParse != '>')
									break;

								CStrPtr ProjectIdStr(pNumberStart, pParse - pNumberStart);

								m_iLastProject = ProjectIdStr.f_ToInt(mint(1));
								bBelongsInProject = true;

								++pParse;

								CStrPtr ToAdd(pParse, fg_StrLen(pParse));
								OutputLine += ToAdd;
								if (CleanLine.f_IsEmpty() && !ToAdd.f_IsEmpty())
									Color = m_AnsiProperties.m_ForegroundColor;
								CleanLine += ToAdd;
								bDidOutput = true;

								return true;
							}
							while (false)
								;

							OutputLine += _String;
							if (CleanLine.f_IsEmpty())
								Color = m_AnsiProperties.m_ForegroundColor;
							CleanLine += _String;
							bDidOutput = true;

							return true;
						}
						, [&](CAnsiEncodingParse::CPropertyChange const &_Change)
						{
							if (_Change.f_IsOfType<CAnsiEncodingParse::CReset>())
							{
								m_AnsiProperties.m_BackgroundColor.f_Clear();
								m_AnsiProperties.m_ForegroundColor.f_Clear();
								m_AnsiProperties.m_Bold.f_Clear();
								m_AnsiProperties.m_Italic.f_Clear();
							}
							else if (_Change.f_IsOfType<CAnsiEncodingParse::CBold>() && _Change.f_GetAsType<CAnsiEncodingParse::CBold>().m_bEnabled)
								m_AnsiProperties.m_Bold = {true};
							else if (_Change.f_IsOfType<CAnsiEncodingParse::CItalic>() && _Change.f_GetAsType<CAnsiEncodingParse::CItalic>().m_bEnabled)
								m_AnsiProperties.m_Italic = {true};
							else if (_Change.f_IsOfType<CAnsiEncodingParse::CBackgroundColor>())
							{
								auto &Color = _Change.f_GetAsType<CAnsiEncodingParse::CBackgroundColor>();
								if (Color.m_bEnabled)
									m_AnsiProperties.m_BackgroundColor = Color;
								else
									m_AnsiProperties.m_BackgroundColor.f_Clear();
							}
							else if (_Change.f_IsOfType<CAnsiEncodingParse::CForegroundColor>())
							{
								auto &Color = _Change.f_GetAsType<CAnsiEncodingParse::CForegroundColor>();
								if (Color.m_bEnabled)
									m_AnsiProperties.m_ForegroundColor = Color;
								else
									m_AnsiProperties.m_ForegroundColor.f_Clear();
							}
						}
						, &m_AnsiParseState
					)
				;

				if (bSetProperties)
				{
					OutputLine += AnsiEncoding.f_Default();
					bSetProperties = false;
				}

				constexpr auto static c_ErrorColor = CAnsiEncodingParse::CDecodedColor{.m_Red = 255, .m_Green = 109, .m_Blue = 103, .m_bEnabled = true};
				constexpr auto static c_WarningColor = CAnsiEncodingParse::CDecodedColor{.m_Red = 254, .m_Green = 251, .m_Blue = 103, .m_bEnabled = true};

				if (bBelongsInProject)
				{
					auto &ProjectInfo = m_ProjectMap[m_iLastProject];

					auto CleanTrimmed = CleanLine.f_Trim();

					bool bSkipThisLine = false;
					if (ProjectInfo.m_bLastWasLink)
					{
						if (CleanLine.f_StartsWith("  \"") && CleanLine.f_EndsWith("\""))
							bSkipThisLine = true;
						else
						 	ProjectInfo.m_bLastWasLink = false;
					}


					if (bSkipThisLine || CleanTrimmed == ":VCEnd" || CleanTrimmed.f_Find("\\CL.exe /c") > 0 || CleanTrimmed.f_Find("\\clang-cl.exe /c ") > 0)
						;
					else if
					(
						CleanTrimmed.f_Find("\\link.exe /ERRORREPORT:QUEUE") > 0
						|| CleanTrimmed.f_Find("\\Lib.exe /OUT") > 0
						|| CleanTrimmed.f_Find("\\lld-link.exe /OUT") > 0
						|| CleanTrimmed.f_Find("\\llvm-lib.exe /OUT") > 0
					)
						ProjectInfo.m_bLastWasLink = true;
					else if (bFilterColored && Color && Color->m_bEnabled && !(*Color == c_ErrorColor || *Color == c_WarningColor))
					{
						if (bVerbose)
							co_await pCommandLine->f_StdErr("\n=== Skip color ({} {} {}) === {}\n"_f << Color->m_Red << Color->m_Green << Color->m_Blue << OutputLine);
					}
					else
						ProjectInfo.m_BufferedLines.f_Insert(fg_Move(OutputLine));

					if (CleanLine.f_StartsWith("Done Building Project "))
					{
						CStr ProjectNameParse = CleanLine.f_RemovePrefix("Done Building Project ");
						fg_GetStrSep(ProjectNameParse, "\"");
						ProjectInfo.m_ProjectName = fg_GetStrSep(ProjectNameParse, "\"");

						ProjectInfo.m_bIsCompleted = true;

						if (bVerbose)
							co_await pCommandLine->f_StdErr("\n=== Completed {} ({}) ===\n"_f << ProjectInfo.m_ProjectName << m_iLastProject);

						if (!bOrder)
						{
							ProjectInfo.m_bHasBeenOutput = true;

							co_await f_OutputProject(m_iLastProject);
						}
						else
						{
							for
							(
								auto iProject = m_ProjectMap.f_GetIterator_SmallestGreaterThanEqual(m_iLastOutput)
								; iProject && iProject->m_bIsCompleted
								; ++iProject
							)
							{
								auto &ProjectInfo = *iProject;

								if (ProjectInfo.m_bHasBeenOutput)
									continue;

								ProjectInfo.m_bHasBeenOutput = true;
								m_iLastOutput = iProject.f_GetKey();

								co_await f_OutputProject(iProject.f_GetKey());
							}
						}
					}
				}
				else if (!CleanLine.f_Trim().f_IsEmpty())
				{
					auto &Color = m_AnsiProperties.m_ForegroundColor;
					if (bFilterColored && Color && Color->m_bEnabled && (*Color == c_ErrorColor || *Color == c_WarningColor))
					{
						if (bVerbose)
							co_await pCommandLine->f_StdErr("\n=== Skip color ({} {} {}) === {}\n"_f << Color->m_Red << Color->m_Green << Color->m_Blue << OutputLine);
					}
					else
						co_await pCommandLine->f_StdOut("{}\n"_f << fg_Move(OutputLine)); // Output unordered lines immediately to maintain build progress visibility
				}
			}

			if (_bFlush)
			{
				// Output any remaining incomplete projects
				for (auto &[iProject, ProjectInfo] : m_ProjectMap.f_Entries())
				{
					if (ProjectInfo.m_bHasBeenOutput || ProjectInfo.m_BufferedLines.f_IsEmpty())
						continue;

					if (bVerbose)
						co_await pCommandLine->f_StdErr("\n=== Incomplete Project {} ({}) ===\n"_f << ProjectInfo.m_ProjectName << iProject);
					for (auto const &BufferedLine : ProjectInfo.m_BufferedLines)
						co_await pCommandLine->f_StdOut("{}\n"_f << BufferedLine);
				}
			}

			co_return {};
		}
	};

	void f_Register
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
					"Names"_o= _o["MSBuildFilter"]
					, "Description"_o= "Filter and order output from MSBuild in normal verbosity mode.\n"
					"Reads from stdin and outputs filtered/ordered results to stdout.\n"
					, "Category"_o= "Build"
					, "Options"_o=
					{
						"Verbose?"_o=
						{
							"Names"_o= _o["--verbose", "-v"]
							, "Default"_o= false
							, "Description"_o= "Show verbose output for debugging.\n"
						}
						, "PassThrough?"_o=
						{
							"Names"_o= _o["--pass-through"]
							, "Default"_o= false
							, "Description"_o= "Pass through all input without filtering (for testing).\n"
						}
						, "Order?"_o=
						{
							"Names"_o= _o["--order"]
							, "Default"_o= false
							, "Description"_o= "Order output of projects.\n"
						}
						, "FilterColored?"_o=
						{
							"Names"_o= _o["--filter-colored"]
							, "Default"_o= true
							, "Description"_o= "Filter out colored lines (except for errors).\n"
						}
					}
					, "Parameters"_o=
					{
						"InputFile?"_o=
						{
							"Type"_o= ""
							, "Description"_o= "The file to read log from. Default is stdin"
						}
					}
				}
				, [=](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					bool bVerbose = _Params["Verbose"].f_Boolean();
					bool bPassThrough = _Params["PassThrough"].f_Boolean();
					bool bOrder = _Params["Order"].f_Boolean();
					bool bFilterColored = _Params["FilterColored"].f_Boolean();

					TCOptional<CStr> InputFile;

					if (auto *pValue = _Params.f_GetMember("InputFile"))
						InputFile = pValue->f_String();

					// Create stdin reader actor
					TCActor<CStdInActor> StdInActor = fg_Construct<CStdInActor>();

					// Set up separate promises for EOF and cancellation
					NConcurrency::TCPromiseFuturePair<void> EofDone;
					NConcurrency::TCPromiseFuturePair<void> Cancelled;

					NStorage::TCSharedPointer<CProjectsState> pState = fg_Construct
						(
							CConfig
							{
								.m_pCommandLine = _pCommandLine
								, .m_bVerbose = bVerbose
								, .m_bPassThrough = bPassThrough
								, .m_bOrder = bOrder
								, .m_bFilterColored = bFilterColored
							}
						)
					;

					// MSBuild parsing and filtering state

					if (InputFile)
					{
						auto Contents = CFile::fs_ReadFile(*InputFile);

						co_await pState->f_ProcessInput(fg_Move(Contents), true);
					}
					else
					{
						auto Subscription = co_await StdInActor
							(
								&CStdInActor::f_RegisterForInputBinary
								, g_ActorFunctor /
								[
									pCommandLine = _pCommandLine
									, bVerbose
									, bPassThrough
									, EofPromise = fg_Move(EofDone.m_Promise)
									, pState
								]
								(EStdInReaderOutputType _Type, CIOByteVector _Input, CStr _Error) -> TCFuture<void>
								{
									using namespace NMib::NStr;
									auto &State = *pState;

									auto Subscription = co_await State.m_InputSequencer.f_Sequence();

									if (State.m_bEndOfFileReceived)
										co_return {};

									switch (_Type)
									{
									case NProcess::EStdInReaderOutputType_StdIn:
										{
											if (bPassThrough)
											{
												co_await pCommandLine->f_StdOutBinary(fg_Move(_Input));
												co_return {};
											}

											co_await State.f_ProcessInput(fg_Move(_Input), false);
										}
										break;
									case NProcess::EStdInReaderOutputType_GeneralError:
										{
											co_await pCommandLine->f_StdErr("[ERROR] {}\n"_f << _Error);
										}
										break;
									case NProcess::EStdInReaderOutputType_EndOfFile:
										{
											State.m_bEndOfFileReceived = true;

											co_await State.f_ProcessInput({}, true);

											if (bVerbose)
												*pCommandLine %= "[EOF] End of input - processed {} projects\n"_f << State.m_ProjectMap.f_GetLen();

											EofPromise.f_SetResult();
										}
										break;
									}

									co_return {};
								}
								, NProcess::EStdInReaderFlag_None
								, 1024 * 1024 // 1MB max line size
							)
						;

						// Register for cancellation to handle Ctrl+C
						auto CancellationSubscription = _pCommandLine->f_RegisterForCancellation
							(
								g_ActorFunctor
								(
									g_ActorSubscription / []()
									{
									}
								)
								/ [CancelPromise = fg_Move(Cancelled.m_Promise), pCommandLine = _pCommandLine, bVerbose]() -> TCFuture<bool>
								{
									if (bVerbose)
										*pCommandLine %= "[CANCELLED] Received cancel signal\n";
									CancelPromise.f_SetResult();
									co_return false;
								}
							)
						;

						// Wait for EOF or cancellation
						co_await NConcurrency::fg_AnyDone(fg_Move(EofDone.m_Future), fg_Move(Cancelled.m_Future));

						// Clean up
						co_await StdInActor(&CStdInActor::f_AbortReads);
						co_await fg_Move(StdInActor).f_Destroy();
					}

					co_return 0;
				}
			)
		;
	}
};

DMibRuntimeClass(CTool, CTool_MSBuildFilter);
