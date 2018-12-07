// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_SymbolBreakpad : public CTool
{
public:

	aint f_Run(NContainer::CRegistry_CStr &_Params)
	{
		CStr SymbolDir = _Params.f_GetValue("0", "NotExist:/");
		CStr FileName = _Params.f_GetValue("1", "NotExist.file");
		if (!NFile::CFile::fs_FileExists(FileName, EFileAttrib_File))
		{
			DError(CStr(CStr::CFormat("File does not exist ({})") << FileName));
		}
		if (!NFile::CFile::fs_FileExists(FileName + ".zip", EFileAttrib_File))
		{
			DError(CStr(CStr::CFormat("File does not exist ({})") << (FileName + ".zip")));
		}

		{
//			DConOut("Deploying: {}" DNewLine, FileName);

			TCBinaryStreamFile<> File;
			
			File.f_Open(FileName, EFileOpen_Read | EFileOpen_ShareAll);
			
			CStr Buffer;
			aint FindPos = 0;
			bool bFoundModule;

			CStr Platform;
			CStr Architecture;
			CStr GUID;
			CStr Name;
			
			auto fl_FindModule
				= [&] () -> bool
				{
					CStrPtr Temp;
					Temp.f_SetConstPtr(Buffer.f_GetStr() + FindPos, Buffer.f_GetLen() - FindPos);
					aint iLine = Temp.f_FindChar('\n');
					if (iLine >= 0)
					{
						CStrPtr Line;
						Line.f_SetConstPtr(Buffer.f_GetStr() + FindPos, iLine);
						if (Line.f_StartsWith("MODULE"))
						{
							aint nParsed;
							(CStrPtr::CParse("MODULE {} {} {} {}" DNewLine) >> Platform >> Architecture >> GUID >> Name).f_Parse(Line, nParsed);
							
							if (nParsed != 4)
								DError(CStr::CFormat("Not all arguments was found for MODULE line in breakpad symbol file: '{}'") << Line);
				
							bFoundModule = true;
							return true;
						}
						FindPos += iLine + 1;
					}
					return false;
				}
			;
			
			while (1)
			{
				ch8 Temp[1024];
				mint ThisTime = fg_Min(1024, File.f_GetLength() - File.f_GetPosition());
				File.f_ConsumeBytes(Temp, ThisTime);
				
				Buffer.f_AddStr(Temp, ThisTime);
				
				if (fl_FindModule())
					break;
			}
			
			if (!bFoundModule)
				DError("MODULE line was not found in breakpad symbol file");
			
			CStr OutPath = CStr::CFormat("{}/{}/{}/{}/{}") << SymbolDir << Platform << Name << Architecture << GUID;
			
			DConOut("{}" DNewLine, OutPath);
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_SymbolBreakpad);
