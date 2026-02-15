// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Process/VirtualProcessLaunch>

class CTool_LaunchTimed : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr FileName = _Params.f_GetValue("0", "012301231023Error");
		if (FileName == "012301231023Error")
		{
			DError("Wrong number of parameters");
		}

		CStr Params;
		int iParam = 1;
		CStr Param;
		while ((Param = _Params.f_GetValue(CStr::fs_ToStr(iParam), "012301231023Error")) != "012301231023Error")
		{
			Params += CStr(" ") + Param;
			++iParam;
		}

		int Ret = 0;

		CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutableRawParams
			(
				FileName
				, Params
				, CFile::fs_GetCurrentDirectory()
				, [&](CProcessLaunchStateChangeVariant const &_StateChange, fp64 _TimeSinceLaunch)
				{
					if (_StateChange.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
					{
						DConOut("{}", _StateChange.f_Get<EProcessLaunchState_LaunchFailed>());
						Ret = 255;
					}
					else if (_StateChange.f_GetTypeID() == EProcessLaunchState_Exited)
					{
						DConOut("Program took {} seconds to run." DNewLine, _TimeSinceLaunch);
						Ret = _StateChange.f_Get<EProcessLaunchState_Exited>();
					}
				}
			)
		;

		LaunchParams.m_fOnOutput
			= [&](EProcessLaunchOutputType _OutputType, CStr const &_Output)
			{
				switch (_OutputType)
				{
				case EProcessLaunchOutputType_GeneralError:
				case EProcessLaunchOutputType_StdErr:
				case EProcessLaunchOutputType_TerminateMessage:
					DConErrOutRaw(_Output);
					break;
				case EProcessLaunchOutputType_StdOut:
					DConOutRaw(_Output);
					break;
				case EProcessLaunchOutputType_Max:
					DMibNeverGetHere;
					break;
				}
			}
		;
		{
			CProcessLaunch Launch(LaunchParams, EProcessLaunchCloseFlag_BlockOnExit);
		}

		return Ret;
	}
};

DMibRuntimeClass(CTool, CTool_LaunchTimed);

