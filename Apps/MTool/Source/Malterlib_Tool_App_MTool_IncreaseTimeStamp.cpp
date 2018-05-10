// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#ifdef DPlatformFamily_Windows

#include "windows.h"
#include "winnt.h"

struct CV_INFO_PDB70
{
	DWORD      CvSignature; 
	GUID       Signature;       // unique identifier 
	DWORD      Age;             // an always-incrementing value 
	ch8       PdbFileName[1];  // zero terminated string with the name of the PDB file 
};

class CTool_IncreaseTimeStamp : public CTool
{
public:

	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		CStr FileName = _Params.f_GetValue("0", "NotExist.file");
		if (!NFile::CFile::fs_FileExists(FileName, EFileAttrib_File))
		{
			DError("File does not exist");
		}

		TCVector<uint8> Mem;
		CFilePos FLen;
		{
			NFile::CFile File;
			DConOut("Changing Timestamp: {}" DNewLine, FileName);

			File.f_Open(FileName, EFileOpen_Read | EFileOpen_ShareAll);
			FLen = File.f_GetLength();
			Mem.f_SetLen(FLen);
			File.f_Read(Mem.f_GetArray(), FLen);
		}

		// Look for header information
		IMAGE_NT_HEADERS *pHeader = nullptr;
		uint32 *pData = (uint32 *)Mem.f_GetArray();
		uint32 Find = 'P' + ('E' << 8) + ('\0' << 16) + ('\0' << 24);
		for (int i = 0; i < FLen; i += 4, pData += 1)
		{				
			if (*pData == Find)
			{
				pHeader = (IMAGE_NT_HEADERS *)pData;
				break;
			}
		}

		IMAGE_NT_HEADERS *pHeader2 = pHeader;
		if (pHeader)
		{
			bool bFound = false; 

			if (pHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 || pHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_IA64)
			{
				IMAGE_NT_HEADERS64 *pHeader = (IMAGE_NT_HEADERS64 *)pHeader2;

				NTime::CTime RefTime = NTime::CTimeConvert::fs_CreateTime(1970);
				CAOTime OldTime = RefTime + CAOTime::fs_GetSecondSpan(pHeader->FileHeader.TimeDateStamp);
				DConOut("Old timestamp: {} {}" DNewLine, OldTime.f_GetAsString_DateNoLocal() << OldTime.f_GetAsString_TimeWithSeconds());
				++pHeader->FileHeader.TimeDateStamp;
				CAOTime NewTime = RefTime + CAOTime::fs_GetSecondSpan(pHeader->FileHeader.TimeDateStamp);
				DConOut("New timestamp: {} {}" DNewLine, NewTime.f_GetAsString_DateNoLocal() << NewTime.f_GetAsString_TimeWithSeconds());
				{
					NFile::CFile File;
					DConOut("Writing File: {}" DNewLine, FileName);

					File.f_Open(FileName, EFileOpen_Write | EFileOpen_ShareAll);
					File.f_Write(Mem.f_GetArray(), FLen);
				}
			}
			else if (pHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
			{
				IMAGE_NT_HEADERS32 *pHeader = (IMAGE_NT_HEADERS32 *)pHeader2;

				NTime::CTime RefTime = NTime::CTimeConvert::fs_CreateTime(1970);
				CAOTime OldTime = RefTime + CAOTime::fs_GetSecondSpan(pHeader->FileHeader.TimeDateStamp);
				DConOut("Old timestamp: {} {}" DNewLine, OldTime.f_GetAsString_DateNoLocal() << OldTime.f_GetAsString_TimeWithSeconds());
				++pHeader->FileHeader.TimeDateStamp;
				CAOTime NewTime = RefTime + CAOTime::fs_GetSecondSpan(pHeader->FileHeader.TimeDateStamp);
				DConOut("New timestamp: {} {}" DNewLine, NewTime.f_GetAsString_DateNoLocal() << NewTime.f_GetAsString_TimeWithSeconds());
				{
					NFile::CFile File;
					DConOut("Writing File: {}" DNewLine, FileName);

					File.f_Open(FileName, EFileOpen_Write | EFileOpen_ShareAll);
					File.f_Write(Mem.f_GetArray(), FLen);
				}

			}
			else
				DError("Unrecognized Machine");

		}
		else
			DError("Could not find PE headers");

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_IncreaseTimeStamp);

class CTool_SetImageOsVersion : public CTool
{
public:

	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		CStr FileName = _Params.f_GetValue("0", "NotExist.file");
		if (!NFile::CFile::fs_FileExists(FileName, EFileAttrib_File))
		{
			DError("File does not exist");
		}

