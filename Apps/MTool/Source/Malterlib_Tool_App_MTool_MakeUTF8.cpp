// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_MakeUTF8 : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr SourcePath = CFile::fs_GetExpandedPath(_Params.f_GetValue("0", "NotExist"), true);

		auto Files = CFile::fs_FindFiles(SourcePath, EFileAttrib_File, true);

		if (Files.f_IsEmpty())
			DError("No source files found to convert");

		for (auto iFile = Files.f_GetIterator(); iFile; ++iFile)
		{
			CStr Contents = CFile::fs_ReadStringFromFile(*iFile);
			CByteVector FileData;
			CFile::fs_WriteStringToVector(FileData, Contents, true);
			if (CFile::fs_CopyFileDiff(FileData, *iFile, CTime::fs_NowUTC()))
				DConOut("Rewrote: {}" DNewLine, *iFile);
		}
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_MakeUTF8);

