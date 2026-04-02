// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_Cat : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr FileName = _Params.f_GetValue("0", "NotExist.file");
		FileName = NFile::CFile::fs_GetExpandedPath(FileName);
		if (!NFile::CFile::fs_FileExists(FileName, EFileAttrib_File))
		{
			DError(CStr(CStr::CFormat("File does not exist ({})") << FileName));
		}


		CStr String = NFile::CFile::fs_ReadStringFromFile(CStr(FileName));

		DConOutRaw(String);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_Cat);
