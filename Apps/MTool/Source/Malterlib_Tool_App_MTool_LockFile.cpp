// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_LockFile : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr FileName = _Params.f_GetValue("0", "012301231023Error");
		if (FileName == "012301231023Error")
		{
			DError("Wrong number of parameters");
		}

		NFile::CFile File;
		File.f_Open(FileName, EFileOpen_Read | EFileOpen_ShareAll);

		CFilePos FileLen = File.f_GetLength();
		File.f_LockRange(0, FileLen + 1000000, EFileLock_PreventRead | EFileLock_Block);

		while (1)
			NSys::fg_Thread_Sleep(0.100f);
		File.f_UnlockRange(0, FileLen + 1000000);


		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_LockFile);

class CTool_PreventDelete : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr FileName = _Params.f_GetValue("0", "012301231023Error");
		if (FileName == "012301231023Error")
		{
			DError("Wrong number of parameters");
		}

		NFile::CFile File;
		File.f_Open(FileName, EFileOpen_Read | EFileOpen_ShareRead | EFileOpen_ShareWrite);

		while (1)
			NSys::fg_Thread_Sleep(0.100f);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PreventDelete);

class CTool_FillDrive : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr CurrentDir = NFile::CFile::fs_GetCurrentDirectory();


		NFile::CFile File;
		File.f_Open(CurrentDir + "/Testfile", EFileOpen_Write | EFileOpen_DontTruncate);

		CFilePos FreeSpace = NFile::CFile::fs_GetFreeSpace(CurrentDir);
		CFilePos SpaceToBeLeft = _Params.f_GetValue("0", "0").f_ToInt(CFilePos(0));

		while (1)
		{
			try
			{
				File.f_SetLength(FreeSpace - SpaceToBeLeft);
				SpaceToBeLeft += 512;
				break;
			}
			catch (NException::CException)
			{
			}
		}


		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_FillDrive);
