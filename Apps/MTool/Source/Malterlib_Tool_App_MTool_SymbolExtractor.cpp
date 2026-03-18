// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include "zlib.h"
#include "contrib/minizip/ioapi.h"
#include "contrib/minizip/unzip.h"

#include <Mib/Encoding/EJson>
#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Container/Regions>
#include <Mib/Cryptography/RandomID>

#include "Malterlib_Tool_App_MTool_WindowsSymbols.h"

namespace
{
	class CBinaryStreamMiniZipAdaptor
	{
		static voidpf ZCALLBACK fp_fopen_func (voidpf opaque, const char* filename, int mode)
		{
			CFile *pFile = DNew CFile;
			if (pFile == nullptr)
				return nullptr;

			CStr FileName(filename);

			EFileOpen OpenMode = EFileOpen_None;

			if (mode & ZLIB_FILEFUNC_MODE_READ)
				OpenMode |= EFileOpen_Read;
			if (mode & ZLIB_FILEFUNC_MODE_WRITE)
				OpenMode |= EFileOpen_Write;

			if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
				OpenMode |= EFileOpen_DontTruncate;
			else
				OpenMode |= EFileOpen_DontOpenExisting;

			if (!(mode & ZLIB_FILEFUNC_MODE_CREATE))
				OpenMode |= EFileOpen_DontCreate;

			try
			{
				pFile->f_Open(FileName, OpenMode);
			}
			catch (CException const &_Exception)
			{
				DConErrOut("Exception opening zip file: {}", _Exception);
			}

			return pFile;
		}

		static voidpf ZCALLBACK fp_fopen64_func (voidpf opaque, const void* filename, int mode)
		{
			return fp_fopen_func(opaque, (const ch8 *)filename, mode);
		}


		static uLong ZCALLBACK fp_fread_func (voidpf opaque, voidpf stream, void* buf, uLong size)
		{
			CFile *pFile = (CFile *)stream;

			pFile->f_Read(buf, size);
			return size;
		}


		static uLong ZCALLBACK fp_fwrite_func (voidpf opaque, voidpf stream, const void* buf, uLong size)
		{
			CFile *pFile = (CFile *)stream;

			pFile->f_Write(buf, size);
			return size;
		}

		static long ZCALLBACK fp_ftell_func (voidpf opaque, voidpf stream)
		{
			CFile *pFile = (CFile *)stream;

			return pFile->f_GetPosition();
		}

		static ZPOS64_T ZCALLBACK fp_ftell64_func (voidpf opaque, voidpf stream)
		{
			CFile *pFile = (CFile *)stream;

			return pFile->f_GetPosition();
		}

		static long ZCALLBACK fp_fseek_func (voidpf opaque, voidpf stream, uLong offset, int origin)
		{
			CFile *pFile = (CFile *)stream;

			switch (origin)
			{
			case ZLIB_FILEFUNC_SEEK_CUR :
				pFile->f_AddPosition(offset);
				break;
			case ZLIB_FILEFUNC_SEEK_END :
				pFile->f_SetPositionFromEnd(offset);
				break;
			case ZLIB_FILEFUNC_SEEK_SET :
				pFile->f_SetPosition(offset);
				break;
			default: return -1;
			}

			return 0;
		}

		static long ZCALLBACK fp_fseek64_func (voidpf opaque, voidpf stream, ZPOS64_T offset, int origin)
		{
			CFile *pFile = (CFile *)stream;

			switch (origin)
			{
			case ZLIB_FILEFUNC_SEEK_CUR :
				pFile->f_AddPosition(offset);
				break;
			case ZLIB_FILEFUNC_SEEK_END :
				pFile->f_SetPositionFromEnd(offset);
				break;
			case ZLIB_FILEFUNC_SEEK_SET :
				pFile->f_SetPosition(offset);
				break;
			default: return -1;
			}

			return 0;
		}

		static int ZCALLBACK fp_fclose_func (voidpf opaque, voidpf stream)
		{
			CFile *pFile = (CFile *)stream;

			pFile->f_Close();
			delete pFile;
			return 0;
		}