void fg_OutputWholeLines(CStr &_Buffer, EProcessLaunchOutputType _Type, bool _bFilterP4, bool _bFlush)
{
	auto &Buffer = _Buffer;

	CStr ToSend;

	aint NewLine = Buffer.f_FindCharReverse('\n');
	if (NewLine >= 0)
	{
		ch8 const *pParse = Buffer;
		ch8 const *pParseStart = pParse;
		ch8 const *pToSendStart = pParse;
		ch8 const *pToSendEnd = pParse;

		bool bWasNormal = false;
		while (*pParse)
		{
			ch8 const *pLineStart = pParse;
			fg_ParseToEndOfLine(pParse);
			if (fg_ParseEndOfLine(pParse))
				pToSendEnd = pParse;
			else
				break;

			if (_bFilterP4)
			{
				if (fg_StrStartsWith(pLineStart, "info") || fg_StrStartsWith(pLineStart, "exit"))
				{
					if (bWasNormal)
						ToSend += Buffer.f_Extract(pToSendStart - pParseStart, pLineStart - pToSendStart);
					bWasNormal = false;
					pToSendStart = pParse;
				}
				else
					bWasNormal = true;
			}
			else
				bWasNormal = true;
		}
		if (bWasNormal && pToSendStart < pToSendEnd)
			ToSend += Buffer.f_Extract(pToSendStart - pParseStart, pToSendEnd - pToSendStart);
		Buffer = Buffer.f_Extract(pToSendEnd - pParseStart);
	}

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

class CTool_LaunchParallell : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		DScopeConOutTimer("LaunchParallell");
		struct CProgram
		{
			CProgram()
				: m_bFilterP4(false)
				, m_bSandboxed(false)
			{
			}

			TCVector<CStr> m_Params;
			bool m_bFilterP4;
			bool m_bSandboxed;
			CStr m_LockFile;

			CStr f_GetParams() const
			{
				return CProcessLaunchParams::fs_GetParams(m_Params);
			}

		};

		CStr LockFilePath;

		TCVector<CProgram> Programs;
		TCVector<CStr> Params2;
		int32 iParam = 0;
		int32 iNestedParam = 0;
		uint32 iExtraErrorSuccess = 0;
		bool bNextFilterP4 = false;
		bool bNextSandboxed = false;
		bool bDelayOutput = false;
		bool bSeparateStdErr = false;
		CProgram *pCurrentProgram = nullptr;
		CStr Param;
		while ((Param = _Params.f_GetValue(CStr::fs_ToStr(iParam), "012301231023Error")) != "012301231023Error")
		{
			Params2.f_Insert(Param);
			if (Param == "(")
			{
				if (++iNestedParam == 1)
				{
					pCurrentProgram = &Programs.f_Insert();
					pCurrentProgram->m_bFilterP4 = bNextFilterP4;
					pCurrentProgram->m_bSandboxed = bNextSandboxed;
					bNextFilterP4 = false;
					bNextSandboxed = false;
				}
				else
					pCurrentProgram->m_Params.f_Insert(Param);
			}
			else if (Param == ")")
			{
				if (--iNestedParam == 0)
					pCurrentProgram = nullptr;
				else if (pCurrentProgram)
					pCurrentProgram->m_Params.f_Insert(Param);
			}
			else if (pCurrentProgram)
			{
				pCurrentProgram->m_Params.f_Insert(Param);
			}
			else if (Param == "p4")
			{
				bNextFilterP4 = true;
			}
			else if (Param == "sandboxed")
			{
				bNextSandboxed = true;
			}
			else if (Param == "delay")
			{
				bDelayOutput = false;
			}
			else if (Param == "sepstderr")
			{
				bSeparateStdErr = true;
			}
			else if (Param.f_StartsWith("lockfile="))
			{
				LockFilePath = Param.f_Extract(9);
			}
			else if (Param.f_StartsWith("extrasucess="))
			{
				iExtraErrorSuccess = Param.f_Extract(12).f_ToInt(uint32(0));
			}
			else
			{
				DError("Invalid () secquence");
			}
			++iParam;
		}

		if (Programs.f_IsEmpty())
			DError(CStr::CFormat("No programs to run with params: {}") << CProcessLaunchParams::fs_GetParams(Params2));

		uint32 ReturnValue = 0;
		CMutual ConOutLock;
		{
			NFile::CLockFile LockFile(LockFilePath);

			if (!LockFilePath.f_IsEmpty())
			{
				NFile::CLockFile::ELockResult Result;

				Result = LockFile.f_Lock(-1);

				if (Result != NFile::CLockFile::ELockResult_Locked)
				{
					DError(CStr::CFormat("Failed to lock lockfile. Check that permission are correct: {}") << LockFilePath);
				}
			}

			CProcessLaunchHandler LaunchHandler;
			for (auto Iter = Programs.f_GetIterator(); Iter; ++Iter)
			{
				CProgram const &Program = *Iter;
				mint nParams = Program.m_Params.f_GetLen();
				if (nParams > 0)
				{
					CStr FileName = Program.m_Params[0];

					TCSharedPointer<CStr> pStdOutBuffer = fg_Construct();
					TCSharedPointer<CStr> pStdErrBuffer = fg_Construct();

					TCVector<CStr> Params = Program.m_Params;
					Params.f_Remove(0);

					CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutable
						(
							FileName
							, Params
							, CFile::fs_GetCurrentDirectory()
							, [&, Program, FileName, pStdOutBuffer, pStdErrBuffer](CProcessLaunchStateChangeVariant const &_StateChange, fp64 _TimeSinceLaunch)
							{
								if (_StateChange.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
								{
									CStr Params = Program.f_GetParams();
									DConOut("error: {} failed to launch: {}", Params, _StateChange.f_Get<EProcessLaunchState_LaunchFailed>());
									ReturnValue = fg_Max(ReturnValue, 255u);
								}
								else if (_StateChange.f_GetTypeID() == EProcessLaunchState_Exited)
								{
									//DConOut("Program took {} seconds to run." DNewLine, _TimeSinceLaunch);
									uint32 ThisValue = _StateChange.f_Get<EProcessLaunchState_Exited>();
									if (ThisValue != 0 && ThisValue != iExtraErrorSuccess)
									{
										CStr Params = Program.f_GetParams();
										DLock(ConOutLock);
										DConOut("error: {} exited with {}" DNewLine, Params, ThisValue);
										ReturnValue = fg_Max(ReturnValue, ThisValue);
									}
									fg_OutputWholeLines(*pStdOutBuffer, EProcessLaunchOutputType_StdOut, Program.m_bFilterP4, true);
									fg_OutputWholeLines(*pStdErrBuffer, EProcessLaunchOutputType_StdErr, false, true);
								}
							}
						)
					;

					LaunchParams.m_fOnOutput
						= [&, Program, pStdOutBuffer, pStdErrBuffer](EProcessLaunchOutputType _OutputType, CStr const &_Output)
						{
							switch (_OutputType)
							{
							case EProcessLaunchOutputType_GeneralError:
							case EProcessLaunchOutputType_StdErr:
							case EProcessLaunchOutputType_TerminateMessage:
								{
									DLock(ConOutLock);
									*pStdErrBuffer += _Output;
									fg_OutputWholeLines(*pStdErrBuffer, EProcessLaunchOutputType_StdErr, false, false);
								}
								break;
							case EProcessLaunchOutputType_StdOut:
								{
									DLock(ConOutLock);
									*pStdOutBuffer += _Output;
									fg_OutputWholeLines(*pStdOutBuffer, EProcessLaunchOutputType_StdOut, Program.m_bFilterP4, false);
								}
								break;
							case EProcessLaunchOutputType_Max:
								DMibNeverGetHere;
								break;
							}
						}
					;

					LaunchParams.m_bSeparateStdErr = bSeparateStdErr;
					LaunchParams.m_bSandboxed = Program.m_bSandboxed;
					LaunchParams.m_bShowLaunched = false;
					LaunchParams.m_bAllowExecutableLocate = true;

					LaunchHandler.f_AddLaunch(LaunchParams, bDelayOutput);
				}
				else
				{
					DError("Empty program statements not allowed");
				}
			}
			LaunchHandler.f_BlockOnExit();

			if (LockFile.f_HasLock())
				LockFile.f_Unlock();
		}
		return ReturnValue;
	}
};

