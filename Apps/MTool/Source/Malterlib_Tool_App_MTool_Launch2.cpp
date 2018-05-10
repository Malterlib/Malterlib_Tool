// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Process/VirtualProcessLaunch>

class CTool_Launch : public CTool
{
public:

	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		TCVector<CStr> lArgs;

		int iArg = 0;
		CStr Arg;
		while ((Arg = _Params.f_GetValue(CStr::fs_ToStr(iArg), "012301231023Error")) != "012301231023Error")
		{
			lArgs.f_Insert(Arg);
			++iArg;
		}

		return fp_Run(lArgs);
	}

private:

	struct CLaunchOptions
	{
	private:
		enum EField
		{
				EField_None				= 0
			,	EField_Time				= DMibBit(0)
			,	EField_PerforceParsing	= DMibBit(1)
			,	EField_SeparateStdErr 	= DMibBit(2)
			,	EField_DelayOutput		= DMibBit(3)
			,	EField_Sandbox			= DMibBit(4)
			, 	EField_SandboxCopyRoot	= DMibBit(5)
			,	EField_CPUUsage			= DMibBit(7)
			,	EField_AttributeOutput 	= DMibBit(8)
			,	EField_RedirectStdOut	= DMibBit(9)
			,	EField_EchoCommand		= DMibBit(10)
			,	EField_Stats			= DMibBit(11)
			,	EField_LimitConcurrency	= DMibBit(12)
		};

		EField m_Fields;

	public:
		int m_iLaunch;

		bint m_bTime;
		bint m_bStats;
		bint m_bPerforceParsing;
		bint m_bSeparateStdErr;
		bint m_bDelayOutput;
		bint m_bAttributeOutput;
		bint m_bEchoCommand;

		bint m_bSandbox;
		bint m_bSandboxCopyRoot;
		TCMap<CStr, CStr> m_SandboxRoots;

		fp32 m_CPUUsage;
		mint m_LimitConcurrency;

		CStr m_RedirectStdOut;

		CStr m_Target;
		TCVector<CStr> m_lParams;

		CLaunchOptions()
			: m_Fields(EField_None)
			, m_iLaunch(0)			
			, m_bTime(false)
			, m_bStats(false)
			, m_LimitConcurrency(false)
			, m_bPerforceParsing(false)
			, m_bSeparateStdErr(false)
			, m_bEchoCommand(false)
			, m_bDelayOutput(false)
			, m_bAttributeOutput(false)
			, m_bSandbox(false)
			, m_bSandboxCopyRoot(false)
			, m_CPUUsage(0.0f)
		{}

		CLaunchOptions(CLaunchOptions&& _ToMove)
			: m_Fields(_ToMove.m_Fields)
			, m_iLaunch(_ToMove.m_iLaunch)			
			, m_bTime(_ToMove.m_bTime)
			, m_bStats(_ToMove.m_bStats)
			, m_LimitConcurrency(_ToMove.m_LimitConcurrency)
			, m_bPerforceParsing(_ToMove.m_bPerforceParsing)
			, m_bSeparateStdErr(_ToMove.m_bSeparateStdErr)
			, m_bEchoCommand(_ToMove.m_bEchoCommand)
			, m_bDelayOutput(_ToMove.m_bDelayOutput)
			, m_bAttributeOutput(_ToMove.m_bAttributeOutput)
			, m_bSandbox(_ToMove.m_bSandbox)
			, m_bSandboxCopyRoot(_ToMove.m_bSandboxCopyRoot)
			, m_SandboxRoots(fg_Move(_ToMove.m_SandboxRoots))
			, m_CPUUsage(_ToMove.m_CPUUsage)
			, m_RedirectStdOut(fg_Move(_ToMove.m_RedirectStdOut))
			, m_Target(fg_Move(_ToMove.m_Target))
			, m_lParams(fg_Move(_ToMove.m_lParams))
		{
			_ToMove = CLaunchOptions();
		}

		CLaunchOptions& operator=(CLaunchOptions&& _ToMove)
		{
			m_Fields = _ToMove.m_Fields;
			m_iLaunch = _ToMove.m_iLaunch;
			m_bTime = _ToMove.m_bTime;
			m_bStats = _ToMove.m_bStats;
			m_LimitConcurrency = _ToMove.m_LimitConcurrency;
			m_bPerforceParsing = _ToMove.m_bPerforceParsing;
			m_bSeparateStdErr = _ToMove.m_bSeparateStdErr;
			m_bEchoCommand = _ToMove.m_bEchoCommand;
			m_bDelayOutput = _ToMove.m_bDelayOutput;
			m_bAttributeOutput = _ToMove.m_bAttributeOutput;
			m_bSandbox = _ToMove.m_bSandbox;
			m_bSandboxCopyRoot = _ToMove.m_bSandboxCopyRoot;
			m_SandboxRoots = fg_Move(_ToMove.m_SandboxRoots);
			m_CPUUsage = _ToMove.m_CPUUsage;
			m_RedirectStdOut = fg_Move(_ToMove.m_RedirectStdOut);
			m_Target = fg_Move(_ToMove.m_Target);
			m_lParams = fg_Move(_ToMove.m_lParams);
			return *this;
		}

		void f_SetTime(bint _bValue) { m_Fields |= EField_Time; m_bTime = _bValue; }
		void f_SetStats(bint _bValue) { m_Fields |= EField_Stats; m_bStats = _bValue; }
		void f_SetLimitConcurrency(mint _Value) { m_Fields |= EField_LimitConcurrency; m_LimitConcurrency = _Value; }
		void f_SetPerforceParsing(bint _bValue) { m_Fields |= EField_PerforceParsing; m_bPerforceParsing = _bValue; }
		void f_SetSeparateStdErr(bint _bValue) { m_Fields |= EField_SeparateStdErr; m_bSeparateStdErr = _bValue; }
		void f_SetEchoCommand(bint _bValue) { m_Fields |= EField_EchoCommand; m_bEchoCommand = _bValue; }
		void f_SetDelayOutput(bint _bValue) { m_Fields |= EField_DelayOutput; m_bDelayOutput = _bValue; }
		void f_SetSandbox(bint _bValue) { m_Fields |= EField_Sandbox; m_bSandbox = _bValue; }
		void f_SetSandboxCopyRoot(bint _bValue) { m_Fields |= EField_SandboxCopyRoot; m_bSandboxCopyRoot = _bValue; }
		void f_SetCPUUsage(fp32 _Value) { m_Fields |= EField_CPUUsage; m_CPUUsage = _Value; }
		void f_SetRedirectStdOut(CStr const &_Value) { m_Fields |= EField_RedirectStdOut; m_RedirectStdOut = _Value; }
		void f_SetAttributeOutput(bint _bValue) { m_Fields |= EField_AttributeOutput; m_bAttributeOutput = _bValue; }

		void f_ApplyDefaults(CLaunchOptions const& _Defaults)
		{			
			EField DefFields = _Defaults.m_Fields;

			#define ApplyField(_FieldName, _VarName) \
				if ( 	!(m_Fields & EField_##_FieldName) \
					&&	(DefFields & EField_##_FieldName) ) \
					_VarName = _Defaults. _VarName;

			ApplyField(Time, m_bTime);
			ApplyField(Stats, m_bStats);
			ApplyField(LimitConcurrency, m_LimitConcurrency);
			ApplyField(PerforceParsing, m_bPerforceParsing);
			ApplyField(SeparateStdErr, m_bSeparateStdErr);
			ApplyField(EchoCommand, m_bEchoCommand);
			ApplyField(DelayOutput, m_bDelayOutput);
			ApplyField(Sandbox, m_bSandbox);
			ApplyField(SandboxCopyRoot, m_bSandboxCopyRoot);
			ApplyField(CPUUsage, m_CPUUsage);
			ApplyField(RedirectStdOut, m_RedirectStdOut);
			ApplyField(AttributeOutput, m_bAttributeOutput);

			if (m_SandboxRoots.f_IsEmpty())
				m_SandboxRoots = _Defaults.m_SandboxRoots;

			#undef ApplyField
		}


	};

	struct CGlobalOptions
	{
		zbint m_bVerbose;
		CStr m_LockFile;

		CLaunchOptions m_Defaults;
	};


	struct CProcessInfo
	{		
		CLaunchOptions const& m_Options;

		CStr m_StdOutBuffer;
		CStr m_StdErrBuffer;
		TCBinaryStreamFile<> m_StdOutRedir;

		zuint32 m_ReturnValue;
		zbool m_bEchoedCommand;
		zbool m_bFinished;
		
		CProcessLaunchHandler::CLaunchInfo * m_pProcessLaunchInfo;
		
		NMib::NProcess::CProcessStatistics m_SampledMemoryStats;
		
		CProcessInfo(CLaunchOptions const &_Options)
			: m_Options(_Options)
			, m_pProcessLaunchInfo(nullptr)
		{
		}

	};

	CMutual m_ConOutLock;

private:

	bint fp_ParseBool(CStr const& _Value)
	{
		return _Value.f_CmpNoCase("Yes") == 0;
	}

	bint fp_ProcessArg(CStr& _Arg, CLaunchOptions& _CurLaunch, CGlobalOptions& _GlobalObjects, bint& _bInTarget)
	{
		if (_Arg.f_StartsWith("("))
		{
			_Arg = _Arg.f_Extract(1).f_Trim();
			_bInTarget = true;
			return true;
		}

		int iEqual = _Arg.f_Find("=");
		CStr Key, Value;

		if (iEqual == -1)
		{
			Key = _Arg;
			Value = "Yes";
		}
		else
		{
			Key = _Arg.f_Extract(0, iEqual);
			Value = _Arg.f_Extract(iEqual + 1);
		}

		CLaunchOptions* pOptions = &_CurLaunch;

		if (Key.f_StartsWith("+"))
		{
			pOptions = &_GlobalObjects.m_Defaults;
			Key = Key.f_Extract(1);
		}

		// Global
		if (Key.f_CmpNoCase("Verbose") == 0)
			_GlobalObjects.m_bVerbose = fp_ParseBool(Value);
		else if (Key.f_CmpNoCase("LockFile") == 0)
			_GlobalObjects.m_LockFile = Value;
		// Launch Specific
		else if (Key.f_CmpNoCase("RedirectStdOut") == 0)
		{
			pOptions->f_SetRedirectStdOut(Value);
			pOptions->f_SetSeparateStdErr(true);
		}
		else if (Key.f_CmpNoCase("Time") == 0)
			pOptions->f_SetTime(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("Stats") == 0)
			pOptions->f_SetStats(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("LimitConcurrency") == 0)
		{
			mint nLimit = 1;
			if (Value.f_IsEmpty() || Value == "Yes")
				nLimit = NSys::fg_Thread_GetVirtualCores();
			else
				nLimit = Value.f_ToInt(mint(1));
			pOptions->f_SetLimitConcurrency(nLimit);
		}
		else if (Key.f_CmpNoCase("P4") == 0)
			pOptions->f_SetPerforceParsing(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("SepStdError") == 0)
			pOptions->f_SetSeparateStdErr(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("EchoCommand") == 0)
			pOptions->f_SetEchoCommand(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("Delay") == 0)
			pOptions->f_SetDelayOutput(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("AttribOutput") == 0)
			pOptions->f_SetAttributeOutput(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("Sandbox") == 0)
			pOptions->f_SetSandbox(fp_ParseBool(Value));
		else if (Key.f_CmpNoCase("CopyRoot") == 0)
			pOptions->f_SetSandboxCopyRoot(fp_ParseBool(Value));
		else if (Key.f_StartsWith("Root"))
		{
			CStr Target = fg_GetStrSep(Value, "=");
			CStr Source = fg_GetStrSep(Value, "=");

			pOptions->m_SandboxRoots[Target] = Source;
		}
		else if (Key.f_CmpNoCase("CPUUsage") == 0)
			pOptions->f_SetCPUUsage(Value.f_ToFloat<fp32>(0.0f));
		else
			return false;

		return true;

	}

	void fp_OutputWholeLines(CStr &_Buffer, EProcessLaunchOutputType _Type, CLaunchOptions const& _Options, bool _bFlush)
	{
		auto &Buffer = _Buffer;

		CStr LinePrefix;
		if (_Options.m_bAttributeOutput)
			LinePrefix = CStr::CFormat("{}> ") << _Options.m_iLaunch;

		CStr ToSend;

		ch8 const *pLineStart = Buffer;
		ch8 const* pLineEnd = pLineStart;

		bint bPerforceParsing = _Options.m_bPerforceParsing;

		while(*pLineStart)
		{
			ch8 const* pLineEnd2 = pLineStart;
			fg_ParseToEndOfLine(pLineEnd2);
			if (!fg_ParseEndOfLine(pLineEnd2))
				break;

			pLineEnd = pLineEnd2;

			if (	_Type == EProcessLaunchOutputType_StdOut
				&& 	bPerforceParsing
				&& 	(
							fg_StrStartsWith(pLineStart, "info")
						||	fg_StrStartsWith(pLineStart, "exit")
					))
			{
				// Supress
			}
			else
			{
				ToSend += LinePrefix;
				ToSend += CStr(pLineStart, pLineEnd - pLineStart);
			}

			pLineStart = pLineEnd;
		}

		Buffer = Buffer.f_Extract(pLineEnd - (ch8 const*)Buffer);

		if (_bFlush)
		{
			ToSend += Buffer;
			Buffer.f_Clear();
		}

		if (!ToSend.f_IsEmpty())
		{
			if (_Type == EProcessLaunchOutputType_StdErr)
				DConErrOutRaw(ToSend);
			else
				DConOutRaw(ToSend);
		}		
	}
	
	mint fp_DisplayStatsGetMaxLen(NMib::NProcess::CProcessStatistics const &_Stats)
	{
		mint Len = 0;
		for (auto Iter = _Stats.m_Statistics.f_GetIterator(); Iter; ++Iter)
			Len = fg_Max(Len, mint(Iter.f_GetKey().f_GetLen()));
		
		return Len;
	}
	
	void fp_DisplayStats(NMib::NProcess::CProcessStatistics const &_Stats, mint _NameLen)
	{
		for (auto Iter = _Stats.m_Statistics.f_GetIterator(); Iter; ++Iter)
		{
			fp64 Value = Iter->m_Value;
			NMib::NStr::CStr Name = Iter.f_GetKey();
			CStr Unit;
			switch (Iter->m_Unit)
			{
			case NMib::NProcess::EProcessStatUnit_GeneralNumber:
				{
					if (!Iter->m_CustomUnit.f_IsEmpty())
						Unit = Iter->m_CustomUnit;
				}
				break;
			case NMib::NProcess::EProcessStatUnit_Bytes:
				{
					if (Iter->m_IdealScale == fp64(1024*1024*1024))
					{
						Value /= Iter->m_IdealScale;
						Unit = "GiB";
					}
					else if (Iter->m_IdealScale == fp64(1024*1024))
					{
						Value /= Iter->m_IdealScale;
						Unit = "MiB";
					}
					else if (Iter->m_IdealScale == fp64(1024))
					{
						Value /= Iter->m_IdealScale;
						Unit = "KiB";
					}
					else
						Unit = "Bytes";
				}
				break;
			case NMib::NProcess::EProcessStatUnit_Cycles:
				{
					Unit = "Cycles";
				}
				break;
			case NMib::NProcess::EProcessStatUnit_Seconds:
				{
					if (Iter->m_IdealScale == fp64(0.000000001))
					{
						Value /= Iter->m_IdealScale;
						Unit = "ns";
					}
					else if (Iter->m_IdealScale == fp64(0.000001))
					{
						Value /= Iter->m_IdealScale;
						Unit = "µs";
					}
					else if (Iter->m_IdealScale == fp64(0.001))
					{
						Value /= Iter->m_IdealScale;
						Unit = "ms";
					}
					else
						Unit = "s";
				}
				break;
			case NMib::NProcess::EProcessStatUnit_Fraction:
				{
					Value *= 100.0;
					Unit = "%";
				}
				break;
			default:
				DMibNeverGetHere(Iter->m_Unit);
				break;
			}
			DConOut("{a-,sj*} = {fe2} {}{\n}", Name << _NameLen << Value << Unit);
		}

	}
	void fg_UpdateSampledStats(CProcessInfo &_ProcessInfo)
	{
		auto ThisStat = _ProcessInfo.m_pProcessLaunchInfo->f_GetProcessLaunch()->f_GetMemoryStatistics();
		for (auto iStat = ThisStat.m_Statistics.f_GetIterator(); iStat; ++iStat)
		{
			auto Mapped = _ProcessInfo.m_SampledMemoryStats.m_Statistics("(Sampled Max) " + iStat.f_GetKey(), *iStat);
			if (!Mapped.f_WasCreated())
			{
				if (iStat->m_Value > (*Mapped).m_Value)
					(*Mapped).m_Value = iStat->m_Value;
			}
		}
	}
	
	TCSharedPointer<CProcessInfo> fp_CreateProcess(CLaunchOptions const& _Options, CProcessLaunchHandler& _Handler)
	{
		TCSharedPointer<CProcessInfo> pProc = fg_Construct(_Options);
		
		if (!_Options.m_RedirectStdOut.f_IsEmpty())
			pProc->m_StdOutRedir.f_Open(_Options.m_RedirectStdOut, EFileOpen_Write | EFileOpen_ShareAll);

		CStr Params = CProcessLaunchParams::fs_GetParams(_Options.m_lParams);
		
		auto fl_EchoCommand
			= [pProc, Params]
			{
				CLaunchOptions const &Options = pProc->m_Options;
				if (Options.m_bEchoCommand && !pProc->m_bEchoedCommand)
				{
					pProc->m_bEchoedCommand = true;
					DConOut("\n<-------------------\n{} {}\n\n", Options.m_Target << Params);
				}
				
			}
		;

		CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutable
			(
					_Options.m_Target 
				, 	Params
				,	CFile::fs_GetCurrentDirectory()
				, 	[this, pProc, fl_EchoCommand](CProcessLaunchStateChangeVariant const &_StateChange, fp64 _TimeSinceLaunch)
					{
						CLaunchOptions const& Options = pProc->m_Options;

						if (_StateChange.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
						{
							DLock(m_ConOutLock);
							DConOut("error: Failed to launch #{} {}: {}", Options.m_iLaunch << Options.m_Target << _StateChange.f_Get<EProcessLaunchState_LaunchFailed>());
							pProc->m_ReturnValue = 255u;
						}
						else if (_StateChange.f_GetTypeID() == EProcessLaunchState_Exited)
						{							
							DLock(m_ConOutLock);

							fl_EchoCommand();
							fp_OutputWholeLines(pProc->m_StdOutBuffer, EProcessLaunchOutputType_StdOut, Options, true);
							fp_OutputWholeLines(pProc->m_StdErrBuffer, EProcessLaunchOutputType_StdErr, Options, true);

							if (Options.m_bTime)
							{
								DConOut("Launch #{} {} took {} seconds to run." DNewLine, Options.m_iLaunch << Options.m_Target << _TimeSinceLaunch);
							}
							
							if (Options.m_bStats)
							{
								NMib::NProcess::CProcessStatistics MemoryStats = pProc->m_pProcessLaunchInfo->f_GetProcessLaunch()->f_GetOverallMemoryStatistics();
								NMib::NProcess::CProcessStatistics ExecutionStats = pProc->m_pProcessLaunchInfo->f_GetProcessLaunch()->f_GetOverallExecutionStatistics();
								
								mint MaxLen = 0;
								MaxLen = fg_Max(fp_DisplayStatsGetMaxLen(MemoryStats), MaxLen);
								MaxLen = fg_Max(fp_DisplayStatsGetMaxLen(ExecutionStats), MaxLen);
								MaxLen = fg_Max(fp_DisplayStatsGetMaxLen(pProc->m_SampledMemoryStats), MaxLen);
								
								fp_DisplayStats(MemoryStats, MaxLen);
								fp_DisplayStats(ExecutionStats, MaxLen);
								fp_DisplayStats(pProc->m_SampledMemoryStats, MaxLen);
							}

							uint32 ThisValue = _StateChange.f_Get<EProcessLaunchState_Exited>();
							if (ThisValue != 0)
							{
								DConOut("error: Launch #{} {} exited with {}" DNewLine,  Options.m_iLaunch << Options.m_Target << ThisValue);
							}
							pProc->m_ReturnValue = ThisValue;
							if (Options.m_bEchoCommand)
							{
								pProc->m_bEchoedCommand = true;
								DConOut("\n------------------->\n", 0);
							}

						}
					}
			)
		;
		
		LaunchParams.m_fOnOutput 
			= [this, pProc, fl_EchoCommand](EProcessLaunchOutputType _OutputType, CStr const &_Output)
			{
				CLaunchOptions const& Options = pProc->m_Options;

				switch (_OutputType)
				{
				case EProcessLaunchOutputType_GeneralError:
				case EProcessLaunchOutputType_StdErr:
				case EProcessLaunchOutputType_TerminateMessage:
					{
						pProc->m_StdErrBuffer += _Output;
						DLock(m_ConOutLock);
						fp_OutputWholeLines(pProc->m_StdErrBuffer, EProcessLaunchOutputType_StdErr, Options, false);
					}
					break;
				case EProcessLaunchOutputType_StdOut:
					{
						if (pProc->m_StdOutRedir.m_File.f_IsValid())
						{
							if (pProc->m_StdOutRedir.f_GetPosition() == 0)
							{
								TCVector<uint8> Vector;
								CFile::fs_WriteStringToVector(Vector, _Output, true);
								pProc->m_StdOutRedir.f_FeedBytes(Vector.f_GetArray(), Vector.f_GetLen());
							}
							else
							{
								CStr Temp = _Output;
								pProc->m_StdOutRedir.f_FeedBytes(Temp.f_GetStr(), Temp.f_GetLen());
							}
						}
						else
						{
							pProc->m_StdOutBuffer += _Output;
							DLock(m_ConOutLock);
							fl_EchoCommand();
							fp_OutputWholeLines(pProc->m_StdOutBuffer, EProcessLaunchOutputType_StdOut, Options, false);
						}
						
					}
					break;
				}
			}
		;

		LaunchParams.m_bSeparateStdErr = _Options.m_bSeparateStdErr;

		LaunchParams.m_bSandboxed = _Options.m_bSandbox;
		LaunchParams.m_bCopyRootToSandbox = _Options.m_bSandboxCopyRoot;
		LaunchParams.m_SandboxRoots = _Options.m_SandboxRoots;

		LaunchParams.m_bShowLaunched = false;
		LaunchParams.m_bAllowExecutableLocate = true;
		LaunchParams.m_CPUUsage = _Options.m_CPUUsage;

		pProc->m_pProcessLaunchInfo = _Handler.f_AddLaunch(LaunchParams, _Options.m_bDelayOutput);

		return pProc;
	}

	aint fp_Run(TCVector<CStr> const &_Args)
	{
		CGlobalOptions GlobalOptions;
		TCVector<CLaunchOptions> lLaunchOptions;

		// Parse Args
		{
			CLaunchOptions CurLaunch;

			bint bInTarget = false;
			int iLaunch = 0;

			for (auto AIter = _Args.f_GetIterator()
				;AIter
				;++AIter)
			{
				bint bSubmitCurTarget = false;

				if (bInTarget)
				{
					CStr Value = *AIter;

					mint nChars = Value.f_GetLen();
					if (Value.f_GetAt(nChars - 1) == ')')
					{
						Value = Value.f_Extract(0, nChars-1);
						bSubmitCurTarget = true;
					}

					if (!Value.f_IsEmpty())
					{
						if (CurLaunch.m_Target.f_IsEmpty())
							CurLaunch.m_Target = Value;
						else
							CurLaunch.m_lParams.f_Insert(Value);
					}
				}
				else
				{
					CStr Arg = *AIter;

					if (!fp_ProcessArg(Arg, CurLaunch, GlobalOptions, bInTarget))
					{
						DError(CStr::CFormat("Unknown argument: \"{}\"" DNewLine) << Arg);
					}

					if (bInTarget)
					{
						CurLaunch.m_Target = Arg;	 // This may be empty here if there is a space after the (, but that is OK!
						if (!CurLaunch.m_Target.f_IsEmpty())
						{
							mint nChars = CurLaunch.m_Target.f_GetLen();
							if (CurLaunch.m_Target.f_GetAt(nChars - 1) == ')')
							{
								CurLaunch.m_Target = CurLaunch.m_Target.f_Extract(0, nChars-1);
								bSubmitCurTarget = true;
							}
						}

					}
				}

				if (bSubmitCurTarget)
				{
					CurLaunch.m_iLaunch = iLaunch;
					++iLaunch;
					lLaunchOptions.f_Insert(fg_Move(CurLaunch));

					CurLaunch = CLaunchOptions();
					bSubmitCurTarget = false;
					bInTarget = false;
				}
			}	

		}

		// Apply Defaults
		{
			for (auto LIter = lLaunchOptions.f_GetIterator()
				;LIter
				;++LIter)
			{
				LIter->f_ApplyDefaults(GlobalOptions.m_Defaults);
			}
		}

		bool bOneHasStats = GlobalOptions.m_Defaults.m_bStats;
		{
			for (auto iLaunchOption = lLaunchOptions.f_GetIterator() ; !bOneHasStats && iLaunchOption; ++iLaunchOption)
			{
				if (iLaunchOption->m_bStats)
					bOneHasStats = true;
			}
		}

		// Dump:
		if (GlobalOptions.m_bVerbose)
		{
			DConOutRaw("Global Options:" DNewLine);
			DConOut("\tVerbose: {}" DNewLine, (GlobalOptions.m_bVerbose ? "Yes" : "No"));
			DConOut("\tLockFile: {}" DNewLine, GlobalOptions.m_LockFile);

			auto fl_DumpOptions = 
				[](CLaunchOptions const& _CurLaunch)
				{
					DConOut("\tTarget: {}" DNewLine, _CurLaunch.m_Target);

					int iParam = 0;
					for (auto PIter = _CurLaunch.m_lParams.f_GetIterator()
						;PIter
						;++PIter)
					{
						DConOut("\tParam {}: {}" DNewLine, iParam << *PIter);
						++iParam;
					}

					DConOut("\tRedirectStdOut: {}" DNewLine, _CurLaunch.m_RedirectStdOut);
					DConOut("\tTimed? {}" DNewLine, (_CurLaunch.m_bTime ? "Yes" : "No"));
					DConOut("\tStats? {}" DNewLine, (_CurLaunch.m_bStats ? "Yes" : "No"));
					DConOut("\tLimitConcurrency: {}" DNewLine, _CurLaunch.m_LimitConcurrency);
					DConOut("\tPerforce Parsing? {}" DNewLine, (_CurLaunch.m_bPerforceParsing ? "Yes" : "No"));
					DConOut("\tSeparate StdErr? {}" DNewLine, (_CurLaunch.m_bSeparateStdErr ? "Yes" : "No"));
					DConOut("\tEcho command? {}" DNewLine, (_CurLaunch.m_bEchoCommand ? "Yes" : "No"));
					DConOut("\tDelay Output? {}" DNewLine, (_CurLaunch.m_bDelayOutput ? "Yes" : "No"));
					DConOut("\tSandbox? {}" DNewLine, (_CurLaunch.m_bSandbox ? "Yes" : "No"));
					if (_CurLaunch.m_bSandbox)
					{
						DConOut("\t\tCopyRoot? {}" DNewLine, (_CurLaunch.m_bSandboxCopyRoot ? "Yes" : "No"));
						for (auto RIter = _CurLaunch.m_SandboxRoots.f_GetIterator()
							;RIter
							;++RIter)
						{
							DConOut("\t\tRoot {} = {}" DNewLine, RIter.f_GetKey() << *RIter);
						}
					}
					DConOut("\tCPUUsage: {}" DNewLine, _CurLaunch.m_CPUUsage);
				};



			int iLaunch = 0;
			for (auto LIter = lLaunchOptions.f_GetIterator()
				;LIter
				;++LIter)
			{
				CLaunchOptions const& CurLaunch = *LIter;

				DConOut("Launch {}:" DNewLine, iLaunch);
				fl_DumpOptions(CurLaunch);

				++iLaunch;
			}
		}

		// Launch
		TCVector<TCSharedPointer<CProcessInfo>> lProcessInfo; // Does not really need to be shared any more.
		{
			NFile::CLockFile LockFile(GlobalOptions.m_LockFile);

			// Use lockfile if set
			if (!GlobalOptions.m_LockFile.f_IsEmpty())
			{
				NFile::CLockFile::ELockResult Result;

				Result = LockFile.f_Lock(-1);

				if (Result != NFile::CLockFile::ELockResult_Locked)
				{
					DError(CStr::CFormat("Failed to lock lockfile. Check that permission are correct: {}") << GlobalOptions.m_LockFile);
				}
			}

			CProcessLaunchHandler LaunchHandler;

			lProcessInfo.f_SetLen(lLaunchOptions.f_GetLen());

			mint nLaunchProcesses = 0;
			mint nMaxLaunchProcesses = TCLimitsInt<mint>::mc_Max;
			if (GlobalOptions.m_Defaults.m_LimitConcurrency)
				nMaxLaunchProcesses = GlobalOptions.m_Defaults.m_LimitConcurrency;
			
			TCLinkedList<TCFunction<void ()>> ToLaunchQueue;
			
			int iLaunch = 0;
			for (auto LIter = lLaunchOptions.f_GetIterator()
				;LIter
				;++LIter)
			{
				auto pLaunchOptions = &*LIter;
				ToLaunchQueue.f_Insert
					(
						[&, iLaunch, pLaunchOptions]
						{
							lProcessInfo[iLaunch] = fp_CreateProcess(*pLaunchOptions, LaunchHandler);
						}
					)
				;
				++iLaunch;
			}
			
			auto fl_LaunchOutstanding
				= [&]
				{
					while (nLaunchProcesses < nMaxLaunchProcesses && !ToLaunchQueue.f_IsEmpty())
					{
						++nLaunchProcesses;
						ToLaunchQueue.f_Pop()();
					}
				}
			;
			
			fl_LaunchOutstanding();

			if (bOneHasStats || GlobalOptions.m_Defaults.m_LimitConcurrency)
			{
				while (!ToLaunchQueue.f_IsEmpty() || LaunchHandler.f_GetFirstNotDone())
				{
					for (auto iProcessInfo = lProcessInfo.f_GetIterator(); iProcessInfo; ++iProcessInfo)
					{
						if (!*iProcessInfo)
							continue; // Not launched yet
						
						auto &ProcessInfo = **iProcessInfo;
			
						if (!ProcessInfo.m_bFinished && !ProcessInfo.m_pProcessLaunchInfo->f_GetProcessLaunch()->f_IsRunning())
						{
							ProcessInfo.m_bFinished = true;
							--nLaunchProcesses; // Allow another process to launch
						}
						
						if (ProcessInfo.m_Options.m_bStats)
							fg_UpdateSampledStats(*(*iProcessInfo));
					}
					fl_LaunchOutstanding();
					LaunchHandler.f_WaitForChange(0.010f);
				}
			}
			else
				LaunchHandler.f_BlockOnExit();

			if (LockFile.f_HasLock())
				LockFile.f_Unlock();
		}

		// Return
		uint32 ReturnValue = 0;
		{
			for (auto PIter = lProcessInfo.f_GetIterator()
				;PIter
				;++PIter)
			{
				ReturnValue = fg_Max(ReturnValue, (*PIter)->m_ReturnValue);
			}
		}		
		return ReturnValue;
	}

};

DMibRuntimeClass(CTool, CTool_Launch);