		static int ZCALLBACK fp_ferror_func (voidpf opaque, voidpf stream)
		{
			/* We never return errors */
			return 0;
		}

	public:
		CBinaryStreamMiniZipAdaptor()
		{
		}

		void f_SetFunctions(zlib_filefunc_def *_pFileFunc)
		{
			_pFileFunc->zopen_file = fp_fopen_func;
			_pFileFunc->zread_file = fp_fread_func;
			_pFileFunc->zwrite_file = fp_fwrite_func;
			_pFileFunc->ztell_file = fp_ftell_func;
			_pFileFunc->zseek_file = fp_fseek_func;
			_pFileFunc->zclose_file = fp_fclose_func;
			_pFileFunc->zerror_file = fp_ferror_func;
			_pFileFunc->opaque = this;
		}

		void f_SetFunctions(zlib_filefunc64_def *_pFileFunc)
		{
			_pFileFunc->zopen64_file = fp_fopen64_func;
			_pFileFunc->zread_file = fp_fread_func;
			_pFileFunc->zwrite_file = fp_fwrite_func;
			_pFileFunc->ztell64_file = fp_ftell64_func;
			_pFileFunc->zseek64_file = fp_fseek64_func;
			_pFileFunc->zclose_file = fp_fclose_func;
			_pFileFunc->zerror_file = fp_ferror_func;
			_pFileFunc->opaque = this;
		}
	};
}

class CTool_SymbolExtractor : public CTool2
{
public:

	struct CParams
	{
		CParams(TCMap<CStr, CStr> const &_Params)
			: m_Params(_Params)
		{
		}


		CStr const &operator [](CStr const &_Key) const
		{
			if (auto *pParam = m_Params.f_FindEqual(_Key))
				return *pParam;

			DError(fg_Format("Missing param: {}=", _Key));
		}

		CStr f_GetValue(CStr const &_Key, CStr const &_DefaultValue) const
		{
			if (auto *pParam = m_Params.f_FindEqual(_Key))
				return *pParam;
			return _DefaultValue;
		}

		TCMap<CStr, CStr> const &m_Params;
	};

	CTime f_GetExecutableTimestamp(CStr const &_Path, CStr &o_PDBFile)
	{
		NTool::CWindowsExecutableInfo ExecutableInfo = NTool::fg_GetWindowsExecutableInfo(_Path);
		o_PDBFile = ExecutableInfo.m_PDBGuid;
		return ExecutableInfo.m_Timestamp;
	}

	static void fs_CheckUnzipError(int _Error, CStr const &_File)
	{
		switch (_Error)
		{
		case UNZ_OK: return;
		case UNZ_END_OF_LIST_OF_FILE: DError(fg_Format("Unzip: End of list of file: {}", _File));
		case UNZ_ERRNO: DError(fg_Format("Unzip: Errno, {}", _File));
		case UNZ_PARAMERROR: DError(fg_Format("Unzip: Param error: {}", _File));
		case UNZ_BADZIPFILE: DError(fg_Format("Unzip: bad zip file: {}", _File));
		case UNZ_INTERNALERROR: DError(fg_Format("Unzip: internal error: {}", _File));
		case UNZ_CRCERROR: DError(fg_Format("Unzip: CRC error: {}", _File));
		}

		DError(fg_Format("Unzip: {}: {}", _Error, _File));
	}

