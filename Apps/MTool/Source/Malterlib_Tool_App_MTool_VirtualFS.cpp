// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/BuildSystem/BuildSystemDependency>
#include <Mib/File/MalterlibFS>
#include <Mib/File/VirtualFSs/MalterlibFS>

struct CCOFFObject
{

	struct IMAGE_FILE_HEADER
	{
		IMAGE_FILE_HEADER()
		{
			fg_MemClear (*this);
		}
		uint16 m_Machine;
		uint16 m_NumberOfSections;
		uint32 m_TimeDateStamp;
		uint32 m_PointerToSymbolTable;
		uint32 m_NumberOfSymbols;
		uint16 m_SizeOfOptionalHeader;
		uint16 m_Characteristics;

		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const
		{
			_Stream << m_Machine;
			_Stream << m_NumberOfSections;
			_Stream << m_TimeDateStamp;
			_Stream << m_PointerToSymbolTable;
			_Stream << m_NumberOfSymbols;
			_Stream << m_SizeOfOptionalHeader;
			_Stream << m_Characteristics;
		}
	};

	struct IMAGE_SECTION_HEADER
	{
		IMAGE_SECTION_HEADER()
		{
			fg_MemClear (*this);
		}
		uint8 m_Name[8];
		uint32 m_VirtualSize;
		uint32 m_VirtualAddress;
		uint32 m_SizeOfRawData;
		uint32 m_PointerToRawData;
		uint32 m_PointerToRelocations;
		uint32 m_PointerToLinenumbers;
		uint16 m_NumberOfRelocations;
		uint16 m_NumberOfLinenumbers;
		uint32 m_Characteristics;

		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const
		{
			_Stream.f_FeedBytes(m_Name, 8);
			_Stream << m_VirtualSize;
			_Stream << m_VirtualAddress;
			_Stream << m_SizeOfRawData;
			_Stream << m_PointerToRawData;
			_Stream << m_PointerToRelocations;
			_Stream << m_PointerToLinenumbers;
			_Stream << m_NumberOfRelocations;
			_Stream << m_NumberOfLinenumbers;
			_Stream << m_Characteristics;
		}
	};

	struct IMAGE_SYMBOL
	{
		IMAGE_SYMBOL()
		{
			fg_MemClear(*this);
		}
		union
		{
			uint8 m_ShortName[8];
			struct
			{
				uint32 m_Short;     // if 0, use LongName
				uint32 m_Long;      // offset into string table
			} m_Name;
			uint32 m_LongName[2];
		} m_Name;
		uint32 m_Value;
		int16 m_SectionNumber;
		uint16 m_Type;
		uint8 m_StorageClass;
		uint8 m_NumberOfAuxSymbols;

		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const
		{
			_Stream.f_FeedBytes(m_Name.m_ShortName, 8);
			_Stream << m_Value;
			_Stream << m_SectionNumber;
			_Stream << m_Type;
			_Stream << m_StorageClass;
			_Stream << m_NumberOfAuxSymbols;
		}

	};

	struct STRING_TABLE
	{
		STRING_TABLE()
			: m_Size(sizeof(uint32))
		{
		}
		uint32 m_Size;
		TCVector<ch8> m_StringData;

		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const
		{
			_Stream << m_Size;
			_Stream.f_FeedBytes(m_StringData.f_GetArray(), m_StringData.f_GetLen());
		}
	};

	struct CDataMember
	{
		zmint m_iSymbol;
		CByteVector m_Data;
		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const
		{
			_Stream.f_FeedBytes(m_Data.f_GetArray(), m_Data.f_GetLen());
		}
	};

	struct CSection
	{
		TCVector<mint> m_Symbols;
		IMAGE_SECTION_HEADER m_Header;
	};

	struct CSymbol
	{
		mint m_iDataMember;
		IMAGE_SYMBOL m_Header;
	};

