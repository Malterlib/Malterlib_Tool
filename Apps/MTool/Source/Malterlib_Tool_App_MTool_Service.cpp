// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"
#ifdef DPlatformFamily_Windows
#include "../../AOWin/AOService.h"
#include "../../AOWin/AOWinMisc.h"

#define DServiceDisplayName "Malterlib Tool Service"
#define DServiceDesc "Runs tool commands as a service."
#define DServiceName "MTool"

class CTool_Service : public CTool
{
public:

	class CService : public CAOService, public NThread::CThread
	{
	public:
		CStr m_Command;
		NRegistry::CRegistry_CStr m_Params;

		virtual NStr::CStr f_GetThreadName()
		{
			return "CTool_Service";
		}

		TCUniquePointer<CTool> m_pTool;
		CService() : CAOService(DServiceName, DServiceDisplayName, DServiceDesc)
		{
			m_pTool = nullptr;
		}

		~CService()
		{
		}

		aint f_Main()
		{
			NStr::CStr Class = m_Command;
			
			TCUniquePointer<CTool> pTool = fg_Explicit((CTool *)fg_CreateRuntimeType(NMib::NStr::CStr("CTool_") + Class));

			if (!pTool)
			{
				DMibConOut("Could not create tool class: CTool_{}" DNewLine, Class);
				return 1;
			}

			m_pTool = fg_Move(pTool);
			aint Ret = m_pTool->f_Run(m_Params);

			m_pTool = nullptr;

			return 0;
		}

		bint f_ServiceCreate()
		{
			f_Start();
			return true;		
		}

		bint f_ServiceDestroy()
		{
			if (m_pTool)
				m_pTool->f_RequestStop();
			f_Stop();
			return true;		
		}

		bint f_ServicePause()
		{
			return true;		
		}

		bint f_ServiceResume()
		{
			return true;		
		}

		void f_SetServiceName(CStr _Name)
		{
			f_SetInfo(_Name, DServiceDisplayName + (CStr::CFormat(" [{}]") << _Name).f_GetStr(), DServiceDesc);
		}

		void f_ParseCommandParams(CStr _ServiceName, NRegistry::CRegistry_CStr &_Params, int32 _StartParam)
		{
			CStr Params;
			int32 nParams = _Params.f_GetChildIterator().f_GetLen();
			for (int32 i = _StartParam; i < nParams; ++i)
			{
				CStr TempParam = _Params.f_GetValue(CStr::fs_ToStr(i), "Invalid");
				if (Params != "")
					Params += " " + TempParam.f_EscapeStr();
				else
					Params += TempParam.f_EscapeStr();
			}

			DConOut("Using params: {}" DNewLine, Params);
			f_SetCommandParams("Service Service " + _ServiceName.f_EscapeStr() + " " + Params);
		}

	};

	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		CService Service;

		CStr Command = _Params.f_GetValue("0", "Unrecognized");

		if (Command == "Service")
		{
			Service.f_SetServiceName(_Params.f_GetValue("1", "Unknown"));
			
			Service.m_Command = _Params.f_GetValue("2", "Unrecognized");
			int32 nParams = _Params.f_GetChildIterator().f_GetLen() - 3;
			for (int32 i = 0; i < nParams; ++i)
			{
				Service.m_Params.f_SetValue(CStr::fs_ToStr(i), _Params.f_GetValue(CStr::fs_ToStr(i+3), ""));
			}

			NMib::fg_GetSys()->f_RegisterProgram(DServiceDisplayName, "support@malterlib.com", true);
			Service.f_RunService();
		}
		else if (Command == "AddServiceIfNotAdded")
		{
			Service.f_SetInfo(CStr(_Params.f_GetValue("1", "Unknown")), CStr(_Params.f_GetValue("2", "Unknown")), CStr(_Params.f_GetValue("3", "Unknown")));
			Service.f_ParseCommandParams(_Params.f_GetValue("1", "Unknown"), _Params, 4);
			if (!Service.f_AddServiceIfNotAdded())
			{
				DConOut("Failed to add service. Make sure that the service is not running and that no service manager is open." DNewLine, 0);
				return 1;
			}
			return 0;
		}
		else if (Command == "AddService")
		{
			Service.f_SetInfo(CStr(_Params.f_GetValue("1", "Unknown")), CStr(_Params.f_GetValue("2", "Unknown")), CStr(_Params.f_GetValue("3", "Unknown")));
			Service.f_ParseCommandParams(_Params.f_GetValue("1", "Unknown"), _Params, 4);

			if (!Service.f_AddService())
			{
				DConOut("Failed to add service. Make sure that the service is not running and that no service manager is open." DNewLine, 0);
				return 1;
			}
			return 0;
		}
		else if (Command == "RemoveService")
		{
			Service.f_SetServiceName(_Params.f_GetValue("1", "Unknown"));
			if (Service.f_ServiceExists())
			{
				if (!Service.f_RemoveService())
				{
					DConOut("Failed to delete service. Make sure that the service is not running and that no service manager is open." DNewLine, 0);
					return 1;
				}
			}
			return 0;
		}
		else if (Command == "StartService")
		{
			Service.f_SetServiceName(_Params.f_GetValue("1", "Unknown"));
			if (!Service.f_StartService())
			{
				DConOut("Failed to start service." DNewLine, 0);
				return 1;
			}
			return 0;
		}
		else if (Command == "StopService")
		{
			Service.f_SetServiceName(_Params.f_GetValue("1", "Unknown"));
			if (Service.f_ServiceExists())
			{
				if (!Service.f_StopService())
				{
					DConOut("Failed to stop service." DNewLine, 0);
					return 1;
				}
			}
			return 0;
		}
		else if (Command == "RunAsProgram")
		{
			Service.m_Command = _Params.f_GetValue("1", "Unrecognized");
			int32 nParams = _Params.f_GetChildIterator().f_GetLen() - 2;
			for (int32 i = 0; i < nParams; ++i)
			{
				Service.m_Params.f_SetValue(CStr::fs_ToStr(i), _Params.f_GetValue(CStr::fs_ToStr(i+2), ""));
			}

			Service.f_ServiceCreate();

			mint ModuleSize;
			void *hInstance = NSys::fg_Module_Get(ModuleSize);

			HICON Icon = LoadIcon((HINSTANCE)hInstance, MAKEINTRESOURCE(101));
			Service.f_InitEmulation(Icon);
			// Just spin in eternity
			while (1)
			{
				if (Service.f_UpdateEmulation())
					break;
				Sleep(50);
			}

			Service.f_ServiceDestroy();

			return 0;
		}

		else 
			DError("Unrecognized command" + Command);

		return 0;
	}

};

DMibRuntimeClass(CTool, CTool_Service);
#endif