DMibRuntimeClass(CTool, CTool_LaunchParallell);


class CTool_TestStdOut : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		fp64 Sleep = fg_GetRandomFloat();
		DConOut("Testing standard out {}" DNewLine, Sleep);
//		NSys::fg_ConsoleOutputFlush();
		NSys::fg_Thread_Sleep(Sleep);
		Sleep = fg_GetRandomFloat();
		DConErrOut("Testing standard err {}" DNewLine, Sleep);
//		NSys::fg_ConsoleErrorOutputFlush();
		NSys::fg_Thread_Sleep(Sleep);
		Sleep = fg_GetRandomFloat();
		DConErrOut("Sleep {}" DNewLine, Sleep);
//		NSys::fg_ConsoleErrorOutputFlush();
		NSys::fg_Thread_Sleep(Sleep);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_TestStdOut);


class CTool_LaunchSandboxed : public CTool2
{
public:

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{

		CStr FileName, Params;

		{
			auto FIter = _Files.f_GetIterator();
			if (!FIter)
				DError("Wrong number of parameters");
			FileName = *FIter;
			++FIter;

			for (;FIter;++FIter)
			{
				Params += CStr(" ") + *FIter;
			}
		}

		int Ret = 0;

		CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutableRawParams
			(
				FileName
				, Params
				, CFile::fs_GetCurrentDirectory()
				, [&](CProcessLaunchStateChangeVariant const &_StateChange, fp64 _TimeSinceLaunch)
				{
					if (_StateChange.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
					{
						DConOut("{}", _StateChange.f_Get<EProcessLaunchState_LaunchFailed>());
						Ret = 255;
					}
					else if (_StateChange.f_GetTypeID() == EProcessLaunchState_Exited)
					{
						DConOut("Program took {} seconds to run." DNewLine, _TimeSinceLaunch);
						Ret = _StateChange.f_Get<EProcessLaunchState_Exited>();
					}
				}
			)
		;

		LaunchParams.m_bSandboxed = true;
		LaunchParams.m_bCopyRootToSandbox = !!_Params.f_FindEqual("CopyRoot");

		{
			int iRoot = 0;

			for (
					auto RIter = _Params.f_FindEqual(CStr(CStr::CFormat("Root{}") << iRoot))
				;	RIter
				;	RIter = _Params.f_FindEqual(CStr(CStr::CFormat("Root{}") << iRoot))
			)
			{
				CStr CurRootStr = *RIter;

				CStr Root = fg_GetStrSep(CurRootStr, "=");
				LaunchParams.m_SandboxRoots[Root] = fg_GetStrSep(CurRootStr, "=");

				DConOut("Mapping: {} : {}" DNewLine, Root, LaunchParams.m_SandboxRoots[Root]);

				++iRoot;
			}
		}


		LaunchParams.m_fOnOutput
			= [&](EProcessLaunchOutputType _OutputType, CStr const &_Output)
			{
				switch (_OutputType)
				{
				case EProcessLaunchOutputType_GeneralError:
				case EProcessLaunchOutputType_StdErr:
				case EProcessLaunchOutputType_TerminateMessage:
					DConErrOutRaw(_Output);
					break;
				case EProcessLaunchOutputType_StdOut:
					DConOutRaw(_Output);
					break;
				case EProcessLaunchOutputType_Max:
					DMibNeverGetHere;
					break;
				}
			}
		;


		{
			CProcessLaunch Launch(LaunchParams, EProcessLaunchCloseFlag_BlockOnExit);
		}

		return Ret;

	}

};

DMibRuntimeClass(CTool, CTool_LaunchSandboxed);