	struct CFile
	{
		IMAGE_FILE_HEADER  m_Header;
		TCVector<CSection> m_Sections;
		TCVector<CSymbol> m_Symbols;
		TCVector<CDataMember> m_DataMembers;
		STRING_TABLE m_StringTable;
		void f_AddSymbol(CStr const &_Name, CByteVector const &_Data, CSection *_pSection)
		{
			mint iSymbol = m_Symbols.f_GetLen();
			auto &Symbol = m_Symbols.f_Insert();

			Symbol.m_Header.m_Name.m_Name.m_Short = 0;
			Symbol.m_Header.m_Name.m_Name.m_Long = m_StringTable.m_StringData.f_GetLen() + sizeof(uint32);
			m_StringTable.m_StringData.f_Insert(_Name.f_GetStr(), _Name.f_GetLen() + 1);
			Symbol.m_Header.m_Type = uint16(0x0002) | uint16(3) << 8;
			Symbol.m_Header.m_StorageClass = 0x0002;
			Symbol.m_Header.m_SectionNumber = 1 + _pSection - m_Sections.f_GetArray();
			Symbol.m_Header.m_Value = 0;
			Symbol.m_iDataMember = m_DataMembers.f_GetLen();
			auto &DataMember = m_DataMembers.f_Insert();
			DataMember.m_iSymbol = iSymbol;
			DataMember.m_Data = _Data;
			_pSection->m_Symbols.f_Insert(iSymbol);
		}
		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream)
		{
			_Stream << m_Header;
			for (auto iSection = m_Sections.f_GetIterator(); iSection; ++iSection)
			{
				_Stream << iSection->m_Header;
				auto StartPos = _Stream.f_GetPosition();
				iSection->m_Header.m_PointerToRawData = StartPos;
				for (auto iSymbol = iSection->m_Symbols.f_GetIterator(); iSymbol; ++iSymbol)
				{
					auto &Symbol = m_Symbols[*iSymbol];
					auto &Data = m_DataMembers[Symbol.m_iDataMember];
					Symbol.m_Header.m_Value = _Stream.f_GetPosition() - StartPos;
					_Stream << Data;
					iSection->m_Header.m_SizeOfRawData += Data.m_Data.f_GetLen();
				}
			}
			m_Header.m_PointerToSymbolTable = _Stream.f_GetPosition();
			for (auto iSymbol = m_Symbols.f_GetIterator(); iSymbol; ++iSymbol)
				_Stream << iSymbol->m_Header;
			_Stream << m_StringTable;
		}
		template <typename tf_CStream>
		void f_Feed(tf_CStream &_Stream) const
		{
			fg_RemoveQualifiers(*this).f_Feed(_Stream);
		}
	};

	static CByteVector fs_GenerateData(CStr const &_SymbolName, CByteVector const &_Data)
	{
		CStr SymbolName;

		SymbolName = _SymbolName;

		CFile File;

		auto &Section = File.m_Sections.f_Insert();

		fg_StrCopy(Section.m_Header.m_Name, ".data", 8);
		Section.m_Header.m_Characteristics = 0xc0500040;

		File.f_AddSymbol(SymbolName, _Data, &Section);

		File.m_Header.m_NumberOfSections = File.m_Sections.f_GetLen();
		File.m_Header.m_NumberOfSymbols = File.m_Symbols.f_GetLen();
		File.m_Header.m_Characteristics = 4;
		File.m_StringTable.m_Size = File.m_StringTable.m_StringData.f_GetLen() + sizeof(uint32);
		// Start by calculating positions
		{
			TCBinaryStreamNull<> Stream;
			Stream << File;
		}

		CBinaryStreamMemory<> Stream;
		Stream << File;

		return Stream.f_MoveVector();
	}


};

class CTool_VirtualFS : public CTool
{
public:

	enum ETargetFormat
	{
		ETargetFormat_Binary
		, ETargetFormat_CPP
		, ETargetFormat_COFF
		, ETargetFormat_ASM
	};

