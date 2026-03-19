// Copyright © 2025 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib/CommandLine/Source/Malterlib_CommandLine_AnsiEncoding.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/CommandLine/AnsiEncodingParse>
#include <Mib/CommandLine/TableRenderer>
#include <Mib/Container/Map>
#include <Mib/Container/Vector>
#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Process/StdInActor>
#include <Mib/String/String>

struct CTool_XcodeBuildFilter : public CDistributedTool, public CAllowUnsafeThis
{
public:
	struct CConfig
	{
		TCSharedPointer<CCommandLineControl> m_pCommandLine;
		bool m_bVerbose = false;
		bool m_bPassThrough = false;
		bool m_bShowCommands = false;
		bool m_bShowProgress = true;
	};

	struct CCommandInfo
	{
		CStr m_CommandLine;
		TCVector<CStr> m_BufferedOutput;
		bool m_bHasFailed = false;
	};

	struct CCommandCounts
	{
		umint m_nCompile = 0;
		umint m_nLink = 0;
		umint m_nPhaseScript = 0;
		umint m_nProcessPCH = 0;
		umint m_nOther = 0;

		umint f_GetTotal() const
		{
			return m_nCompile + m_nLink + m_nPhaseScript + m_nProcessPCH + m_nOther;
		}
	};

	struct CState : public CAllowUnsafeThis
	{
		CState(CConfig &&_Config)
			: m_Config(fg_Move(_Config))
		{
		}

		CConfig m_Config;

		TCMap<CStr, CCommandInfo> m_Commands;
		CStr m_CurrentCommandKey;
		TCVector<CStr> m_NonCommandBufferedOutput;
		bool m_bInFailureReport = false;
		TCVector<CStr> m_FailedCommands;

		TCOptional<bool> m_BuildResult;

		CCommandCounts m_CommandCounts;
		CStopwatch m_LastProgressUpdate{true};
		CStopwatch m_BuildStopClock{true};
		bool m_bProgressShown = false;
		umint m_LastOutputProgressRows = 0;

		CStr m_BufferedInput;
		umint m_BufferedInputParsedChars = 0;
		bool m_bEndOfFileReceived = false;

		CStr *m_pPendingIncludeLineFix = nullptr;

		CSequencer m_InputSequencer{"Input sequencer"};

		static bool fs_IsCommandLine(CStr const &_Line)
		{
			static constexpr ch8 const *c_CommandPrefixes[] =
			{
				"Compile"
				, "SwiftCompile"
				, "CompileSwift"
				, "CompileSwiftSources"
				, "CompileAssetCatalog"
				, "Ld "
				, "Link"
				, "CodeSign"
				, "Process"
				, "ProcessPCH"
				, "ProcessInfoPlistFile"
				, "ProcessProductPackaging"
				, "ProcessStoryboard"
				, "PhaseScriptExecution"
				, "GenerateDSYMFile"
				, "CreateUniversalBinary"
				, "CpResource"
				, "Copy"
				, "Touch"
				, "WriteAuxiliaryFile"
				, "WriteFile"
				, "MkDir"
				, "SetOwnerAndGroup"
				, "Chmod"
				, "SymLink"
				, "Libtool"
				, "Strip"
				, "RegisterExecutionPolicyException"
				, "builtin-"
			};

			for (auto const *pPrefix : c_CommandPrefixes)
			{
				if (_Line.f_StartsWith(pPrefix))
					return true;
			}

			return false;
		}

		static bool fs_IsBuildCommand(CStr const &_Line)
		{
			if (_Line.f_StartsWith("cd "))
				return false;

			return fs_IsCommandLine(_Line);
		}

		static bool fs_IsSplitIncludeFirstLine(CStr const &_Trimmed)
		{
			constexpr static auto c_Prefix = gc_Str<"In file included from ">.m_Str;
			if (!_Trimmed.f_StartsWith(c_Prefix) && !c_Prefix.f_StartsWith(_Trimmed))
				return false;

			umint nColonCount = 0;
			for (auto Char : _Trimmed)
			{
				if (Char == ':')
					++nColonCount;
			}

			return nColonCount != 2;
		}

		static bool fs_IsSplitIncludeLineNumber(CStr const &_Trimmed)
		{
			return _Trimmed.f_EndsWith(":");
		}

