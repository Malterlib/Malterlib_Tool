// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#ifdef DPlatformFamily_Windows

#include <Mib/Core/PlatformSpecific/WindowsRegistry>

class CTool_ReadWindowsRegistry : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Root = _Params.f_GetValue("0", "012301231023Error");

		using ERegRoot = NMib::NPlatform::CWin32_Registry::ERegRoot;
		ERegRoot RegRoot;
		if (Root == "LocalMachine")
			RegRoot = ERegRoot::ERegRoot_LocalMachine;
		else if (Root == "CurrentUser")
			RegRoot = ERegRoot::ERegRoot_CurrentUser;
		else if (Root == "Classes")
			RegRoot = ERegRoot::ERegRoot_Classes;
		else if (Root == "Win64_LocalMachine")
			RegRoot = ERegRoot::ERegRoot_Win64_LocalMachine;
		else if (Root == "Win64_CurrentUser")
			RegRoot = ERegRoot::ERegRoot_Win64_CurrentUser;
		else if (Root == "Win64_Classes")
			RegRoot = ERegRoot::ERegRoot_Win64_Classes;
		else
			DError(fg_Format("Unknown root: {}", Root));

		NMib::NPlatform::CWin32_Registry Registry{RegRoot};
		CStr Value = Registry.f_Read_Str(_Params.f_GetValue("1", ""), _Params.f_GetValue("2", ""));

		DConOut("{}\n", Value);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_ReadWindowsRegistry);

#endif

