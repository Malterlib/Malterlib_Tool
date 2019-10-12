// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_DetectVM : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		bool bSilent = false;

		CStr Arg0 = _Params.f_GetValue("0", "");
		bSilent = (Arg0 == "-s") || (Arg0 == "--silent");

		if (!bSilent)
			DConOutRaw("Detecting VM...");

		NMib::CVirtualMachineInfo  const& VMInfo = fg_GetSys()->f_GetVirtualMachineInfo();

//		NSys::fg_HW_GetVirtualMachineInfo(VMInfo);

		if (!bSilent)
		{
			DConOutRaw("Done.\n");

			DConOut("Detected VM: {}\n", (VMInfo.m_bDetected ? true : false) );
			DConOut("VM: {}\n", (VMInfo.m_pName ? VMInfo.m_pName : "none")) ;
		}

		return VMInfo.m_bDetected ? 1 : 0;
	}
};

DMibRuntimeClass(CTool, CTool_DetectVM);