		void f_IncrementCommandCount(CStr const &_CommandLine)
		{
			if (_CommandLine.f_StartsWith("Compile") || _CommandLine.f_StartsWith("SwiftCompile"))\
				++m_CommandCounts.m_nCompile;
			else if (_CommandLine.f_StartsWith("Ld ") || _CommandLine.f_StartsWith("Link"))
				++m_CommandCounts.m_nLink;
			else if (_CommandLine.f_StartsWith("PhaseScriptExecution"))
				++m_CommandCounts.m_nPhaseScript;
			else if (_CommandLine.f_StartsWith("ProcessPCH"))
				++m_CommandCounts.m_nProcessPCH;
			else
				++m_CommandCounts.m_nOther;
		}

		TCFuture<void> f_UpdateProgress(bool _bForce)
		{
			if (!m_Config.m_bShowProgress && !_bForce)
				co_return {};

			auto CurrentTime = m_LastProgressUpdate.f_GetTime();

			constexpr static auto c_TimeInterval = 33.3333333333_ms;

			if (m_bProgressShown && CurrentTime < c_TimeInterval && !_bForce)
				co_return {};

			m_LastProgressUpdate.f_AddOffset(c_TimeInterval);
			m_bProgressShown = true;

			auto AnsiEncoding = m_Config.m_pCommandLine->f_AnsiEncoding();

			auto TableRenderer = m_Config.m_pCommandLine->f_TableRenderer();

			CTableRenderHelper::CColumnHelper Columns(1);

			umint nOutputRows = 5;

			Columns.f_AddHeading("Total", 0);
			Columns.f_AddHeading("{}{}{ns }{}"_f << AnsiEncoding.f_StatusNormal() << AnsiEncoding.f_Bold() << m_CommandCounts.f_GetTotal() << AnsiEncoding.f_Default(), 0);

			Columns.f_SetSortByColumns({"Timestamp"});

			TableRenderer.f_AddHeadings(&Columns);
			TableRenderer.f_SetOptions(CTableRenderHelper::EOption_Rounded | CTableRenderHelper::EOption_AvoidRowSeparators);

			auto UnimportantRGB = AnsiEncoding.f_ForegroundRGB(128, 128, 128);

			auto fAddCategory = [&](ch8 const *_pName, umint _nCount, bool _bImportant)
				{
					++nOutputRows;
					TableRenderer.f_AddRow
						(
							_pName
							, _nCount ? (_bImportant ? CStr("{ns }"_f << _nCount) : CStr("{}{ns }{}"_f << UnimportantRGB << _nCount << AnsiEncoding.f_Default())) : CStr()
						)
					;
				}
			;

			fAddCategory("PCH", m_CommandCounts.m_nProcessPCH, true);
			fAddCategory("Compile", m_CommandCounts.m_nCompile, true);
			fAddCategory("Link", m_CommandCounts.m_nLink, true);
			fAddCategory("Script", m_CommandCounts.m_nPhaseScript, false);
			fAddCategory("Other", m_CommandCounts.m_nOther, false);

			if (m_LastOutputProgressRows)
				*m_Config.m_pCommandLine += "{}\r"_f << AnsiEncoding.f_MovePreviousLine(m_LastOutputProgressRows);
			else if (m_Config.m_bShowProgress)
				*m_Config.m_pCommandLine += "\n{}"_f << AnsiEncoding.f_ShowCursor(false);
			else
				*m_Config.m_pCommandLine += "\n";

			TableRenderer.f_Output(CTableRenderHelper::EOutputType_HumanReadable);
			m_LastOutputProgressRows = nOutputRows;

			co_return {};
		}

		TCFuture<void> f_ClearProgress()
		{
			if (!m_bProgressShown && m_CommandCounts.f_GetTotal() == 0)
				co_return {};

			co_await f_UpdateProgress(true);

			auto AnsiEncoding = m_Config.m_pCommandLine->f_AnsiEncoding();
			if (m_Config.m_bShowProgress)
				co_await m_Config.m_pCommandLine->f_StdOut("\n{}"_f << AnsiEncoding.f_ShowCursor(true));
			else
				co_await m_Config.m_pCommandLine->f_StdOut("\n");

			m_bProgressShown = false;

			co_return {};
		}

		void f_StartNewCommand(CStr const &_CommandLine)
		{
			m_CurrentCommandKey = _CommandLine;
			auto &CommandInfo = m_Commands[m_CurrentCommandKey];
			CommandInfo.m_CommandLine = _CommandLine;

			f_IncrementCommandCount(_CommandLine);
			m_pPendingIncludeLineFix = nullptr;
		}

		static CStr fs_ExtractCommandKey(CStr const &_Line)
		{
			return _Line.f_Trim();
		}

