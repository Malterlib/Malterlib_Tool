// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

#ifdef DPlatformFamily_Windows

#include <Mib/Core/PlatformSpecific/WindowsRegistry>
#include <Mib/Core/PlatformSpecific/WindowsFilePath>
#include <Mib/Core/PlatformSpecific/WindowsString>
#include <Mib/Core/PlatformSpecific/WindowsError>
#include <Mib/Process/ProcessLaunch>

struct CTool_LaunchNSIS : public CTool
{
	aint f_Run(NContainer::CRegistry &_Params);
};

aint CTool_LaunchNSIS::f_Run(NContainer::CRegistry &_Params)
{
	CStr FileName = _Params.f_GetValue("0", "012301231023Error");
	if (FileName == "012301231023Error")
		DError("Wrong number of parameters");

	NMib::NPlatform::CWin32_Registry Registry;
	CStr MakeNSIS = Registry.f_Read_Str("SOFTWARE\\Classes\\NSIS.Script\\DefaultIcon", "");
	MakeNSIS = NFile::CFile::fs_GetPath(MakeNSIS.f_Left(MakeNSIS.f_FindReverse(","))) + "/makensis.exe";
	// MakeNSIS = MakeNSIS.f_Left(MakeNSIS.f_FindReverse(","));

	// DConOut("{}" DNewLine, MakeNSIS);

	TCVector<CStr> Params;
	int iParam = 1;
	CStr Param;
	while ((Param = _Params.f_GetValue(CStr::fs_ToStr(iParam), "012301231023Error")) != "012301231023Error")
	{
		Params.f_Insert(Param);
		++iParam;
	}

	Params.f_Insert(FileName);

	int Ret = 0;

	CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutable
		(
			MakeNSIS
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
					DConOut("NSIS took {} seconds to run." DNewLine, _TimeSinceLaunch);
					Ret = _StateChange.f_Get<EProcessLaunchState_Exited>();
				}
			}
		)
	;

	LaunchParams.m_fOnOutput = [&](EProcessLaunchOutputType _OutputType, CStr const &_Output)
		{
			switch (_OutputType)
			{
			case EProcessLaunchOutputType_GeneralError:
			case EProcessLaunchOutputType_StdErr:
			case EProcessLaunchOutputType_TerminateMessage:
				DConErrOutRaw(_Output);
				break;
			case EProcessLaunchOutputType_StdOut: DConOutRaw(_Output); break;
			case EProcessLaunchOutputType_Max: DNeverGetHere; break;
			}
		}
	;
	{
		CProcessLaunch Launch(LaunchParams, EProcessLaunchCloseFlag_BlockOnExit);
	}

	return Ret;
}

DMibRuntimeClass(CTool, CTool_LaunchNSIS);

struct CTool_LaunchWithNamedPipes : public CTool
{
	aint f_Run(NContainer::CRegistry &_Params);
};

