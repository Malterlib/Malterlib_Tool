// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

struct CTool_RemoveBrokenObj : public CTool
{
	void f_RemoveBrokenObj(CStr const &_Path);
	aint f_Run(NContainer::CRegistry &_Params);
};

void CTool_RemoveBrokenObj::f_RemoveBrokenObj(CStr const &_Path)
{
	// DConOut("Removing broken ojb files for dir: {}" DNewLine, _Path);
	TCVector<CStr> Files = NFile::CFile::fs_FindFiles(_Path + "/*.obj", EFileAttrib_File, true);

	for (umint iFile = 0; iFile < Files.f_GetLen(); ++iFile)
	{
		CFilePos Len = NFile::CFile::fs_GetFileSize(Files[iFile]);
		if (Len == 0)
		{
			DConOut("Delete: {}" DNewLine, Files[iFile]);
			NFile::CFile::fs_DeleteFile(Files[iFile]);
		}
		else if (Len >= 16)
		{
			try
			{
				CByteVector Test;
				CFile File;
				File.f_Open(Files[iFile], EFileOpen_Read | EFileOpen_ShareAll);
				umint ToRead = fg_Min(File.f_GetLength(), 16);
				Test.f_SetLen(ToRead);
				File.f_Read(Test.f_GetArray(), Test.f_GetLen());

				umint Len = Test.f_GetLen();
				bool bOnly0 = true;
				{
					for (umint iByte = 0; iByte < Len; ++iByte)
					{
						if (Test[iByte] != 0)
							bOnly0 = false;
					}
				}
				if (bOnly0)
				{
					DConOut("Delete: {}" DNewLine, Files[iFile]);
					NFile::CFile::fs_DeleteFile(Files[iFile]);
				}
			}
			catch (CExceptionFile const &)
			{
			}
		}
	}
}

aint CTool_RemoveBrokenObj::f_Run(NContainer::CRegistry &_Params)
{
	CStr Dir = _Params.f_GetValue("0", "NotExist");
	CStr Dir2 = _Params.f_GetValue("1", "NotExist");
	if (NFile::CFile::fs_FileExists(Dir, EFileAttrib_Directory))
	{
		f_RemoveBrokenObj(Dir);
		DConOut("Done removing broken obj files" DNewLine, 0);
		return 0;
	}

	if (!NFile::CFile::fs_FileExists(Dir2, EFileAttrib_Directory))
		DError((CStr::CFormat("Directory {} does not exist") << Dir).f_GetStr());

	f_RemoveBrokenObj(Dir2);
	DConOut("Done removing broken obj files" DNewLine, 0);
	return 0;
}

DMibRuntimeClass(CTool, CTool_RemoveBrokenObj);