		CStr *f_AddCommandOutput(CStr const &_Line)
		{
			if (m_CurrentCommandKey.f_IsEmpty())
			{
				m_NonCommandBufferedOutput.f_Insert(_Line);
				return &m_NonCommandBufferedOutput.f_GetLast();
			}

			auto *pCommand = m_Commands.f_FindEqual(m_CurrentCommandKey);
			if (pCommand)
			{
				pCommand->m_BufferedOutput.f_Insert(_Line);
				return &pCommand->m_BufferedOutput.f_GetLast();
			}

			return nullptr;
		}

		TCFuture<void> f_OutputLinesBatched(TCVector<CStr> _Lines)
		{
			CStr Output;
			bool bWasEmpty = false;
			for (auto const &Line : _Lines)
			{
				if (Line.f_Trim().f_IsEmpty())
				{
					if (bWasEmpty)
						continue;
					bWasEmpty = true;
				}
				else
					bWasEmpty = false;

				Output += "{}\n"_f << Line;
				if (Output.f_GetLen() >= 8192)
				{
					co_await m_Config.m_pCommandLine->f_StdOut(fg_Move(Output));
					Output.f_Clear();
				}
			}

			if (Output)
				co_await m_Config.m_pCommandLine->f_StdOut(fg_Move(Output));

			co_return {};
		}

		TCFuture<void> f_OutputFailedCommands()
		{
			m_pPendingIncludeLineFix = nullptr;

			co_await f_ClearProgress();

			if (m_Config.m_bVerbose)
				co_await m_Config.m_pCommandLine->f_StdErr("[DEBUG] Failed commands count: {}\n"_f << m_FailedCommands.f_GetLen());

			if (m_FailedCommands.f_IsEmpty())
			{
				if (m_BuildResult && *m_BuildResult)
				{
					co_await m_Config.m_pCommandLine->f_StdOut("Build completed successfully in {}\n\n"_f << fg_SecondsDurationToHumanReadable(m_BuildStopClock.f_GetTime()));

					co_return {};
				}

				co_await f_OutputLinesBatched(fg_Move(m_NonCommandBufferedOutput));

				co_await m_Config.m_pCommandLine->f_StdOut("\nBuild failed after {}\n\n"_f << fg_SecondsDurationToHumanReadable(m_BuildStopClock.f_GetTime()));

				co_return {};
			}

			if (m_Config.m_bVerbose)
				co_await m_Config.m_pCommandLine->f_StdErr("[DEBUG] Total commands stored: {}\n"_f << m_Commands.f_GetLen());

			co_await f_OutputLinesBatched(fg_Move(m_NonCommandBufferedOutput));

			TCVector<CStr> NotFoundCommands;

			for (auto const &FailedCommand : m_FailedCommands)
			{
				if (m_Config.m_bVerbose)
					co_await m_Config.m_pCommandLine->f_StdErr("[DEBUG] Looking for failed command: {}\n"_f << FailedCommand);

				auto *pCommand = m_Commands.f_FindEqual(FailedCommand);
				if (!pCommand)
				{
					if (m_Config.m_bVerbose)
						co_await m_Config.m_pCommandLine->f_StdErr("[DEBUG] Command not found in map!\n");
					NotFoundCommands.f_Insert(FailedCommand);
					continue;
				}

				if (m_Config.m_bVerbose)
					co_await m_Config.m_pCommandLine->f_StdErr("[DEBUG] Found command with {} lines of output\n"_f << pCommand->m_BufferedOutput.f_GetLen());

				if (pCommand->m_BufferedOutput.f_IsEmpty())
					continue;

				co_await m_Config.m_pCommandLine->f_StdOut("\n================================================================================\n\n");

				co_await f_OutputLinesBatched(fg_Move(pCommand->m_BufferedOutput));
			}

			if (!NotFoundCommands.f_IsEmpty())
			{
				co_await m_Config.m_pCommandLine->f_StdOut("\n================================================================================\n");
				co_await m_Config.m_pCommandLine->f_StdOut("Other things that failed:\n");
				co_await m_Config.m_pCommandLine->f_StdOut("================================================================================\n\n");

				for (auto const &Command : NotFoundCommands)
					co_await m_Config.m_pCommandLine->f_StdOut("{}\n"_f << Command);
			}

			co_await m_Config.m_pCommandLine->f_StdOut("\nBuild failed after {}\n\n"_f << fg_SecondsDurationToHumanReadable(m_BuildStopClock.f_GetTime()));

			co_return {};
		}

