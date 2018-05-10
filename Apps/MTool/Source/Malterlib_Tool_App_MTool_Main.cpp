// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Core/Application>
#ifdef DEnableKBHit
#ifdef DPlatformFamily_Windows

#include "conio.h"

#endif
#endif

CStr CTool2::f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option, CStr const &_Default) const
{
	auto pOption = _Params.f_FindEqual(_Option);
	if (pOption)
		return *pOption;
	return _Default;
}

CStr CTool2::f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option) const
{
	auto pOption = _Params.f_FindEqual(_Option);
	if (pOption)
		return *pOption;
	
	DError(CStr::CFormat("You have to specify '{}'") << _Option);
}

aint CTool2::f_Run(NRegistry::CRegistry_CStr &_Params)
{
	TCMap<CStr, CStr> Params;

	TCVector<CStr> Files;

	for (mint i = 0; true; ++i)
	{
		CStr Value = _Params.f_GetValue(CStr::fs_ToStr(i), "");
		if (Value.f_IsEmpty())
			break;
		if (Value.f_FindChar('=') >= 0)
		{
			CStr Key = fg_GetStrSep(Value, "=");
			Params[Key] = Value;
		}
		else
			Files.f_Insert(Value);
	}

	return f_Run(Files, Params);
}

class CToolApp : public NMib::CApplication
{
public:
	aint f_Main()
	{
#ifdef DPlatformFamily_OSX
		
		int Signals[] = 
			{
				SIGHUP,
				SIGINT,
				SIGQUIT,
				SIGILL,
				SIGTRAP,
				SIGABRT,
				SIGEMT,
				SIGFPE,
				SIGKILL,
				SIGSYS,
				SIGPIPE,
				SIGALRM,
				SIGTERM,
				SIGURG,
				SIGSTOP,
				SIGTSTP,
				SIGCONT
			}
		;
		
		auto fSignalHandler = [](int _Signal)
			{
				DConOut("Ignored signal: {}\n", _Signal);
			}
		;
		
		TCMap<int, void (*)(int)> OldSignals;
		
		for (auto &Signal : Signals)
			OldSignals[Signal] = signal(Signal, fSignalHandler);
		
		auto Cleanup 
			= g_OnScopeExit > [&]
			{
				for (auto iOldSignal = OldSignals.f_GetIterator(); iOldSignal; ++iOldSignal)
					signal(iOldSignal.f_GetKey(), *iOldSignal);
			}
		;
#endif		
		
		auto Checkout = fg_GetSys()->f_MemoryManager_Checkout();

		try
		{
			// Read executable path to enable tracking of file accesses to detect new tool compiled
			CFile Test;
			Test.f_Open(CFile::fs_GetProgramPath(), EFileOpen_ReadAttribs | EFileOpen_ShareAll);
			Test.f_GetAttributes();
		}
		catch (NException::CException const &_Exception)
		{
			DConErrOut("Exception reading exe attributes: {}" DNewLine, _Exception.f_GetErrorStr());
		}

		TCVector<CStr> Args;
		NSys::fg_Process_GetCommandLineArgs(Args);
		Args.f_Remove(0);
		int nArgs = Args.f_GetLen();
		//DConErrOut("{}\n", f_CommandLineParameters());
		if (nArgs >= 1)
		{
			NStr::CStr Class;
		l_Retry:
			int iStart = 0;
			for (int i = iStart; i < nArgs; ++i)
			{
				NStr::CStr Param = Args[i];
				
				if (Param == "--ToolClass")
				{
					Class = Args[i + 1];
					break;
				}
			}
			
			if (Class.f_IsEmpty())
			{
				Class = Args[0];
				if (Class[0] == '-')
					Class = Class.f_Extract(1);
				++iStart;
			}
			
			if (Class == "-x86")
			{
				++iStart;
				Class = Args[1];
			}			
			
			
			NRegistry::CRegistry_CStr Params;
			mint iOut = 0;
			for (int i = iStart; i < nArgs; ++i)
			{
				NStr::CStr Param = Args[i];
				
				if (Param == "--ToolClass")
				{
					++i;
					continue;
				}
				Params.f_SetValue(CStr::fs_ToStr(iOut), Param);
				++iOut;
			}

			CTool *pTool = fg_CreateRuntimeType<CTool>(NMib::NStr::CStr("CTool_") + Class);

			if (!pTool)
			{
				if (Class != "CMake")
				{
					Class = "CMake";
					Params.f_Clear();
					goto l_Retry;
				}
				else
				{
					DConErrOut("Could not create tool class: CTool_{}" DNewLine, Class);
	#ifdef DEnableKBHit
					while (!_kbhit())
						NMib::NSys::fg_Thread_SmallestSleep();
	#endif
					return 1;
				}
			}

			aint Ret;
			try
			{
				Ret = pTool->f_Run(Params);
			}
			catch (NException::CException &_Exception)
			{
				CStr ErrorStr = _Exception.f_GetErrorStr();
				if (ErrorStr.f_Find("error:") >= 0)
				{
					DConErrOut("{}" DNewLine, _Exception.f_GetErrorStr());
					DConErrOut("note: Tool command line: {}" DNewLine, f_CommandLineParameters());
				}
				else
				{
					DConErrOut("{}" DNewLine, _Exception.f_GetErrorStr());
					DConErrOut2("error: Running tool '{}': {}{\n}", Class, f_CommandLineParameters());
				}
				delete pTool;
#ifdef DEnableKBHit
				while (!_kbhit())
					NMib::NSys::fg_Thread_SmallestSleep();
#endif
				return 1;
			}

			delete pTool;

#ifdef DEnableKBHit
				while (!_kbhit())
					NMib::NSys::fg_Thread_SmallestSleep();
#endif
			return Ret;

		}

		return 0;
	}
};

DMibAppImplement(CToolApp);
