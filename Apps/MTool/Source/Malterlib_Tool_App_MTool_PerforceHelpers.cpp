// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"
#include <Mib/Cryptography/UUID>
#include <Mib/Process/Platform>
#include <Mib/Process/ProcessLaunch>

struct CPerforceHelper
{
	CPerforceHelper(CTool2 &_Tool);

	TCUniquePointer<CPerforceClientThrow> f_GetClient(TCMap<CStr, CStr> const &_Params, CStr &_ClientName);

	CTool2 &m_Tool;
};

CPerforceHelper::CPerforceHelper(CTool2 &_Tool)
	: m_Tool(_Tool)
{
}

TCUniquePointer<CPerforceClientThrow> CPerforceHelper::f_GetClient(TCMap<CStr, CStr> const &_Params, CStr &_ClientName)
{
	CStr Root = m_Tool.f_GetOption(_Params, "Root");
	CStr Password = m_Tool.f_GetOption(_Params, "Password", CStr());
	CStr Template = m_Tool.f_GetOption(_Params, "Template");

	TCUniquePointer<CPerforceClientThrow> pClient;
	CPerforceClient::CConnectionInfo ConnectionInfo;
	ConnectionInfo.m_Server = m_Tool.f_GetOption(_Params, "Server");
	ConnectionInfo.m_User = m_Tool.f_GetOption(_Params, "User");
	ConnectionInfo.m_Host = m_Tool.f_GetOption(_Params, "Host", NProcess::NPlatform::fg_Process_GetComputerName());

	CUniversallyUniqueIdentifier UUID(EUniversallyUniqueIdentifierGenerate_StringHash, CUniversallyUniqueIdentifier("{fcf99ea6-0d44-45de-89a6-13f0b16bdab4}"), Root);

	ConnectionInfo.m_Client = fg_Format("{}_{}_{}", ConnectionInfo.m_User, ConnectionInfo.m_Host, UUID.f_GetAsString(EUniversallyUniqueIdentifierFormat_AlphaNum));
	_ClientName = ConnectionInfo.m_Client;

	CFile::fs_CreateDirectory(Root);

	pClient = fg_Construct(ConnectionInfo);
	pClient->f_Login(Password);

	//DConOut("Root: {}{\n}", Root);
	//DConOut("ClientName: {}{\n}", ConnectionInfo.m_Client);
	//DConOut("User: {}{\n}", ConnectionInfo.m_User);
	//DConOut("Host: {}{\n}", ConnectionInfo.m_Host);

	CStr ClientText;

	if (!pClient->f_NoThrow().f_GetClient(ConnectionInfo.m_Client, ClientText) || ClientText.f_IsEmpty())
		pClient->f_CreateClient(ConnectionInfo.m_Client, Root, CStr(), Template);
	else
		pClient->f_UpdateClient(ConnectionInfo.m_Client, Root, CStr(), Template);

	return pClient;
}

#include <iostream>
#include <string>

bool fg_Confirm(CStr const &_Text)
{
	NSys::fg_Thread_Sleep(0.2f); // Allow stdout to flush before error output so order is preserved

	DConErrOut(_Text, 0);
	std::string ReplyStd;
	std::cin >> ReplyStd;
	CStr Reply = ReplyStd.c_str();
	return Reply.f_CmpNoCase("Y") == 0 || Reply.f_CmpNoCase("yes") == 0;
}

CStr fg_AskUser(CBlockingStdInReader &_StdIn, CStr const &_Text)
{
	NSys::fg_ConsoleOutputFlush();
	NSys::fg_Thread_Sleep(0.2f); // Allow stdout to flush before error output so order is preserved
	DConErrOut(_Text, 0);
	NSys::fg_ConsoleErrorOutputFlush();
	NSys::fg_Thread_Sleep(0.2f); // Allow stdout to flush before error output so order is preserved

	ch8 ToTrim[] = {32, 8, 9, 10, 13, 3, 0};

	CStr Value = fg_GetSys()->f_GetEnvironmentVariable("MTool_AskUser");
	if (Value == "EMPTY")
		return {};
	if (!Value.f_IsEmpty())
		return Value;

	return _StdIn.f_ReadLine().f_Trim(ToTrim);
}