	NTime::CTime f_GetDirectoryWriteTime(NStr::CStr const& _File) const
	{
		NFile::CFile File;
		File.f_Open(_File, EFileOpen_ShareAll | EFileOpen_ReadAttribs | EFileOpen_Directory);
		return File.f_GetWriteTime();
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{

		static char const* sc_pHelpText =
			"Usage:" DNewLine
			"MTool VirtualFS <Flags> <SourceFile>" DNewLine
			DNewLine
			"Where:" DNewLine
			"  <Flags> is made up of:" DNewLine
			"      -o <TargetFile> - Specify the target file to be written" DNewLine
			"      -ocpp <TargetFile> - Specify the target file to be written (cpp when -obj or -asm is specified)" DNewLine
			"    and either:" DNewLine
			"      -binary         - TargetFile will be written as a binary file." DNewLine
			"    or" DNewLine
			"      -obj			   - TargetFile will be written as a COFF object file." DNewLine
			"    or" DNewLine
			"      -asm			   - TargetFile will be written as a S file." DNewLine
			"    or" DNewLine
			"      -cpp            - TargetFile will be written as a cpp file (the default)." DNewLine
			"    and optionally:" DNewLine
			"      -verbose        - Detailed information on all operations performed will be displayed." DNewLine
			"      -odepend        - Output dependency in make format to file." DNewLine
			"      -platform       - The target platform. Currently this only matters for -asm and 'OSX' and 'Linux' is supported" DNewLine
			"      -oidsdepend     - Output dependency in ids dependency format to file." DNewLine
			"      -dest <Path>    - Path within VFS to prefix included files with" DNewLine
			"      -symbol <Name>  - The name of the exported symbol (decorated)" DNewLine
			"  <SourceFile> is the name of a registry file specifying the files to add" DNewLine;




		bool bVerbose = false;
		ETargetFormat TargetFormat = ETargetFormat_CPP;
		CStr SourceFilename;
		CStr TargetFilename;
		CStr TargetFilenameCpp;
		CStr DestPath;
		CStr DependencyFile;
		CStr DependencyContents;
		CStr MalterlibDependencyFile;
		CStr Platform = "OSX";
		NBuildSystem::CMalterlibDependencyTracker MalterlibDependencyTracker;

		auto f_IntToStr = [](int _Val) -> CStr
		{
			return CStr::CFormat("{}") << _Val;
		};

		int iCurArg = 0;
		CStr CurArg;
		CStr SymbolName = "gc_VFS_Bytes";
		for (;!(CurArg = _Params.f_GetValue(f_IntToStr(iCurArg), "")).f_IsEmpty(); ++iCurArg)
		{
			if (CurArg[0] == '-')
			{
				if (CurArg.f_CmpNoCase("-help") == 0)
				{
					DConOutRaw(sc_pHelpText);
					return 0;
				}
				else if (CurArg.f_CmpNoCase("-verbose") == 0)
					bVerbose = true;
				else if (CurArg.f_CmpNoCase("-binary") == 0)
					TargetFormat = ETargetFormat_Binary;
				else if (CurArg.f_CmpNoCase("-cpp") == 0)
					TargetFormat = ETargetFormat_CPP;
				else if (CurArg.f_CmpNoCase("-obj") == 0)
					TargetFormat = ETargetFormat_COFF;
				else if (CurArg.f_CmpNoCase("-asm") == 0)
					TargetFormat = ETargetFormat_ASM;
				else if ( 		(CurArg.f_CmpNoCase("-o") == 0)
							|| 	(CurArg.f_CmpNoCase("-output") == 0) )
				{
					++iCurArg;
					TargetFilename = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (TargetFilename.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No out file specified after -o[utput] flag" DNewLine);
					DConOutRaw(sc_pHelpText);
						return -1;
					}
				}
				else if (CurArg.f_CmpNoCase("-odepend") == 0)
				{
					++iCurArg;
					DependencyFile = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (DependencyFile.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No out file specified after -odepend flag" DNewLine);
						DConOutRaw(sc_pHelpText);
						return -1;
					}
				}
				else if (CurArg.f_CmpNoCase("-oidsdepend") == 0)
				{
					++iCurArg;
					MalterlibDependencyFile = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (MalterlibDependencyFile.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No out file specified after -oidsdepend flag" DNewLine);
						DConOutRaw(sc_pHelpText);
						return -1;
					}
				}
				else if (CurArg.f_CmpNoCase("-ocpp") == 0)
				{
					++iCurArg;
					TargetFilenameCpp = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (TargetFilenameCpp.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No out file specified after -ocpp flag" DNewLine);
					DConOutRaw(sc_pHelpText);
						return -1;
					}
				}
				else if (CurArg.f_CmpNoCase("-dest") == 0)
				{
					++iCurArg;
					DestPath = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (DestPath.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No dest path specified after -dest path" DNewLine);
					DConOutRaw(sc_pHelpText);
						return -1;
					}
				}
				else if (CurArg.f_CmpNoCase("-symbol") == 0)
				{
					++iCurArg;
					SymbolName = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (SymbolName.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No symbol name specified after -symbol name" DNewLine);
						DConOutRaw(sc_pHelpText);
						return -1;
					}
				}
				else if (CurArg.f_CmpNoCase("-platform") == 0)
				{
					++iCurArg;
					Platform = _Params.f_GetValue(f_IntToStr(iCurArg), "");
					if (Platform.f_IsEmpty())
					{
						DConOutRaw("VirtualFS: No platform specified after -platform" DNewLine);
						DConOutRaw(sc_pHelpText);
						return -1;
					}
				}


				else
				{
					DConOut("VirtualFS: Unrecognised flag: {}" DNewLine, CurArg);
					DConOutRaw(sc_pHelpText);
					return -1;
				}
			}
			else
				break;
		}

		if (TargetFilename.f_IsEmpty())
		{
			DConOutRaw("No target file specified." DNewLine);
			DConOutRaw(sc_pHelpText);
			return -1;
		}

		if (TargetFilenameCpp.f_IsEmpty() && (TargetFormat == ETargetFormat_COFF || TargetFormat == ETargetFormat_ASM || TargetFormat == ETargetFormat_CPP))
		{
			DConOutRaw("No cpp target file specified." DNewLine);
			DConOutRaw(sc_pHelpText);
			return -1;
		}

		SourceFilename = _Params.f_GetValue(f_IntToStr(iCurArg), "");
		++iCurArg;

		TargetFilename = TargetFilename.f_ReplaceChar('\\', '/');
		TargetFilenameCpp = TargetFilenameCpp.f_ReplaceChar('\\', '/');
		SourceFilename = SourceFilename.f_ReplaceChar('\\', '/');

		if (!NFile::CFile::fs_FileExists(SourceFilename, EFileAttrib_File))
		{
			DConOut("Source file {} does not exist." DNewLine, SourceFilename);
			return 1;
		}

		MalterlibDependencyTracker.f_AddOutputFile(TargetFilename);
		if (!TargetFilenameCpp.f_IsEmpty())
			MalterlibDependencyTracker.f_AddOutputFile(TargetFilenameCpp);

		DependencyContents += CStr::CFormat("dependencies: \\\n  {}") << SourceFilename;

		CRegistryPreserveAll Source;
		try
		{
			CStr SourceString;
			SourceString = NFile::CFile::fs_ReadStringFromFile(CStr(SourceFilename));

			Source.f_ParseStr(SourceString, SourceFilename);
		}
		catch(NFile::CExceptionFile& _Ex)
		{
			DConOut("Failed to read source file {}: {}" DNewLine, SourceFilename << _Ex.f_GetErrorStr());
			return 1;
		}

		struct CInclude
		{
			CStr m_Pattern;
			CStr m_Destination;
			bool m_bRecurse;
			CStr m_File;
			uint32 m_Line = 0;
		};

		TCVector<CInclude> lIncludes;
		for (auto IncludeIter = Source.f_GetChildIterator()
			;IncludeIter
			;++IncludeIter)
		{
			if (IncludeIter->f_GetName().f_CmpNoCase("Include") == 0)
			{
				CRegistryPreserveAll* pIncludeReg = IncludeIter;

				CInclude Inc;
				Inc.m_Pattern = pIncludeReg->f_GetValue("Pattern", "");
				Inc.m_Destination = pIncludeReg->f_GetValue("Destination", "");
				Inc.m_bRecurse = pIncludeReg->f_GetValue("Recurse", "1").f_ToInt(1);
				Inc.m_File = pIncludeReg->f_GetFile();
				Inc.m_Line = pIncludeReg->f_GetLine();

				if (Inc.m_Pattern.f_IsEmpty())
				{
					DConOut(DMibPFileLineFormat " error: No Pattern specified in include block{\n}", pIncludeReg->f_GetFile() << pIncludeReg->f_GetLine());
					return 1;
				}

				lIncludes.f_Insert(Inc);
			}
		}

		NStream::CBinaryStream* pActiveTargetStream = nullptr;
		NFile::TCBinaryStreamFile<> TargetFileStream;
		CBinaryStreamMemory<> TargetMemoryStream;
		NMib::NFile::CVirtualFS VirtualFS;

		if (TargetFormat == ETargetFormat_Binary)
		{
			EFileOpen FileMode = EFileOpen_Read | EFileOpen_ShareAll | EFileOpen_Write;
			TargetFileStream.f_Open(TargetFilename, FileMode);
			pActiveTargetStream = &TargetFileStream;
		}
		else if (TargetFormat == ETargetFormat_CPP || TargetFormat == ETargetFormat_COFF || TargetFormat == ETargetFormat_ASM)
		{
			pActiveTargetStream = &TargetMemoryStream;
		}

		if (!pActiveTargetStream)
		{
			DConOutRaw("Internal error: No valid target format." DNewLine);
			return 1;
		}

		int32 const GrowSize = 1024;
		int32 const ClusterSize = 1024;
		VirtualFS.f_Create(pActiveTargetStream, ClusterSize, GrowSize, GrowSize);

#if defined(DPlatformFamily_Windows)
		CStr TLogFileName = CStr::CFormat("{}/{}.read.1.appendtlog") << CFile::fs_GetPath(TargetFilename) << CFile::fs_GetFileNoExt(CFile::fs_GetProgramPath());
		CFile TLogFile;
		TLogFile.f_Open(TLogFileName, EFileOpen_Write | EFileOpen_ShareAll);
		CByteVector Data;
		//CStr InitialData = CStr::CFormat("^{}" DNewLine) << SourceFilename.f_ReplaceChar('/', '\\').f_UpperCase();
		CStr InitialData;
		CFile::fs_WriteStringToVector(Data, InitialData);
		try
		{
			TLogFile.f_Write(Data.f_GetArray(), Data.f_GetLen());
		}
		catch (NException::CException const &_Exception)
		{
			DConOut("Exception writing tlog: {}" DNewLine, _Exception.f_GetErrorStr());
		}
#endif

		TCSet<CStr> DependentDirectories;

		CFileSystemInterface_Disk DiskSystem;

		mint nFilesAdded = 0;
		for (auto CurInclude = lIncludes.f_GetIterator()
			;CurInclude
			;++CurInclude)
		{
			if (bVerbose)
				DConOut("{}{}" DNewLine, CurInclude->m_Pattern << (CurInclude->m_bRecurse ? " (Recursive)" : ""));

			TCVector<CStr> lSourceFiles = NFile::CFile::fs_FindFiles(CurInclude->m_Pattern, EFileAttrib_File | EFileAttrib_Link, CurInclude->m_bRecurse, false);

			if (lSourceFiles.f_IsEmpty() && !CurInclude->m_bRecurse && CurInclude->m_Pattern.f_FindChars("*?") < 0)
			{
				DConOut(DMibPFileLineFormat " error: No files found for pattern '{}'{\n}", CurInclude->m_File << CurInclude->m_Line << CurInclude->m_Pattern);
				return 1;
			}

			CStr BasePath = NFile::CFile::fs_GetPath(CurInclude->m_Pattern);

			MalterlibDependencyTracker.f_AddFind(CurInclude->m_Pattern, CurInclude->m_bRecurse, false, EFileAttrib_File | EFileAttrib_Link, lSourceFiles);

			{
				CStr BaseDepend = BasePath;
				while (!BaseDepend.f_IsEmpty() && !CFile::fs_FileExists(BaseDepend, EFileAttrib_Directory))
					BaseDepend = NFile::CFile::fs_GetPath(BaseDepend);
				if (!BaseDepend.f_IsEmpty())
					DependentDirectories[BaseDepend];
			}

			if (CurInclude->m_bRecurse)
			{
				TCVector<CStr> DependDirs = NFile::CFile::fs_FindFiles(BasePath + "/*", EFileAttrib_Directory, true, false);

				for (auto iDepend = DependDirs.f_GetIterator(); iDepend; ++iDepend)
					DependentDirectories[*iDepend];
			}

			{
				TCVector<CStr> BaseFiles = NFile::CFile::fs_FindFiles(BasePath, EFileAttrib_Directory, false);
				if (BaseFiles.f_GetLen())
					BasePath = BaseFiles[0];
			}

			mint Len = BasePath.f_GetLen();

			for (mint i = 0; i < lSourceFiles.f_GetLen(); ++i)
			{
				auto & SourceFile = lSourceFiles[i];
				{
					while (!BasePath.f_IsEmpty() && !CFile::fs_FileExists(BasePath, EFileAttrib_Directory))
					{
						BasePath = NFile::CFile::fs_GetPath(BasePath);
					}
				}

				CStr SourceName = SourceFile.f_Extract(Len+1);
				if (SourceName[0] == '.')
					continue;
				if (CFile::fs_GetFile(SourceName) == ".DS_Store")
					continue;

				CStr AddPath = NFile::CFile::fs_AppendPath<CStr>(CurInclude->m_Destination, SourceName);

				if (bVerbose)
					DConOut("Adding '{}' as '{}'" DNewLine, SourceFile << AddPath);

				++nFilesAdded;

				CStr Dir = NFile::CFile::fs_GetPath(AddPath);
				if (!Dir.f_IsEmpty())
					VirtualFS.f_CreateDirectory(Dir);

				MalterlibDependencyTracker.f_AddInputFile(SourceFile);

				DependencyContents += CStr::CFormat(" \\\n  {}") << SourceFile;

				VirtualFS.f_CopyFileToFS(SourceFile, AddPath);
			}
		}
		DConOut("{} files added to VFS" DNewLine, nFilesAdded);

		for (auto iDepend = DependentDirectories.f_GetIterator(); iDepend; ++iDepend)
		{
			CFile Test;
			CStr DependDir = *iDepend;
			try
			{
				if (CFile::fs_GetFile(DependDir) != "Intermediate")
				{
					Test.f_Open(DependDir, EFileOpen_ReadAttribs | EFileOpen_ShareAll | EFileOpen_Directory);
					Test.f_GetAttributes();

#if defined(DPlatformFamily_Windows)
					CStr ToLog = DependDir.f_ReplaceChar('/', '\\').f_UpperCase() + "\\" DNewLine;
					TLogFile.f_Write(ToLog.f_GetStr(), ToLog.f_GetLen());
#endif
//					DependencyContents += CStr::CFormat(" \\\n  {}") << DependDir;
				}

			}
			catch (NException::CException const &_Exception)
			{
				DConOut("Exception reading depend path: {}" DNewLine, _Exception.f_GetErrorStr());
			}
		}

		VirtualFS.f_Close();

		if (TargetFormat == ETargetFormat_Binary)
		{
			TargetFileStream.f_Close(); // Can this throw?
		}
		else if (TargetFormat == ETargetFormat_COFF)
		{
			CByteVector lData = TargetMemoryStream.f_MoveVector();

			auto Hash = CHash_MD5::fs_DigestFromData(lData);

			CStr UniqueName = Hash.f_GetString();

			CStr CppOutput = CStr::CFormat(
						"// VFS Blob generated by MTool from {}" DNewLine //  @ {}" DNewLine
						"namespace NAOCC" DNewLine
						"{{" DNewLine
						"	void fg_SetExeFSData(int _nBytes, void const *_pData);" DNewLine
						"}" DNewLine
						DNewLine
						"int const gc_VFS_{1}_nBytes = {2};" DNewLine
						"extern \"C\" unsigned char gc_VFS_{1}_Bytes[];" DNewLine DNewLine
					)
					<< SourceFilename
					<< UniqueName
					<< lData.f_GetLen();


			CppOutput +=
				CStr::CFormat
				(
					"struct CExeFSSetter" DNewLine
					"{{" DNewLine
					"	CExeFSSetter() {{ NAOCC::fg_SetExeFSData(gc_VFS_{0}_nBytes, gc_VFS_{0}_Bytes); }" DNewLine
					"};" DNewLine
					DNewLine
					"CExeFSSetter g_SetExeFS;" DNewLine DNewLine
				) << UniqueName
			;

			try
			{
				NFile::CFile::fs_WriteStringToFile(CStr(TargetFilenameCpp), CppOutput);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write target CPP file {}: {}" DNewLine, TargetFilenameCpp << _Ex.f_GetErrorStr());
				return 1;
			}

			SymbolName = SymbolName.f_Replace("gc_VFS_Bytes", CStr(CStr::CFormat("gc_VFS_{}_Bytes") << UniqueName));

			try
			{
				auto Data = CCOFFObject::fs_GenerateData(SymbolName, lData);

				NFile::CFile::fs_WriteFile(CStr(TargetFilename), Data);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write target OBJ file {}: {}" DNewLine, TargetFilename << _Ex.f_GetErrorStr());
				return 1;
			}
		}
		else if (TargetFormat == ETargetFormat_ASM)
		{
			CByteVector lData = TargetMemoryStream.f_MoveVector();

			auto Hash = CHash_MD5::fs_DigestFromData(lData);

			CStr UniqueName = Hash.f_GetString();

			CStr CppOutput = CStr::CFormat(
						"// VFS Blob generated by MTool from {}" DNewLine //  @ {}" DNewLine
						"namespace NAOCC" DNewLine
						"{{" DNewLine
						"	void fg_SetExeFSData(int _nBytes, void const *_pData);" DNewLine
						"}" DNewLine
						DNewLine
						"int const gc_VFS_{1}_nBytes = {2};" DNewLine
						"extern \"C\" unsigned char gc_VFS_{1}_Bytes[];" DNewLine DNewLine
					)
					<< SourceFilename
					<< UniqueName
					<< lData.f_GetLen();


			CppOutput +=
				CStr::CFormat
				(
					"struct CExeFSSetter" DNewLine
					"{{" DNewLine
					"	CExeFSSetter() {{ NAOCC::fg_SetExeFSData(gc_VFS_{0}_nBytes, gc_VFS_{0}_Bytes); }" DNewLine
					"};" DNewLine
					DNewLine
					"CExeFSSetter g_SetExeFS;" DNewLine DNewLine
				) << UniqueName
			;

			try
			{
				NFile::CFile::fs_WriteStringToFile(TargetFilenameCpp, CppOutput);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write target CPP file {}: {}" DNewLine, TargetFilenameCpp << _Ex.f_GetErrorStr());
				return 1;
			}

			SymbolName = SymbolName.f_Replace("gc_VFS_Bytes", CStr(CStr::CFormat("gc_VFS_{}_Bytes") << UniqueName));

			CStr BinFileName = CFile::fs_AppendPath(CFile::fs_GetPath(TargetFilename), CFile::fs_GetFileNoExt(TargetFilename) + ".bin");
			try
			{
				MalterlibDependencyTracker.f_AddOutputFile(BinFileName);
				NFile::CFile::fs_WriteFile(BinFileName, lData);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write target BIN file {}: {}" DNewLine, TargetFilename << _Ex.f_GetErrorStr());
				return 1;
			}

			try
			{

				CStr ASMOutput;
				if (Platform == "OSX")
				{
					ASMOutput = CStr::CFormat
						(
							"{\n}"
							"	.global _gc_VFS_{0}_Bytes{\n}"
							"	.static_const{\n}"
							"_gc_VFS_{0}_Bytes:{\n}"
							"	.incbin \"{1}\"{\n}"
						)
						<< UniqueName
						<< BinFileName
					;
				}
				else
				{
					ASMOutput = CStr::CFormat
						(
							"{\n}"
							"	.global gc_VFS_{0}_Bytes{\n}"
							"	.section .rodata{\n}"
							"gc_VFS_{0}_Bytes:{\n}"
							"	.incbin \"{1}\"{\n}"
						)
						<< UniqueName
						<< BinFileName
					;
				}
				NFile::CFile::fs_WriteStringToFile(TargetFilename, ASMOutput);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write target BIN file {}: {}" DNewLine, TargetFilename << _Ex.f_GetErrorStr());
				return 1;
			}
		}
		else if (TargetFormat == ETargetFormat_CPP)
		{
			CByteVector lData = TargetMemoryStream.f_MoveVector();

			CStr CppOutput = CStr::CFormat(
						"// VFS Blob generated by MTool from {}" DNewLine //  @ {}" DNewLine
						"namespace NAOCC" DNewLine
						"{{" DNewLine
						"	void fg_SetExeFSData(int _nBytes, void const *_pData);" DNewLine
						"}" DNewLine
						DNewLine
						"int const gc_VFS_nBytes = {};" DNewLine
						"unsigned char const gc_VFS_Bytes[] = {{" DNewLine
					)
					<< SourceFilename
//					<< NTime::fg_GetFullTimeStr(NTime::CTime::fs_NowLocal())
					<< lData.f_GetLen();

			auto Formatter = CStr::CFormat("\t0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}, 0x{nfh,sz2,sf0}," DNewLine);
			auto ByteFormatter = CStr::CFormat("0x{nfh,sz2,sf0}, ");

			uint8 Input[16];
			Formatter
					<< Input[0]  << Input[1]  << Input[2]  << Input[3]
					<< Input[4]  << Input[5]  << Input[6]  << Input[7]
					<< Input[8]  << Input[9]  << Input[10] << Input[11]
					<< Input[12] << Input[13] << Input[14] << Input[15];
			ByteFormatter << Input[0];


			mint nBytesLeft = lData.f_GetLen();
			auto DataIter = lData.f_GetIterator();

			while(nBytesLeft >= 16)
			{
				fg_MemCopy(Input, &*DataIter, 16);
				CppOutput += Formatter;

				DataIter += 16;
				nBytesLeft -= 16;
			}

			if (nBytesLeft)
			{
				CppOutput += "\t";
				for (;nBytesLeft;--nBytesLeft)
				{
					Input[0] = *DataIter;
					CppOutput += ByteFormatter;
					++DataIter;
				}
				CppOutput += DNewLine;
			}

			CppOutput += "};" DNewLine DNewLine;

			CppOutput +=
				"struct CExeFSSetter" DNewLine
				"{" DNewLine
				"	CExeFSSetter() { NAOCC::fg_SetExeFSData(gc_VFS_nBytes, gc_VFS_Bytes); }" DNewLine
				"};" DNewLine
				DNewLine
				"CExeFSSetter g_SetExeFS;" DNewLine DNewLine;

			try
			{
				NFile::CFile::fs_WriteStringToFile(CStr(TargetFilenameCpp), CppOutput);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write target CPP file {}: {}" DNewLine, TargetFilenameCpp << _Ex.f_GetErrorStr());
				return 1;
			}
		}

		if (!DependencyFile.f_IsEmpty())
		{
			try
			{
				NFile::CFile::fs_CreateDirectory(CFile::fs_GetPath(DependencyFile));
				DependencyContents += "\n";
				NFile::CFile::fs_WriteStringToFile(CStr(DependencyFile), DependencyContents, false);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write dependency file file {}: {}" DNewLine, DependencyFile << _Ex.f_GetErrorStr());
				return 1;
			}
		}

		if (!MalterlibDependencyFile.f_IsEmpty())
		{
			try
			{
				MalterlibDependencyTracker.f_WriteDependencyFile(MalterlibDependencyFile);
			}
			catch(NFile::CExceptionFile& _Ex)
			{
				DConOut("Failed to write dependency file file {}: {}" DNewLine, DependencyFile << _Ex.f_GetErrorStr());
				return 1;
			}
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_VirtualFS);
