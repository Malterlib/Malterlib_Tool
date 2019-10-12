// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"
#include <Mib/Cryptography/UUID>
#include <Mib/Process/Platform>
#include <Mib/Process/ProcessLaunch>

struct CPerforceHelper
{
	CTool2 &m_Tool;
	CPerforceHelper(CTool2 &_Tool)
		: m_Tool(_Tool)
	{
	}

	TCUniquePointer<CPerforceClientThrow> f_GetClient(TCMap<CStr, CStr> const &_Params, CStr &_ClientName)
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
};


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


class CTool_CreatePerforceClient : public CTool2
{
public:
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CPerforceHelper PerforceHelper(*this);

		CStr ClientName;
		TCUniquePointer<CPerforceClientThrow> pClient = PerforceHelper.f_GetClient(_Params, ClientName);
		DConOut("{}", ClientName);
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_CreatePerforceClient);


class CTool_SyncPerforce : public CTool2
{
public:
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CPerforceHelper PerforceHelper(*this);

		CStr ToSync = f_GetOption(_Params, "ToSync");
		CStr ClientName;
		CClock Timer;
		Timer.f_Start();
		fp64 NextUpdate = Timer.f_GetTime() + 0.5;
		TCUniquePointer<CPerforceClientThrow> pClient = PerforceHelper.f_GetClient(_Params, ClientName);
		pClient->f_Sync
			(
				ToSync
				, [&](int64 _TotalBytes, int64 _SyncedBytes)
				{
					fp64 Now = Timer.f_GetTime();
					
					if (Now > NextUpdate)
					{
						if (_TotalBytes > 0)
							DConOut("{sj12} bytes synced ({fe1} %){\n}", _SyncedBytes << (fp64(_SyncedBytes) / fp64(_TotalBytes)) * 100.0);
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
};

DMibRuntimeClass(CTool, CTool_SyncPerforce);

class CTool_PerforceRunScript : public CTool2
{
public:
	
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr DoneMessage = "Done!";
		auto ReportDone
			= g_OnScopeExit > [&]
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

			auto fl_GetTool
				= [&](CStr const &_Tool) -> CStr
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

				CStr Tool = fl_GetTool(ToolName);
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

//		DConOut("Executing: {} {}\n", ClientPath << Params);

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
//			DConOut("Setting path to: {}\n", Path);
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
						{
							Launch.f_SendStdIn(_Input);
						}
						else
						{
							DConErrOutRaw(_Input);
						}
					}
				)
			;
			CStdInReader StdInReader(fg_Move(StdInParams));

			FinishedEvent.f_Wait();
		}
		
		return Ret;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceRunScript);