	CTime f_GetZipTimestamp(CStr const &_Path)
	{
		CBinaryStreamMiniZipAdaptor Adaptor;

		zlib_filefunc64_def Functions;
		Adaptor.f_SetFunctions(&Functions);

		unzFile pZipFile = unzOpen2_64(_Path, &Functions);
		if (!pZipFile)
			DError(fg_Format("Failed to create zip file: {}", _Path));
		auto Cleanup = g_OnScopeExit / [&]()
			{
				unzClose(pZipFile);
			}
		;

		fs_CheckUnzipError(unzGoToFirstFile(pZipFile), _Path);

		unz_file_info64 FileInfo;
		CStr FileName;
		CByteVector ExtraField;
		ExtraField.f_SetLen(65536);
		CStr Comment;
		fs_CheckUnzipError
			(
				unzGetCurrentFileInfo64(pZipFile, &FileInfo, FileName.f_GetStr(256), 256, ExtraField.f_GetArray(), ExtraField.f_GetLen(), Comment.f_GetStr(256), 256)
				, _Path
			)
		;

		auto &Date = FileInfo.tmu_date;

		if
			(
				fg_Clamp(Date.tm_mon, 0, 11) != Date.tm_mon
				|| fg_Clamp(Date.tm_mday, 1, int32(CTimeConvert::fs_GetDaysInMonth(Date.tm_year, Date.tm_mon))) != Date.tm_mday
				|| fg_Clamp(Date.tm_hour, 0, 23) != Date.tm_hour
				|| fg_Clamp(Date.tm_min, 0, 59) != Date.tm_min
				|| fg_Clamp(Date.tm_sec, 0, 59) != Date.tm_sec
			)
		{
			DError(fg_Format("Invalid date in zip file: {}", _Path));
		}

		CTime Time = CTimeConvert::fs_CreateTime(Date.tm_year, Date.tm_mon + 1, Date.tm_mday, Date.tm_hour, Date.tm_min, Date.tm_sec).f_ToLocal();

		ExtraField.f_SetLen(FileInfo.size_file_extra);

		CBinaryStreamMemoryConstRef<> ExtraStream(ExtraField);

		bool bFoundTimeHeader = false;

		while (!ExtraStream.f_IsAtEndOfStream())
		{
			uint16 HeaderID;
			ExtraStream >> HeaderID;
			uint16 HeaderSize;
			ExtraStream >> HeaderSize;
			auto StartPos = ExtraStream.f_GetPosition();

			if (HeaderID == 0x000d)
			{
			}
			else if (HeaderID == 0x5455)
			{
				uint8 Flags;
				uint32 UnixTimestamp;

				ExtraStream >> Flags;
				ExtraStream >> UnixTimestamp;

				Time = CTimeConvert::fs_FromUnixSeconds(UnixTimestamp);
				bFoundTimeHeader = true;
			}

			ExtraStream.f_SetPosition(StartPos + HeaderSize);
		}

		if (!bFoundTimeHeader)
			DConErrOut("WARNING {}: Did not find extended timestamp header\n", _Path);

		return Time;
	}

	struct CDatabase
	{
		struct CSymbolFile
		{
			CStr m_BasePath;
			CStr m_FileName;

			auto operator <=> (CSymbolFile const &_Right) const noexcept = default;
		};

		TCRegions<CTime> m_SourceTimes;
		TCMap<CTime, TCSet<CStr>> m_SourceFilesByTime;

		TCMap<CTime, TCSet<CSymbolFile>> m_SymbolFilesByTime;
		TCMap<CStr, CTime> m_PDBTimestamps;
		TCMap<CStr, CSymbolFile> m_OutstandingPDPFiles;

		void f_Read(CStr const &_FileName)
		{
			CEJsonSorted Database;

			if (CFile::fs_FileExists(_FileName))
				Database = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(_FileName, true), _FileName);
			else
				return;

			for (auto &Region : Database["SourceRegions"].f_Array())
			{
				auto &Start = Region["Start"].f_Date();
				m_SourceTimes.f_MakeRegion(Start, Region["End"].f_Date());
			}

			for (auto &File : Database["SourceFiles"].f_Array())
				m_SourceFilesByTime[File["Time"].f_Date()][File["FileName"].f_String()];

			for (auto &File : Database["SymbolFiles"].f_Array())
				m_SymbolFilesByTime[File["Time"].f_Date()][CDatabase::CSymbolFile{File["BasePath"].f_String(), File["FileName"].f_String()}];

			for (auto &Timestamp : Database["PDBTimeStamps"].f_Object())
				m_PDBTimestamps[Timestamp.f_Name()] = Timestamp.f_Value().f_Date();