aint CTool_LaunchWithNamedPipes::f_Run(NContainer::CRegistry &_Params)
{
	aint iParam = 4;
	TCVector<CStr> Params;
	CStr Param;
	while ((Param = _Params.f_GetValue(CStr::fs_ToStr(iParam), "012301231023Error")) != "012301231023Error")
	{
		Params.f_Insert(Param);
		++iParam;
	}

	CStr StdOut = "\\\\.\\pipe\\" + _Params.f_GetValue("0", "");
	CStr StdErr = "\\\\.\\pipe\\" + _Params.f_GetValue("1", "");
	CStr StdIn = "\\\\.\\pipe\\" + _Params.f_GetValue("2", "");

	WaitNamedPipeA(StdOut, NMPWAIT_WAIT_FOREVER);
	SECURITY_ATTRIBUTES Inheritable;
	Inheritable.nLength = sizeof(SECURITY_ATTRIBUTES);
	Inheritable.lpSecurityDescriptor = nullptr;
	Inheritable.bInheritHandle = TRUE;
	HANDLE pStdOutFile = CreateFileA(StdOut, GENERIC_WRITE, 0, &Inheritable, OPEN_EXISTING, 0, nullptr);
	if (pStdOutFile == INVALID_HANDLE_VALUE)
		return 1;

	HANDLE pStdErrFile = pStdOutFile;

	if (StdErr != StdOut)
	{
		WaitNamedPipeA(StdErr, NMPWAIT_WAIT_FOREVER);
		pStdErrFile = CreateFileA(StdErr, GENERIC_WRITE, 0, &Inheritable, OPEN_EXISTING, 0, nullptr);
		if (pStdErrFile == INVALID_HANDLE_VALUE)
		{
			CloseHandle(pStdOutFile);
			return 1;
		}
	}

	WaitNamedPipeA(StdIn, NMPWAIT_WAIT_FOREVER);
	HANDLE pStdInFile = CreateFileA(StdIn, GENERIC_READ, 0, &Inheritable, OPEN_EXISTING, 0, nullptr);
	if (pStdInFile == INVALID_HANDLE_VALUE)
		return 1;

	uint32 ProcessID = GetCurrentProcessId();
	DWORD Written = 0;
	if (!WriteFile(pStdOutFile, &ProcessID, sizeof(ProcessID), &Written, nullptr))
		DDTrace("{}" DNewLine, NMib::NPlatform::fg_Win32_GetLastErrorStr(0));

	uint32 Finished = 1;
	if (!ReadFile(pStdInFile, &Finished, sizeof(Finished), &Written, nullptr))
		DDTrace("{}" DNewLine, NMib::NPlatform::fg_Win32_GetLastErrorStr(0));

	CStr Program = _Params.f_GetValue("3", "");

	DWORD ExitCode = -2;

	{
		PROCESS_INFORMATION ProcessInfo;
		STARTUPINFOW StartupInfo;

		// Set up the start up info struct.
		fg_MemClear(StartupInfo);
		StartupInfo.cb = sizeof(STARTUPINFOW);
		StartupInfo.hStdOutput = pStdOutFile;
		StartupInfo.hStdInput = pStdInFile;
		StartupInfo.hStdError = pStdErrFile;
		StartupInfo.wShowWindow = SW_HIDE;
		StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

		// Note that dwFlags must include STARTF_USESHOWWINDOW if we
		// use the wShowWindow flags. This also assumes that the
		// CreateProcess() call will use CREATE_NEW_CONSOLE.

		// Launch the child process.

		CStr WorkingDir = NFile::CFile::fs_GetCurrentDirectory();

		CWStr ParamsW = NStr::NPlatform::fg_StrToWindows
			(
				NFile::NPlatform::fg_ConvertToWindowsPath(Program, true).f_EscapeStr().f_Replace("\\\\", "\\") + " " + CProcessLaunchParams::fs_GetParams(Params)
			)
		;
		{
			if
			(
				!::CreateProcessW
				(
					NFile::NPlatform::fg_ConvertToWindowsPath(Program, true)
					, ParamsW.f_GetStrUniqueWritable()
					, nullptr
					, nullptr
					, TRUE
					, CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT
					, nullptr
					, NFile::NPlatform::fg_ConvertToWindowsPath(WorkingDir, true)
					, &StartupInfo
					, &ProcessInfo
				)
			)
			{
				DDTrace("Error: {}" DNewLine, NMib::NPlatform::fg_Win32_GetLastErrorStr(GetLastError()));
				return -3;
			}
		}
		// Close any unuseful handles
		::CloseHandle(ProcessInfo.hThread);

		WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
		/*
		if (!FlushFileBuffers(pStdOutFile))
		{
			DDTrace("Error: {}" DNewLine, NMib::NPlatform::fg_Win32_GetLastErrorStr(GetLastError()));
		}
		if (!FlushFileBuffers(pStdErrFile))
		{
			DDTrace("Error: {}" DNewLine, NMib::NPlatform::fg_Win32_GetLastErrorStr(GetLastError()));
		}*/

		GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);

		CloseHandle(ProcessInfo.hProcess);
	}

	return ExitCode;
}

DMibRuntimeClass(CTool, CTool_LaunchWithNamedPipes);

#endif

