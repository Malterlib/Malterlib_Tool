// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_DeleteEmptyDirs : public CTool
{
public:

	void fr_DeleteEmptyDirs(CStr _Path)
	{
		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(_Path + "/*", EFileAttrib_Directory, false);

		for (mint i = 0; i < Files.f_GetLen(); ++i)
		{
			fr_DeleteEmptyDirs(Files[i]);
		}

		Files = NFile::CFile::fs_FindFiles(_Path + "/*", EFileAttrib_Directory|EFileAttrib_File, false);
		if (Files.f_GetLen() == 0)
		{
			NFile::CFile::fs_DeleteDirectory(_Path);
			DConOut("Deleting empty dir: {}" DNewLine, _Path);
			return;
		}
	}

	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		CStr Dir = _Params.f_GetValue("0", "NotExist");
		if (!NFile::CFile::fs_FileExists(Dir, EFileAttrib_Directory))
		{
			DError("Directory does not exist");
		}

		fr_DeleteEmptyDirs(Dir);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DeleteEmptyDirs);