			for (auto &File : Database["OutstandingPDBFiles"].f_Object())
				m_OutstandingPDPFiles[File.f_Name()] = CDatabase::CSymbolFile{File.f_Value()["BasePath"].f_String(), File.f_Value()["FileName"].f_String()};
		}

		void f_Write(CStr const &_FileName) const
		{
			CEJsonSorted Database;

			{
				auto &OutRegions = Database["SourceRegions"] = EJsonType_Array;

				for (auto &Time : m_SourceTimes)
				{
					CEJsonSorted TimeData = {"Start"_= Time.f_Start(), "End"_= Time.f_End()};

					auto &Files = TimeData["Files"] = EJsonType_Array;

					TCSet<CStr> FileNames;
					auto iFiles = m_SourceFilesByTime.f_GetIterator_SmallestGreaterThanEqual(Time.f_Start());
					for (; iFiles && iFiles.f_GetKey() <= Time.f_End(); ++iFiles)
					{
						for (auto &FileName : *iFiles)
							FileNames[FileName];
					}

					for (auto &FileName : FileNames)
						Files.f_Insert(FileName);

					OutRegions.f_Insert(fg_Move(TimeData));
				}
			}

			{
				auto &OutFiles = Database["SourceFiles"] = EJsonType_Array;

				for (auto &Files : m_SourceFilesByTime)
				{
					for (auto &File : Files)
					{
						CEJsonSorted FileData = {"Time"_= m_SourceFilesByTime.fs_GetKey(Files), "FileName"_= File};

						OutFiles.f_Insert(fg_Move(FileData));
					}
				}
			}

			{
				auto &OutFiles = Database["SymbolFiles"] = EJsonType_Array;

				for (auto &Files : m_SymbolFilesByTime)
				{
					auto &FileTime = m_SymbolFilesByTime.fs_GetKey(Files);

					for (auto &File : Files)
					{
						OutFiles.f_Insert() = _=
							{
								"Time"_= FileTime
								, "BasePath"_= File.m_BasePath
								, "FileName"_= File.m_FileName
							}
						;
					}
				}
			}

			{
				auto &Timestamps = Database["PDBTimeStamps"] = EJsonType_Object;

				for (auto &Time : m_PDBTimestamps)
				{
					Timestamps[m_PDBTimestamps.fs_GetKey(Time)] = Time;
				}
			}

			{
				auto &OutFiles = Database["OutstandingPDBFiles"] = EJsonType_Object;

				for (auto &File : m_OutstandingPDPFiles)
				{
					OutFiles[m_OutstandingPDPFiles.fs_GetKey(File)] = _=
						{
							"BasePath"_= File.m_BasePath
							, "FileName"_= File.m_FileName
						}
					;
				}
			}


			CFile::fs_WriteStringToFile(_FileName, Database.f_ToString(), false);
		}
	};

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CParams Params(_Params);

		CStr DatabaseFileName = CFile::fs_GetExpandedPath(CStr("SymbolDatabase.json"));
		CStr Action = Params["Action"];

		if (Action == "ExtractTimeRanges")
		{
			CTimeSpan StartMargin = CTimeSpanConvert::fs_CreateHourSpan(-2);
			CTimeSpan EndMargin = CTimeSpanConvert::fs_CreateHourSpan(2);
			CDatabase Database;

			Database.f_Read(DatabaseFileName);

			CStr SourceDirectory = CFile::fs_GetExpandedPath(Params["SourceDirectory"]);

			TCVector<CStr> ExecutableFiles = CFile::fs_FindFiles(SourceDirectory + "/*.exe", EFileAttrib_File, true);
			ExecutableFiles.f_Insert(CFile::fs_FindFiles(SourceDirectory + "/*.dll", EFileAttrib_File, true));

			for (auto &File : ExecutableFiles)
			{
				CTime FileTime;
				try
				{
					CStr PDBGuid;
					FileTime = f_GetExecutableTimestamp(File, PDBGuid);
				}
				catch (CException const &_Error)
				{
					DConErrOut("Failed to extract timestamp: {}\n", _Error);
					continue;
				}

				Database.m_SourceTimes.f_MakeRegion(FileTime + StartMargin, FileTime + EndMargin);
				Database.m_SourceFilesByTime[FileTime][File];
			}

			Database.f_Write(DatabaseFileName);
		}
		else if (Action == "BuildSymbolDatabase")
		{
			CDatabase Database;

			Database.f_Read(DatabaseFileName);

			CStr SourceDirectory = CFile::fs_GetExpandedPath(Params["SourceDirectory"]);

			CStr Wildcard = Params.f_GetValue("Wildcard", "*");

			DConErrOut("Enumerating files\n");
			TCVector<CStr> Files = CFile::fs_FindFiles(fg_Format("{}/{}", SourceDirectory, Wildcard), EFileAttrib_File, true);

			mint iFile = 0;
			mint LastPercent = 0;

			for (auto &File : Files)
			{
				mint Percent = (iFile * 100) / Files.f_GetLen();
				if (Percent != LastPercent)
				{
					LastPercent = Percent;
					DConErrOut("{} %\n", Percent);
				}
				++iFile;
				CStr Extension = CFile::fs_GetExtension(File).f_LowerCase();

				CTime WriteTime;

				CDatabase::CSymbolFile SymbolFile{SourceDirectory, File.f_Extract(SourceDirectory.f_GetLen() + 1)};

				try
				{
					if (Extension == "exe" || Extension == "dll" || Extension == "com")
					{
						CStr PDBGuid;
						WriteTime = f_GetExecutableTimestamp(File, PDBGuid);
						if (!PDBGuid.f_IsEmpty())
							Database.m_PDBTimestamps[PDBGuid] = WriteTime;
					}
					else if (Extension == "zip")
						WriteTime = f_GetZipTimestamp(File);
					else if (Extension == "pdb")
					{
						CStr PDBGuid = CFile::fs_GetFile(CFile::fs_GetPath(File)).f_LowerCase();

						if (PDBGuid.f_IsEmpty())
						{
							DConErrOut("Invalid PDB GUID: {}\n", File);
							continue;
						}

						Database.m_OutstandingPDPFiles[PDBGuid] = SymbolFile;

						continue;
					}
					else
					{
						DConErrOut("Ignored: {}\n", File);
						continue;
					}
				}
				catch (CException const &_Exception)
				{
					DConErrOut("Exception: {}\n", _Exception);
					continue;
				}

				Database.m_SymbolFilesByTime[WriteTime][SymbolFile];
			}

			TCSet<CStr> ToClear;

			for (auto &SymbolFile : Database.m_OutstandingPDPFiles)
			{
				auto &PDBGuid = Database.m_OutstandingPDPFiles.fs_GetKey(SymbolFile);
				auto *pTimestamp = Database.m_PDBTimestamps.f_FindEqual(PDBGuid);
				if (!pTimestamp)
					continue;

				ToClear[PDBGuid];
				Database.m_SymbolFilesByTime[*pTimestamp][SymbolFile];
			}

			for (auto &Guid : ToClear)
				Database.m_OutstandingPDPFiles.f_Remove(Guid);

			for (auto &Outstanding : Database.m_OutstandingPDPFiles)
				DConErrOut("Missing timestamp: {}/{}\n", Outstanding.m_BasePath, Outstanding.m_FileName);

			Database.f_Write(DatabaseFileName);
		}
		else if (Action == "List")
		{
			CDatabase Database;
			Database.f_Read(DatabaseFileName);

			for (auto iRange = Database.m_SourceTimes.f_GetIterator(); iRange; ++iRange)
			{
				CTime const &StartTime = iRange->f_Start();
				CTime const &EndTime = iRange->f_End();

				DConOut("{} -> {}\n", StartTime.f_ToLocal(), EndTime.f_ToLocal());

				TCMap<CStr, CTime> SourceFileNames;
				for (auto iFiles = Database.m_SourceFilesByTime.f_GetIterator_SmallestGreaterThanEqual(StartTime); iFiles && iFiles.f_GetKey() <= EndTime; ++iFiles)
				{
					for (auto &FileName : *iFiles)
						SourceFileNames[FileName] = iFiles.f_GetKey();
				}

				TCMap<CDatabase::CSymbolFile, CTime> SymbolFileNames;
				for (auto iFiles = Database.m_SymbolFilesByTime.f_GetIterator_SmallestGreaterThanEqual(StartTime); iFiles && iFiles.f_GetKey() <= EndTime; ++iFiles)
				{
					for (auto &FileName : *iFiles)
						SymbolFileNames[FileName] = iFiles.f_GetKey();
				}

				DConOut("    Source Files\n");
				for (auto &Time : SourceFileNames)
				{
					auto &FileName = SourceFileNames.fs_GetKey(Time);
					DConOut("        {} - {}\n", Time.f_ToLocal(), FileName);
				}

				DConOut("    Symbol Files\n");
				for (auto &Time : SymbolFileNames)
				{
					auto &FileName = SymbolFileNames.fs_GetKey(Time);
					DConOut("        {} - {}/{}\n", Time.f_ToLocal(), FileName.m_BasePath, FileName.m_FileName);
				}
			}
		}
		else if (Action == "Copy")
		{
			CDatabase Database;
			Database.f_Read(DatabaseFileName);

			CStr DestinationDirectory = Params["DestinationDirectory"];

			for (auto iRange = Database.m_SourceTimes.f_GetIterator(); iRange; ++iRange)
			{
				CTime const &StartTime = iRange->f_Start();
				CTime const &EndTime = iRange->f_End();

				DConOut("{} -> {}\n", StartTime.f_ToLocal(), EndTime.f_ToLocal());

				TCSet<CDatabase::CSymbolFile> SymbolFileNames;
				for (auto iFiles = Database.m_SymbolFilesByTime.f_GetIterator_SmallestGreaterThanEqual(StartTime); iFiles && iFiles.f_GetKey() <= EndTime; ++iFiles)
				{
					for (auto &FileName : *iFiles)
						SymbolFileNames[FileName];
				}

				for (auto &FileName : SymbolFileNames)
				{
					CStr DestinationFile = CFile::fs_AppendPath(DestinationDirectory, FileName.m_FileName);
					if (CFile::fs_FileExists(DestinationFile))
						continue;

					try
					{
						CStr SourceFile = CFile::fs_AppendPath(FileName.m_BasePath, FileName.m_FileName);
						CStr DestPath = CFile::fs_GetPath(DestinationFile);
						CStr TempFileName = DestPath + "/" + fg_FastRandomID();
						CFile::fs_CreateDirectory(DestPath);
						CFile::fs_CopyFile(SourceFile, TempFileName);
						CFile::fs_RenameFile(TempFileName, DestinationFile);

						DConOut("    {} -> {} = {}\n", FileName.m_BasePath, DestinationDirectory, FileName.m_FileName);
					}
					catch (CException const &_Exception)
					{
						DConOut("ERR {} -> {} = {}\n        {}\n", FileName.m_BasePath, DestinationDirectory, FileName.m_FileName, _Exception);
					}
				}
			}
		}
		else if (Action == "Search")
		{
			CDatabase Database;
			Database.f_Read(DatabaseFileName);

			if (_Files.f_GetLen() != 1)
				DError("Required one search term");

			CStr const &SearchTerm = _Files[0];

			for (auto &Files : Database.m_SymbolFilesByTime)
			{
				auto &Time = Database.m_SymbolFilesByTime.fs_GetKey(Files);

				for (auto &File : Files)
				{
					if (File.m_FileName.f_Find(SearchTerm) >= 0)
						DConOut("{}/{}: {}\n", File.m_BasePath, File.m_FileName, Time);
				}
			}
		}
		else
			DError("Unknown action: {}"_f << Action);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_SymbolExtractor);