// Resolves the files of _File, of changelist _Changelist too when it isn't 0, without merging: a file changed on both sides is left
// unresolved, also when its changes don't conflict, as an automatic merge can merge them wrongly. Returns the report of the files
// left and how to merge them, or nothing when all are resolved.
CStr fg_PerforceResolveSafe(CPerforceClientThrow &_Client, CStr const &_File, uint32 _Changelist)
{
	_Client.f_ResolveSafe(_File, _Changelist);

	auto Unresolved = _Client.f_GetUnresolvedFiles(_File, _Changelist);
	if (Unresolved.f_IsEmpty())
		return {};

	// The connection's settings can differ from those of the shell the commands are run in. The host is always given, as a shell's
	// P4HOST or P4CONFIG can name another one, and a workspace can be restricted to one host.
	CStr Server = _Client.f_GetActiveServer();
	CStr User = _Client.f_GetActiveUser();
	CStr Host = _Client.f_GetActiveHost();
	CStr Workspace = _Client.f_GetActiveClient();

	// The commands are for Bash on every platform, as no quoting suits both cmd and PowerShell. A value Bash would read otherwise is
	// single-quoted, which keeps every other character literal, also $, ` and !.
	auto fValue = [](CStr const &_Value) -> CStr
		{
			return _Value.f_IsEmpty() || fg_StrEscapeBashQuotesNeeded(_Value) ? fg_StrEscapeBashSingleQuotes(_Value) : _Value;
		}
	;
	auto fCommand = [&](CStr const &_Command) -> CStr
		{
			CStr Command = "p4 -p {} -u {} -H {} -c {} {}"_f << fValue(Server) << fValue(User) << fValue(Host) << fValue(Workspace) << _Command;
			if (_Changelist != 0)
				Command += " -c {}"_f << _Changelist;
			if (!_File.f_IsEmpty())
				Command += " " + fValue(_File);

			return Command;
		}
	;

	CStr Report = "These files in workspace {} were changed on both sides, which the safe resolve leaves for you to merge:\n"_f << Workspace;
	for (auto const &File : Unresolved)
		Report += "\t{} ({} from {}#{})\n"_f << File.m_ClientFile << File.m_ResolveType << File.m_FromFile << File.m_EndFromRev;

	Report += "Merge each file, check the result and mark it resolved, for example with: {}\n"_f << fCommand("resolve");
	if (_Changelist != 0)
		Report += "Then submit with: {}\n"_f << fCommand("submit");
	else
		Report += "Then submit it.\n";

	return Report;
}

struct CTool_CreatePerforceClient : public CTool2
{
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override;
};

aint CTool_CreatePerforceClient::f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
{
	CPerforceHelper PerforceHelper(*this);

	CStr ClientName;
	TCUniquePointer<CPerforceClientThrow> pClient = PerforceHelper.f_GetClient(_Params, ClientName);
	DConOut("{}", ClientName);

	return 0;
}

DMibRuntimeClass(CTool, CTool_CreatePerforceClient);

struct CTool_SyncPerforce : public CTool2
{
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override;
};

aint CTool_SyncPerforce::f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
{
	CPerforceHelper PerforceHelper(*this);

	CStr ToSync = f_GetOption(_Params, "ToSync");
	CStr ClientName;
	CStopwatch Stopwatch;
	Stopwatch.f_Start();
	fp64 NextUpdate = Stopwatch.f_GetTime() + 0.5;
	TCUniquePointer<CPerforceClientThrow> pClient = PerforceHelper.f_GetClient(_Params, ClientName);
	pClient->f_Sync
		(
			ToSync
			, [&](int64 _TotalBytes, int64 _SyncedBytes)
			{
				fp64 Now = Stopwatch.f_GetTime();

				if (Now > NextUpdate)
				{
					if (_TotalBytes > 0)
						DConOut("{sj12} bytes synced ({fe1} %){\n}", _SyncedBytes, (fp64(_SyncedBytes) / fp64(_TotalBytes)) * 100.0);
					else
						DConOut("{sj12} bytes synced{\n}", _SyncedBytes);

					NextUpdate = Now + 5.0;
				}

				return true;
			}
		)
	;

	return 0;
}

DMibRuntimeClass(CTool, CTool_SyncPerforce);

struct CTool_PerforceRunScript : public CTool2
{
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override;
};

