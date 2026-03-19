// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_RemoveBrokenObj : public CTool
{
public:

	void fr_RemoveBrokenObj(CStr _Path)
	{
//		DConOut("Removing broken ojb files for dir: {}" DNewLine, _Path);
		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(_Path + "/*.obj", EFileAttrib_File, true);

		for (umint i = 0; i < Files.f_GetLen(); ++i)
		{
			CFilePos Len = NFile::CFile::fs_GetFileSize(Files[i]);
			if (Len == 0)
			{
				DConOut("Delete: {}" DNewLine, Files[i]);
				NFile::CFile::fs_DeleteFile(Files[i]);
			}
			else if (Len >= 16)
			{
				try
				{
					CByteVector Test;
					CFile File;
					File.f_Open(Files[i], EFileOpen_Read|EFileOpen_ShareAll);
					umint ToRead = fg_Min(File.f_GetLength(), 16);
					Test.f_SetLen(ToRead);
					File.f_Read(Test.f_GetArray(), Test.f_GetLen());

					umint Len = Test.f_GetLen();
					bool bOnly0 = true;
					{
						for (umint i = 0; i < Len; ++i)
						{
							if (Test[i] != 0)
								bOnly0 = false;
						}
					}
					if (bOnly0)
					{
						DConOut("Delete: {}" DNewLine, Files[i]);
						NFile::CFile::fs_DeleteFile(Files[i]);
					}
				}
				catch (CExceptionFile const &)
				{
				}
			}
		}
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Dir = _Params.f_GetValue("0", "NotExist");
		CStr Dir2 = _Params.f_GetValue("1", "NotExist");
		if (NFile::CFile::fs_FileExists(Dir, EFileAttrib_Directory))
		{
			fr_RemoveBrokenObj(Dir);
			DConOut("Done removing broken obj files" DNewLine,0);
			return 0;
		}

		if (!NFile::CFile::fs_FileExists(Dir2, EFileAttrib_Directory))
		{
			DError((CStr::CFormat("Directory {} does not exist") << Dir).f_GetStr());
		}

		fr_RemoveBrokenObj(Dir2);
		DConOut("Done removing broken obj files" DNewLine,0);
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_RemoveBrokenObj);
