// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#include "Malterlib_Tool_App_MTool_WindowsSymbols.h"

namespace NMib::NTool
{
	CWindowsExecutableInfo fg_GetWindowsExecutableInfo(CStr const &_Path)
	{
		CWindowsExecutableInfo ReturnInfo;
		CByteVector Mem;
		NFile::CFile File;
		File.f_Open(_Path, EFileOpen_Read | EFileOpen_ShareAll);
		CFilePos FLen;
		{
			FLen = fg_Min(File.f_GetLength(), 128*1024);
			Mem.f_SetLen(FLen);
			File.f_Read(Mem.f_GetArray(), FLen);
		}

		auto fReadData = [&](CFilePos _Position, void *_pDest, mint _Length)
			{
				File.f_SetPosition(_Position);
				File.f_Read(_pDest, fg_Min(CFilePos(_Length), File.f_GetLength() - _Position));
			}
		;

		// Look for header information
		CNTImageHeaders *pGeneralHeaders = nullptr;
		uint32 *pData = (uint32 *)Mem.f_GetArray();
		mint HeadersStart = 0;
		uint32 Find = 'P' + ('E' << 8) + ('\0' << 16) + ('\0' << 24);
		for (int i = 0; i < FLen; i += 4, pData += 1)
		{
			if (*pData == Find)
			{
				pGeneralHeaders = (CNTImageHeaders *)pData;
				HeadersStart = i;
				break;
			}
		}

		if (!pGeneralHeaders)
			DError(fg_Format("Could not find PE header: {}", _Path));


		auto fExtractGuid = [&](auto &_Header)
			{
				ReturnInfo.m_TimestampRaw = _Header.m_FileHeader.m_TimeDateStamp;
				ReturnInfo.m_SizeOfImage = _Header.m_Optional.m_SizeOfImage;

				TCVector<CImageSectionHeader> SectionHeaders;

				SectionHeaders.f_SetLen(_Header.m_FileHeader.m_NumberOfSections);

				CFilePos SectionsStart = CFilePos(HeadersStart) + sizeof(CNTImageHeaders) + _Header.m_FileHeader.m_SizeOfOptionalHeader;
				fReadData(SectionsStart, SectionHeaders.f_GetArray(), SectionHeaders.f_GetLen() * sizeof(CImageSectionHeader));

				mint DebugRva = _Header.m_Optional.m_DataDirectory[6].m_VirtualAddress;

				CImageSectionHeader *pDebugSection = nullptr;

				for (auto &Section : SectionHeaders)
				{
					auto SectionSize = Section.m_Misc.m_VirtualSize;

					if( SectionSize == 0 ) // compensate for Watcom linker strangeness, according to Matt Pietrek
						SectionSize = Section.m_SizeOfRawData;

					if( ( DebugRva >= Section.m_VirtualAddress ) &&
						( DebugRva < Section.m_VirtualAddress + SectionSize ) )
					{
						// Yes, the RVA belongs to this section
						pDebugSection = &Section;
						break;
					}
				}

				if (!pDebugSection)
					return;

				// Look up the file offset using the section header

				mint Diff = (mint)( pDebugSection->m_VirtualAddress - pDebugSection->m_PointerToRawData);

				mint FileOffset = DebugRva - Diff;

				CImageDebugDirectory DebugInfo;
				fReadData(FileOffset, &DebugInfo, sizeof(DebugInfo));

				CV_INFO_PDB70 PDB;
				fReadData(DebugInfo.m_PointerToRawData, &PDB, sizeof(PDB));

				ReturnInfo.m_PDBFile = PDB.m_PdbFileName;
				ReturnInfo.m_PDBGuid = "{nfh,sj8,sf0}{nfh,sj4,sf0}{nfh,sj4,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh,sj2,sf0}{nfh}"_f
					<< PDB.m_Signature.m_Data1
					<< PDB.m_Signature.m_Data2
					<< PDB.m_Signature.m_Data3
					<< PDB.m_Signature.m_Data4[0]
					<< PDB.m_Signature.m_Data4[1]
					<< PDB.m_Signature.m_Data4[2]
					<< PDB.m_Signature.m_Data4[3]
					<< PDB.m_Signature.m_Data4[4]
					<< PDB.m_Signature.m_Data4[5]
					<< PDB.m_Signature.m_Data4[6]
					<< PDB.m_Signature.m_Data4[7]
					<< PDB.m_Age
				;
			}
		;

		if (pGeneralHeaders->m_FileHeader.f_Is64Bit())
			fExtractGuid(*(CNTImageHeaders64 *)pGeneralHeaders);
		else if (pGeneralHeaders->m_FileHeader.f_Is32Bit())
			fExtractGuid(*(CNTImageHeaders32 *)pGeneralHeaders);
		else
			DError("Unrecognized Machine");

		static CTime s_Epoch = CTimeConvert::fs_CreateTime(1970, 1, 1);
		ReturnInfo.m_Timestamp = s_Epoch + CTimeSpanConvert::fs_CreateSecondSpan(pGeneralHeaders->m_FileHeader.m_TimeDateStamp);

		return ReturnInfo;
	}
}