		CStr VersionStr = _Params.f_GetValue("1", "NoVersion");

		int32 MajorVersion = -1;
		int32 MinorVersion = -1;

		(CStr::CParse("{}.{}") >> MajorVersion >> MinorVersion).f_Parse(VersionStr);
		if (MajorVersion < 0 || MinorVersion < 0)
			DError("No or incorrect version specified");

		TCVector<uint8> Mem;
		CFilePos FLen;
		{
			NFile::CFile File;
#ifdef DMibDebug
			DConOut("Changing operating system version: {}" DNewLine, FileName);
#endif

			File.f_Open(FileName, EFileOpen_Read | EFileOpen_ShareAll);
			FLen = File.f_GetLength();
			Mem.f_SetLen(FLen);
			File.f_Read(Mem.f_GetArray(), FLen);
		}

		// Look for header information
		IMAGE_NT_HEADERS *pHeader = nullptr;
		uint32 *pData = (uint32 *)Mem.f_GetArray();
		uint32 Find = 'P' + ('E' << 8) + ('\0' << 16) + ('\0' << 24);
		for (int i = 0; i < FLen; i += 4, pData += 1)
		{				
			if (*pData == Find)
			{
				pHeader = (IMAGE_NT_HEADERS *)pData;
				break;
			}
		}

		IMAGE_NT_HEADERS *pHeader2 = pHeader;
		if (pHeader)
		{
			bool bFound = false; 

			if (pHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 || pHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_IA64)
			{
				IMAGE_NT_HEADERS64 *pHeader = (IMAGE_NT_HEADERS64 *)pHeader2;

#ifdef DMibDebug
				DConOut
					(
						"Old version: {}.{} ({}.{})" DNewLine
						, pHeader->OptionalHeader.MajorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MinorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MajorSubsystemVersion
						<< pHeader->OptionalHeader.MinorSubsystemVersion
					)
				;
#endif
				pHeader->OptionalHeader.MajorOperatingSystemVersion = MajorVersion;
				pHeader->OptionalHeader.MinorOperatingSystemVersion = MinorVersion;
				pHeader->OptionalHeader.MajorSubsystemVersion = MajorVersion;
				pHeader->OptionalHeader.MinorSubsystemVersion = MinorVersion;
#ifdef DMibDebug
				DConOut
					(
						"New version: {}.{} ({}.{})" DNewLine
						, pHeader->OptionalHeader.MajorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MinorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MajorSubsystemVersion
						<< pHeader->OptionalHeader.MinorSubsystemVersion
					)
				;
#endif
				{
					NFile::CFile File;
					DConOut("Writing File: {}" DNewLine, FileName);

					File.f_Open(FileName, EFileOpen_Write | EFileOpen_ShareAll);
					File.f_Write(Mem.f_GetArray(), FLen);
				}
			}
			else if (pHeader->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
			{
				IMAGE_NT_HEADERS32 *pHeader = (IMAGE_NT_HEADERS32 *)pHeader2;

#ifdef DMibDebug
				DConOut
					(
						"Old version: {}.{} ({}.{})" DNewLine
						, pHeader->OptionalHeader.MajorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MinorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MajorSubsystemVersion
						<< pHeader->OptionalHeader.MinorSubsystemVersion
					)
				;
#endif
				pHeader->OptionalHeader.MajorOperatingSystemVersion = MajorVersion;
				pHeader->OptionalHeader.MinorOperatingSystemVersion = MinorVersion;
				pHeader->OptionalHeader.MajorSubsystemVersion = MajorVersion;
				pHeader->OptionalHeader.MinorSubsystemVersion = MinorVersion;
#ifdef DMibDebug
				DConOut
					(
						"New version: {}.{} ({}.{})" DNewLine
						, pHeader->OptionalHeader.MajorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MinorOperatingSystemVersion 
						<< pHeader->OptionalHeader.MajorSubsystemVersion
						<< pHeader->OptionalHeader.MinorSubsystemVersion
					)
				;
#endif

				{
					NFile::CFile File;
#ifdef DMibDebug
					DConOut("Writing File: {}" DNewLine, FileName);
#endif

					File.f_Open(FileName, EFileOpen_Write | EFileOpen_ShareAll);
					File.f_Write(Mem.f_GetArray(), FLen);
				}

			}
			else
				DError("Unrecognized Machine");

		}
		else
			DError("Could not find PE headers");

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_SetImageOsVersion);

#endif