		CStr f_AddInput(CIOByteVector &&_Input, bool _bFlush)
		{
			m_BufferedInput.f_AddStr((ch8 const *)_Input.f_GetArray(), _Input.f_GetLen());

			CStr FinishedInput;

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
					fg_ParseToEndOfLine(pParse);
					auto *pEndOfLine = pParse;
					fg_ParseEndOfLine(pParse);
					if (pParse != pEndOfLine)
						pFinishedOutput = pParse;
					else
						break;
				}
				umint nFinishedChars = pFinishedOutput - pStartParse;
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

		TCFuture<void> f_ProcessInput(CIOByteVector _Input, bool _bFlush)
		{
			auto &Config = m_Config;
			auto pCommandLine = Config.m_pCommandLine;

			CStr ToProcess = f_AddInput(fg_Move(_Input), _bFlush);
			if (!ToProcess)
			{
				if (_bFlush)
				{
					if (Config.m_bVerbose)
						co_await pCommandLine->f_StdErr("[DEBUG] Flushing - about to output failed commands\n");

					co_await f_OutputFailedCommands();
				}
				co_return {};
			}

			auto Lines = ToProcess.f_SplitLine();
			Lines.f_PopBack();

			for (auto &Line : Lines)
			{
				if (Config.m_bPassThrough)
				{
					m_pPendingIncludeLineFix = nullptr;
					co_await pCommandLine->f_StdOut("{}\n"_f << fg_Move(Line));
					continue;
				}

				auto CleanLine = CAnsiEncodingParse::fs_StripEncoding(Line);

				if (m_pPendingIncludeLineFix)
				{
					if (fs_IsSplitIncludeLineNumber(CleanLine))
					{
						*m_pPendingIncludeLineFix += CleanLine;
						m_pPendingIncludeLineFix = nullptr;
						continue;
					}

					m_pPendingIncludeLineFix = nullptr;
				}

				if
				(
					CleanLine.f_StartsWith("    Target ")
					|| CleanLine.f_StartsWith(str_utf8("        ➜ Explicit dependency on target"))
				)
				{
					continue; // Ignore
				}

				auto fMaybeTrackSplitInclude = [&](CStr *_pStoredLine)
					{
						if (!_pStoredLine)
							return;

						if (fs_IsSplitIncludeFirstLine(Line))
							m_pPendingIncludeLineFix = _pStoredLine;
					}
				;

				auto Trimmed = CleanLine.f_Trim();
				if (Trimmed.f_IsEmpty())
					continue;

				if (Trimmed.f_StartsWith("The following build commands failed:"))
				{
					m_bInFailureReport = true;
					if (Config.m_bVerbose)
						co_await pCommandLine->f_StdErr("[DEBUG] Entering failure report section\n");
					continue;
				}

				if (m_bInFailureReport)
				{
					if (Trimmed.f_Find(" failure") >= 0 && Trimmed.f_EndsWith(")"))
					{
						m_bInFailureReport = false;
						continue;
					}

					if (!Trimmed.f_IsEmpty())
					{
						if (Config.m_bVerbose)
							co_await pCommandLine->f_StdErr("[DEBUG] Adding failed command: {}\n"_f << Trimmed.f_Extract(0, 50));
						m_FailedCommands.f_Insert(Trimmed);
					}
					continue;
				}

				if (fs_IsBuildCommand(Trimmed))
				{
					if (Config.m_bVerbose)
						co_await pCommandLine->f_StdErr("[DEBUG] Starting new command: {}\n"_f << Trimmed.f_Extract(0, 50));
					f_StartNewCommand(Trimmed);

					co_await f_UpdateProgress(false);

					if (Config.m_bShowCommands)
						co_await pCommandLine->f_StdOut("{}\n"_f << Line);
					continue;
				}

				fMaybeTrackSplitInclude(f_AddCommandOutput(Line));
				if (Config.m_bShowCommands)
					co_await pCommandLine->f_StdOut("{}\n"_f << Line);

				if (Trimmed.f_StartsWith("** BUILD FAILED **"))
				{
					m_BuildResult = false;
					continue;
				}
				else if (Trimmed.f_StartsWith("** BUILD SUCCEEDED **"))
				{
					m_BuildResult = true;
					continue;
				}
			}

			if (_bFlush)
			{
				if (Config.m_bVerbose)
					co_await pCommandLine->f_StdErr("[DEBUG] Flush at end of lines loop\n");

				co_await f_OutputFailedCommands();
			}

			co_return {};
		}
	};

	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, CStr const &_ClassName
		)
	{
		bool bDefaultShowProgress = true;

		if (auto Value = fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildShowProgress", "").f_LowerCase(); Value)
			bDefaultShowProgress = Value == "true";
		else
		{
			if (fg_GetSys()->f_GetEnvironmentVariable("BUILDSERVER", "").f_LowerCase() == "true")
				bDefaultShowProgress = false;

			if (fg_GetSys()->f_GetEnvironmentVariable("GITHUB_ACTION", "").f_LowerCase() != "")
				bDefaultShowProgress = false;
		}

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["XcodeBuildFilter"]
					, "Description"_o= "Filter and summarize xcodebuild output.\n"
					"Groups messages per target and keeps warnings/errors visible.\n"
					, "Category"_o= "Build"
					, "Options"_o=
					{
						"Verbose?"_o=
						{
							"Names"_o= _o["--verbose", "-v"]
							, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildVerbose", "false").f_LowerCase() == "true"
							, "Description"_o= "Show verbose trace information.\n"
						}
						, "PassThrough?"_o=
						{
							"Names"_o= _o["--pass-through"]
							, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildPassThrough", "false").f_LowerCase() == "true"
							, "Description"_o= "Pass through all input without filtering (for testing).\n"
						}
						, "ShowCommands?"_o=
						{
							"Names"_o= _o["--show-commands"]
							, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildShowCommands", "false").f_LowerCase() == "true"
							, "Description"_o= "Keep raw build command lines (CompileC, Ld, etc.).\n"
						}
						, "ShowProgress?"_o=
						{
							"Names"_o= _o["--show-progress"]
							, "Default"_o= bDefaultShowProgress
							, "Description"_o= "Show progress while compile is ongoing\n"
						}
					}
					, "Parameters"_o=
					{
						"InputFile?"_o=
						{
							"Type"_o= ""
							, "Description"_o= "Optional file to read log data from. Default is stdin."
						}
					}
				}
				, [=](NEncoding::CEJsonSorted const _Params, TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					bool bVerbose = _Params["Verbose"].f_Boolean();
					bool bPassThrough = _Params["PassThrough"].f_Boolean();
					bool bShowCommands = _Params["ShowCommands"].f_Boolean();
					bool bShowProgress = _Params["ShowProgress"].f_Boolean();

					TCOptional<CStr> InputFile;

					if (auto *pValue = _Params.f_GetMember("InputFile"))
						InputFile = pValue->f_String();

					TCActor<CStdInActor> StdInActor = fg_Construct<CStdInActor>();

					NConcurrency::TCPromiseFuturePair<void> EofDone;
					NConcurrency::TCPromiseFuturePair<void> Cancelled;

					TCSharedPointer<CState> pState = fg_Construct
						(
							CConfig
							{
								.m_pCommandLine = _pCommandLine
								, .m_bVerbose = bVerbose
								, .m_bPassThrough = bPassThrough
								, .m_bShowCommands = bShowCommands
								, .m_bShowProgress = bShowProgress
							}
						)
					;

					if (InputFile)
					{
						if (bVerbose)
							co_await _pCommandLine->f_StdErr("[DEBUG] Verbose mode enabled, reading from file\n");
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
									, bPassThrough
									, EofPromise = fg_Move(EofDone.m_Promise)
									, pState
								]
								(EStdInReaderOutputType _Type, CIOByteVector _Input, CStr _Error) -> TCFuture<void>
								{
									auto Subscription = co_await pState->m_InputSequencer.f_Sequence();

									if (pState->m_bEndOfFileReceived)
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

											co_await pState->f_ProcessInput(fg_Move(_Input), false);
										}
										break;
									case NProcess::EStdInReaderOutputType_GeneralError:
										{
											co_await pCommandLine->f_StdErr("[ERROR] {}\n"_f << _Error);
										}
										break;
									case NProcess::EStdInReaderOutputType_EndOfFile:
										{
											pState->m_bEndOfFileReceived = true;
											co_await pState->f_ProcessInput({}, true);
											EofPromise.f_SetResult();
										}
										break;
									}

									co_return {};
								}
								, NProcess::EStdInReaderFlag_None
								, 1024 * 1024
							)
						;

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

						co_await NConcurrency::fg_AnyDone(fg_Move(EofDone.m_Future), fg_Move(Cancelled.m_Future));

						co_await StdInActor(&CStdInActor::f_AbortReads);
						co_await fg_Move(StdInActor).f_Destroy();
					}

					co_return 0;
				}
			)
		;
	}
};

DMibRuntimeClass(CTool, CTool_XcodeBuildFilter);
