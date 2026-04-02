// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>

namespace NMib::NTool
{
	enum EPEImageFileMachine : uint16
	{
		EPEImageFileMachine_I386 = 0x014c  // Intel 386.
		, EPEImageFileMachine_ARM = 0x01c0  // ARM Little-Endian
		, EPEImageFileMachine_THUMB = 0x01c2  // ARM Thumb/Thumb-2 Little-Endian
		, EPEImageFileMachine_ARMNT = 0x01c4  // ARM Thumb-2 Little-Endian
		, EPEImageFileMachine_POWERPC = 0x01F0  // IBM PowerPC Little-Endian
		, EPEImageFileMachine_IA64 = 0x0200  // Intel 64
		, EPEImageFileMachine_AMD64 = 0x8664  // AMD64 (K8)
		, EPEImageFileMachine_ARM64 = 0xAA64  // ARM64 Little-Endian
	};

	struct CNTImageFileHeader
	{
		EPEImageFileMachine m_Machine;
		uint16 m_NumberOfSections;
		uint32 m_TimeDateStamp;
		uint32 m_PointerToSymbolTable;
		uint32 m_NumberOfSymbols;
		uint16 m_SizeOfOptionalHeader;
		uint16 m_Characteristics;

		bool f_Is64Bit()
		{
			switch (m_Machine)
			{
			case EPEImageFileMachine_IA64:
			case EPEImageFileMachine_AMD64:
			case EPEImageFileMachine_ARM64:
				return true;
			default:
				return false;
			}
			return false;
		}

		bool f_Is32Bit()
		{
			switch (m_Machine)
			{
			case EPEImageFileMachine_I386:
			case EPEImageFileMachine_ARM:
			case EPEImageFileMachine_THUMB:
			case EPEImageFileMachine_ARMNT:
			case EPEImageFileMachine_POWERPC:
				return true;
			default:
				return false;
			}
			return false;
		}
	};

	struct CPEImageDataDirectory
	{
		uint32 m_VirtualAddress;
		uint32 m_Size;
	};

	enum
	{
		EPEImageDirectoryEntries = 16
	};

	struct CPEOptionalHeaders32
	{
		uint16 m_Magic;
		uint8 m_MajorLinkerVersion;
		uint8 m_MinorLinkerVersion;
		uint32 m_SizeOfCode;
		uint32 m_SizeOfInitializedData;
		uint32 m_SizeOfUninitializedData;
		uint32 m_AddressOfEntryPoint;
		uint32 m_BaseOfCode;
		uint32 m_BaseOfData;

		uint32 m_ImageBase;
		uint32 m_SectionAlignment;
		uint32 m_FileAlignment;
		uint16 m_MajorOperatingSystemVersion;
		uint16 m_MinorOperatingSystemVersion;
		uint16 m_MajorImageVersion;
		uint16 m_MinorImageVersion;
		uint16 m_MajorSubsystemVersion;
		uint16 m_MinorSubsystemVersion;
		uint32 m_Win32VersionValue;
		uint32 m_SizeOfImage;
		uint32 m_SizeOfHeaders;
		uint32 m_CheckSum;
		uint16 m_Subsystem;
		uint16 m_DllCharacteristics;
		uint32 m_SizeOfStackReserve;
		uint32 m_SizeOfStackCommit;
		uint32 m_SizeOfHeapReserve;
		uint32 m_SizeOfHeapCommit;
		uint32 m_LoaderFlags;
		uint32 m_NumberOfRvaAndSizes;
		CPEImageDataDirectory m_DataDirectory[EPEImageDirectoryEntries];
	};

	struct CPEOptionalHeaders64
	{
		uint16 m_Magic;
		uint8 m_MajorLinkerVersion;
		uint8 m_MinorLinkerVersion;
		uint32 m_SizeOfCode;
		uint32 m_SizeOfInitializedData;
		uint32 m_SizeOfUninitializedData;
		uint32 m_AddressOfEntryPoint;
		uint32 m_BaseOfCode;
		uint64 m_ImageBase;
		uint32 m_SectionAlignment;
		uint32 m_FileAlignment;
		uint16 m_MajorOperatingSystemVersion;
		uint16 m_MinorOperatingSystemVersion;
		uint16 m_MajorImageVersion;
		uint16 m_MinorImageVersion;
		uint16 m_MajorSubsystemVersion;
		uint16 m_MinorSubsystemVersion;
		uint32 m_Win32VersionValue;
		uint32 m_SizeOfImage;
		uint32 m_SizeOfHeaders;
		uint32 m_CheckSum;
		uint16 m_Subsystem;
		uint16 m_DllCharacteristics;
		uint64 m_SizeOfStackReserve;
		uint64 m_SizeOfStackCommit;
		uint64 m_SizeOfHeapReserve;
		uint64 m_SizeOfHeapCommit;
		uint32 m_LoaderFlags;
		uint32 m_NumberOfRvaAndSizes;
		CPEImageDataDirectory m_DataDirectory[EPEImageDirectoryEntries];
	};

	struct CNTImageHeaders
	{
		uint32 m_Signature;
		CNTImageFileHeader m_FileHeader;
	};

	struct CNTImageHeaders32
	{
		uint32 m_Signature;
		CNTImageFileHeader m_FileHeader;
		CPEOptionalHeaders32 m_Optional;
	};

	struct CNTImageHeaders64
	{
		uint32 m_Signature;
		CNTImageFileHeader m_FileHeader;
		CPEOptionalHeaders64 m_Optional;
	};

	struct CGuid
	{
		uint32 m_Data1;
		uint16 m_Data2;
		uint16 m_Data3;
		uint8 m_Data4[8];
	};

	struct CImageSectionHeader
	{
		uint8 m_Name[8];
		union
		{
			uint32 m_PhysicalAddress;
			uint32 m_VirtualSize;
		} m_Misc;
		uint32 m_VirtualAddress;
		uint32 m_SizeOfRawData;
		uint32 m_PointerToRawData;
		uint32 m_PointerToRelocations;
		uint32 m_PointerToLinenumbers;
		uint16 m_NumberOfRelocations;
		uint16 m_NumberOfLinenumbers;
		uint32 m_Characteristics;
	};

	struct CImageDebugDirectory
	{
		uint32 m_Characteristics;
		uint32 m_TimeDateStamp;
		uint16 m_MajorVersion;
		uint16 m_MinorVersion;
		uint32 m_Type;
		uint32 m_SizeOfData;
		uint32 m_AddressOfRawData;
		uint32 m_PointerToRawData;
	};

	struct CV_INFO_PDB70
	{
		uint32 m_CvSignature;
		CGuid m_Signature;     // unique identifier
		uint32 m_Age;          // an always-incrementing value
		ch8 m_PdbFileName[65535];  // zero terminated string with the name of the PDB file
	};

	struct CWindowsExecutableInfo
	{
		CTime m_Timestamp;
		CStr m_PDBGuid;
		CStr m_PDBFile;
		uint32 m_TimestampRaw = 0;
		uint32 m_SizeOfImage = 0;
	};

	CWindowsExecutableInfo fg_GetWindowsExecutableInfo(CStr const &_Path);
}