aint CTool_PerforceRunScript::f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
{
	CStr DoneMessage = "Done!";
	auto ReportDone = g_OnScopeExit / [&]
		{
			DConOut("{}{\n}", DoneMessage);
		}
	;
	CStr File = f_GetOption(_Params, "File").f_Trim();

	CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
	CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
	CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");

	fg_GetSys()->f_SetEnvironmentVariable("P4CHARSET", "");

	DConOut("File: {}{\n}", File);

	DConOut("P4PORT: {}{\n}", P4Port);
	DConOut("P4USER: {}{\n}", P4User);
	DConOut("P4CLIENT: {}{\n}", P4Client);

	TCUniquePointer<CPerforceClientThrow> pClient;
	CPerforceClient::CConnectionInfo ConnectionInfo;
	ConnectionInfo.m_Server = P4Port;
	ConnectionInfo.m_User = P4User;
	ConnectionInfo.m_Client = P4Client;

	pClient = fg_Construct(ConnectionInfo);
	pClient->f_Login(CStr());

	if (pClient->f_IsUTF8())
		fg_GetSys()->f_SetEnvironmentVariable("P4CHARSET", "utf8");
	else
		fg_GetSys()->f_SetEnvironmentVariable("P4CHARSET", "");

	DConOut("Logged in to perforce{\n}", 0);

	CStr ClientPath = pClient->f_GetClientPath(File);

	TCVector<CStr> Params;

	CStr Path;
	bool bIsCygwin = false;

#ifdef DPlatformFamily_Windows

	if (CFile::fs_GetExtension(ClientPath).f_CmpNoCase("bat") == 0)
	{
		Params.f_Insert("/C");
		Params.f_Insert(ClientPath);

		ClientPath = "cmd.exe";
	}
	else
	{
		CStr Contents = CFile::fs_ReadStringFromFile(CStr(ClientPath));

		CStr FirstLine = fg_GetStrLineSep(Contents);

		CStr ToolPath;

		TCVector<CStr> ToolPaths;

		ToolPaths.f_Insert("x:/Dev/cygwin/bin");
		ToolPaths.f_Insert("c:/cygwin/bin");

		auto fGetTool = [&](CStr const &_Tool) -> CStr
			{
				for (auto &Path : ToolPaths)
				{
					CStr WholePath = CFile::fs_AppendPath(Path, _Tool);
					if (CFile::fs_FileExists(WholePath, EFileAttrib_File))
					{
						bIsCygwin = true;
						ToolPath = Path;
						return WholePath;
					}
				}

				DError(fg_Format("Could not find tool {}", _Tool));
			}
		;

		if (FirstLine.f_StartsWith("#!"))
		{
			CStr ToolName;
			if (FirstLine.f_Find("/bash") >= 0)
				ToolName = "bash.exe";
			else if (FirstLine.f_Find("/sh") >= 0)
				ToolName = "sh.exe";

			if (ToolName.f_IsEmpty())
				DError(fg_Format("Could not find tool for {}", FirstLine));

			CStr Tool = fGetTool(ToolName);
			Params.f_Insert("--login");
			Params.f_Insert("-c");
			TCVector<CStr> RunParams;
			RunParams.f_Insert("cd");
			RunParams.f_Insert(CFile::fs_GetPath(ClientPath));
			RunParams.f_Insert("&&");
			RunParams.f_Insert(ClientPath);

			Params.f_Insert(CProcessLaunchParams::fs_GetParamsUnix(RunParams));

			ClientPath = Tool;
		}
	}

#endif

	int Ret = 0;

	CEvent FinishedEvent;

	// DConOut("Executing: {} {}\n", ClientPath, Params);

	CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutable
		(
			ClientPath
			, Params
			, CFile::fs_GetPath(ClientPath)
			, [&](CProcessLaunchStateChangeVariant const &_StateChange, fp64 _TimeSinceLaunch)
			{
				if (_StateChange.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
				{
					DConErrOut("{}", _StateChange.f_Get<EProcessLaunchState_LaunchFailed>());
					Ret = 255;
					FinishedEvent.f_SetSignaled();
				}
				else if (_StateChange.f_GetTypeID() == EProcessLaunchState_Exited)
				{
					Ret = _StateChange.f_Get<EProcessLaunchState_Exited>();
					FinishedEvent.f_SetSignaled();
				}
			}
		)
	;

	LaunchParams.m_bAllowExecutableLocate = true;
	LaunchParams.m_bSeparateStdErr = true;
	LaunchParams.m_bShowLaunched = false;
	if (bIsCygwin)
		LaunchParams.m_Environment["CYGWIN"] = "nodosfilewarning";

	if (!Path.f_IsEmpty())
	{
		LaunchParams.m_Environment["PATH"] = Path;
		LaunchParams.m_bMergeEnvironment = true;
		// DConOut("Setting path to: {}\n", Path);
	}

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
			case EProcessLaunchOutputType_Max: DMibNeverGetHere; break;
			}
		}
	;

	{
		CProcessLaunch Launch(LaunchParams, EProcessLaunchCloseFlag_BlockOnExit);

		auto StdInParams = CStdInReaderParams::fs_Create
			(
				[&](EStdInReaderOutputType _Type, NStr::CStr const &_Input)
				{
					if (_Type == EStdInReaderOutputType_StdIn)
						Launch.f_SendStdIn(_Input);
					else
						DConErrOutRaw(_Input);
				}
			)
		;
		CStdInReader StdInReader(fg_Move(StdInParams));

		FinishedEvent.f_Wait();
	}

	return Ret;
}

DMibRuntimeClass(CTool, CTool_PerforceRunScript);

