// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Cryptography/UUID>

class CTool_Touch : public CTool
{
public:

	void fr_Touch(CStr _Path, CStr _Pattern, const NTime::CTime &_Time, bool _bRecursive)
	{
		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(_Path + "/" + _Pattern, EFileAttrib_File, _bRecursive);
		for (mint i = 0; i < Files.f_GetLen(); ++i)
		{
			try
			{
				CStr File = Files[i];
				DConOut("Touching: {}" DNewLine, File);
				NFile::CFile Temp;
				Temp.f_Open(File, EFileOpen_ShareAll | EFileOpen_WriteAttribs | EFileOpen_ReadAttribs);
				Temp.f_SetWriteTime(_Time);
			}
			catch(NFile::CExceptionFile)
			{
			}
		}
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Pattern = _Params.f_GetValue("0", "NotExist");
		CStr Dir = NFile::CFile::fs_GetPath(Pattern);
		Pattern = NFile::CFile::fs_GetFile(Pattern);
		bool bRecursive = false;
		if (_Params.f_GetValue("1", "NotExist").f_CmpNoCase("-R") == 0)
			bRecursive = true;

		NTime::CTime Now = NTime::CTime::fs_NowUTC();

		fr_Touch(Dir, Pattern, Now, bRecursive);
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_Touch);

class CTool_TouchOrCreate : public CTool
{
	public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr File = _Params.f_GetValue("0", "NotExist");

		NTime::CTime Now = NTime::CTime::fs_NowUTC();

		CFile::fs_CreateDirectory(CFile::fs_GetPath(File));
		NFile::CFile Temp;
		Temp.f_Open(File, EFileOpen_Read | EFileOpen_Write | EFileOpen_ShareAll | EFileOpen_DontTruncate);
		Temp.f_SetWriteTime(Now);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_TouchOrCreate);

class CTool_CopyWriteTime : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr SourceFile = CFile::fs_GetExpandedPath(_Params.f_GetValue("0", "NotExist"));
		CStr DestFile = CFile::fs_GetExpandedPath(_Params.f_GetValue("1", "NotExist"));

		try
		{
			auto SourceTime = NFile::CFile::fs_GetWriteTime(SourceFile);

			if (NFile::CFile::fs_FileExists(DestFile))
			{
				auto DestTime = NFile::CFile::fs_GetWriteTime(DestFile);
				if (DestTime != SourceTime)
				{
					DConErrOut("Copy write time ({} != {}): {} -> {}{\n}", SourceTime << DestTime << SourceFile << DestFile);
					CFile::fs_SetWriteTime(DestFile, SourceTime);
				}
			}
			else
			{
				NFile::CFile TempDst;
				TempDst.f_Open(DestFile, EFileOpen_ShareAll | EFileOpen_Write | EFileOpen_Read | EFileOpen_WriteAttribs | EFileOpen_ReadAttribs | EFileOpen_DontTruncate);
				DConErrOut("Copy new write time: {} -> {}{\n}", SourceFile << DestFile);
				TempDst.f_SetWriteTime(SourceTime);
			}
		}
		catch (NFile::CExceptionFile const &_Error)
		{
			DConErrOut2("Copy new write failed: {}: {}{\n}", DestFile, _Error);
		}
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_CopyWriteTime);

class CTool_CopyWriteTimeIfNewer : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr SourceFile = CFile::fs_GetExpandedPath(_Params.f_GetValue("0", "NotExist"));
		CStr DestFile = CFile::fs_GetExpandedPath(_Params.f_GetValue("1", "NotExist"));

		try
		{
			auto SourceTime = NFile::CFile::fs_GetWriteTime(SourceFile);

			if (NFile::CFile::fs_FileExists(DestFile))
			{
				auto DestTime = NFile::CFile::fs_GetWriteTime(DestFile);
				if (DestTime < SourceTime)
				{
					DConErrOut("Copy write time ({} != {}): {} -> {}{\n}", SourceTime << DestTime << SourceFile << DestFile);
					CFile::fs_SetWriteTime(DestFile, SourceTime);
				}
			}
			else
			{
				NFile::CFile TempDst;
				TempDst.f_Open(DestFile, EFileOpen_ShareAll | EFileOpen_Write | EFileOpen_Read | EFileOpen_WriteAttribs | EFileOpen_ReadAttribs | EFileOpen_DontTruncate);
				DConErrOut("Copy new write time: {} -> {}{\n}", SourceFile << DestFile);
				TempDst.f_SetWriteTime(SourceTime);
			}
		}
		catch (NFile::CExceptionFile const &_Error)
		{
			DConErrOut2("Copy new write failed: {}: {}{\n}", DestFile, _Error);
		}
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_CopyWriteTimeIfNewer);

void fg_LogVerbose(CFile::EDiffCopyChange _Change, CStr const &_Source, CStr const &_Destination, CStr const &_Link)
{
	switch (_Change)
	{
	case CFile::EDiffCopyChange_DirectoryDeleted:
		DConOut("Directory deleted: {}" DNewLine, _Destination);
		break;
	case CFile::EDiffCopyChange_DirectoryCreated:
		DConOut("Directory created: {} -> {}" DNewLine, _Source << _Destination);
		break;
	case CFile::EDiffCopyChange_FileDeleted:
		DConOut("File deleted: {}" DNewLine, _Destination);
		break;
	case CFile::EDiffCopyChange_FileCreated:
		DConOut("File created: {} -> {}" DNewLine, _Source << _Destination);
		break;
	case CFile::EDiffCopyChange_FileChanged:
		DConOut("File changed: {} -> {}" DNewLine, _Source << _Destination);
		break;
	case CFile::EDiffCopyChange_LinkDeleted:
		DConOut("Link deleted: {}" DNewLine, _Destination);
		break;
	case CFile::EDiffCopyChange_LinkCreated:
		DConOut("Link created: {} -> {} = {}" DNewLine, _Source << _Destination << _Link);
		break;
	case CFile::EDiffCopyChange_NoChange:
		break;
	}
}

class CTool_DiffCopy : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		DScopeConOutTimer("DiffCopy");
		CStr SourcePattern = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("0", "NotExist"));
        CStr SourceDir = fg_ForceStrUTF8(NFile::CFile::fs_GetPath(SourcePattern));
		CStr DestPath = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("1", "NotExist"));
		CStr Touch = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("2", ""));
        bool bRecursive = _Params.f_GetValue("3", "1").f_ToInt() != 0;
		bool bQuiet = _Params.f_GetValue("4", "0").f_ToInt() != 0;
		bool bDirectory = _Params.f_GetValue("5", "0").f_ToInt() != 0;
		bool bDestinationIsFile = _Params.f_GetValue("6", "0").f_ToInt() != 0;
		bool bCopied = false;
		bool bVerbose = false;

        if (NFile::CFile::fs_FileExists(SourcePattern, EFileAttrib_Directory))
        {
			CStr FullDestPath;
			if (bDestinationIsFile)
				FullDestPath = DestPath;
			else
				FullDestPath = CFile::fs_AppendPath(DestPath, CFile::fs_GetFile(SourcePattern));

			if
				(
					CFile::fs_DiffCopyFileOrDirectory
					(
						SourcePattern
						, FullDestPath
						, [&](CFile::EDiffCopyChange _Change, CStr const &_Source, CStr const &_Destination, CStr const &_Link)
						{
							if (bVerbose)
								fg_LogVerbose(_Change, _Source, _Destination, _Link);
							return CFile::EDiffCopyChangeAction_Perform;
						}
					)
				)
			{
				if (!bQuiet)
					DConOut("{} -> {}" DNewLine, SourcePattern << FullDestPath);
				bCopied = true;
			}
			return 0;
        }

		EFileAttrib Attribs = EFileAttrib_File | EFileAttrib_Link;
		if (bDirectory)
			Attribs = EFileAttrib_Directory;

		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(SourcePattern, Attribs, bRecursive, false);

		mint nFiles = Files.f_GetLen();
		for (mint i = 0; i < nFiles; ++i)
		{
			CStr FilePath = fg_ForceStrUTF8(Files[i]);
			CStr RelativePath = FilePath.f_Extract(SourceDir.f_GetLen() + 1);
			CStr FileDest;
			if (bDestinationIsFile)
				 FileDest = DestPath;
			else
				 FileDest = NFile::CFile::fs_AppendPath(DestPath, RelativePath);

			if
				(
					CFile::fs_DiffCopyFileOrDirectory
					(
						FilePath
						, FileDest
						, [&](CFile::EDiffCopyChange _Change, CStr const &_Source, CStr const &_Destination, CStr const &_Link)
						{
							if (bVerbose)
								fg_LogVerbose(_Change, _Source, _Destination, _Link);
							return CFile::EDiffCopyChangeAction_Perform;
						}
					)
				)
			{
				if (!bQuiet)
					DConOut("{} -> {}" DNewLine, FilePath << FileDest);
				bCopied = true;
			}
		}

		if (bCopied && !Touch.f_IsEmpty())
		{
			NFile::CFile Temp;
			NFile::CFile::fs_CreateDirectory(CFile::fs_GetPath(Touch));
			EFileOpen OpenFlags = EFileOpen_ShareAll | EFileOpen_Write | EFileOpen_DontTruncate;
			if (CFile::fs_FileExists(Touch))
				OpenFlags = EFileOpen_ShareAll | EFileOpen_WriteAttribs | EFileOpen_ReadAttribs;
			Temp.f_Open(Touch, OpenFlags);
			Temp.f_SetWriteTime(NTime::CTime::fs_NowUTC());
		}

		if (nFiles == 0)
			DError(CStr::CFormat("No files found for pattern: {}") << SourcePattern);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DiffCopy);

class CTool_DiffReplace : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		DScopeConOutTimer("DiffReplace");
		CStr Search = _Params.f_GetValue("0", "NotExist");
		CStr Replace = _Params.f_GetValue("1", "NotExist");
		CStr SourcePattern = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("2", "NotExist"));
		CStr DestPath = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("3", "NotExist"));
		CStr Touch = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("4", ""));
		bool bRecursive = _Params.f_GetValue("5", "0").f_ToInt() != 0;
		bool bAddBom = _Params.f_GetValue("6", "0").f_ToInt() != 0;
		bool bQuiet = _Params.f_GetValue("7", "0").f_ToInt() != 0;
		CStr SourceDir = fg_ForceStrUTF8(NFile::CFile::fs_GetPath(SourcePattern));

		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(SourcePattern, EFileAttrib_File, bRecursive);

		mint nFiles = Files.f_GetLen();
		bool bCopied = false;
		for (mint i = 0; i < nFiles; ++i)
		{
			CStr FilePath = fg_ForceStrUTF8(Files[i]);
			CStr RelativePath = FilePath.f_Extract(SourceDir.f_GetLen() + 1);
			CStr FileDest = NFile::CFile::fs_AppendPath(DestPath, RelativePath);
			int32 nTimes = 20 * 30; // 30 seconds
			while (1)
			{
				try
				{
					NFile::CFile::fs_CreateDirectory(NFile::CFile::fs_GetPath(FileDest));
					break;
				}
				catch (NException::CException)
				{
					if (--nTimes == 0)
						throw;
					NSys::fg_Thread_Sleep(NMisc::fg_GetRandomFloat() * 0.100);
				}
			}

			CStr SourceData = NFile::CFile::fs_ReadStringFromFile(CStr(FilePath));
			CStr DestData = SourceData.f_Replace(Search, Replace);
			CByteVector Data;
			NFile::CFile::fs_WriteStringToVector(Data, DestData, bAddBom);
			if (!NFile::CFile::fs_IsFileWritable(FileDest))
				NFile::CFile::fs_MakeFileWritable(FileDest);

			if (NFile::CFile::fs_CopyFileDiff(Data, FileDest, NTime::CTime::fs_NowUTC()))
			{
				if (!bQuiet)
					DConOut("{} -> {}" DNewLine, FilePath << FileDest);
				bCopied = true;
			}
		}

		if (bCopied && !Touch.f_IsEmpty())
		{
			NFile::CFile Temp;
			NFile::CFile::fs_CreateDirectory(CFile::fs_GetPath(Touch));
			EFileOpen OpenFlags = EFileOpen_ShareAll | EFileOpen_Write | EFileOpen_DontTruncate;
			if (CFile::fs_FileExists(Touch))
				OpenFlags = EFileOpen_ShareAll | EFileOpen_WriteAttribs | EFileOpen_ReadAttribs;
			Temp.f_Open(Touch, OpenFlags);
			Temp.f_SetWriteTime(NTime::CTime::fs_NowUTC());
		}

		if (nFiles == 0)
		{
			DError(CStr::CFormat("No files found for pattern: {}") << SourcePattern);
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DiffReplace);

class CTool_DiffTouchCopy : public CTool_DiffCopy
{
public:
};

DMibRuntimeClass(CTool, CTool_DiffTouchCopy);

class CTool_DeleteDirectoryRecursive : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		DScopeConOutTimer("DeleteDirectoryRecursive");
		CStr SourcePattern = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("0", "NotExist"));
		bool bRemoveWriteProtection = _Params.f_GetValue("1", "1").f_ToInt() != 0;

        if (!CFile::fs_FileExists(SourcePattern, EFileAttrib_Directory))
			DError(CStr::CFormat("Directory '{}' does not exist") << SourcePattern);

		CFile::fs_DeleteDirectoryRecursive(SourcePattern, bRemoveWriteProtection);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DeleteDirectoryRecursive);

class CTool_DeleteRecursive : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		DScopeConOutTimer("DeleteRecursive");
		CStr SourcePattern = NFile::CFile::fs_GetExpandedPath(_Params.f_GetValue("0", "NotExist"));

		CStr Pattern = CFile::fs_GetFile(SourcePattern);
		TCVector<CFile::CFoundFile> Files = NFile::CFile::fs_FindFilesEx(SourcePattern, EFileAttrib_File | EFileAttrib_Directory, true, false);

		for (auto iFile = Files.f_GetIterator(); iFile; ++iFile)
		{
			if (CFile::fs_FileExists(iFile->m_Path) && fg_StrMatchWildcard(CFile::fs_GetFile(CStr(iFile->m_Path)).f_GetStr(), Pattern.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted)
			{
				DConOut("Deleted: '{}'" DNewLine, iFile->m_Path);
				CFile::fs_DeleteDirectoryRecursive(iFile->m_Path, true);
			}
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DeleteRecursive);

class CTool_TestOutput : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Test = NMib::NCryptography::fg_GetRandomUuidString();
		DConOut(str_utf16("*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯{\n}"), 0);
		for (int i = 0; i < 16; ++i)
		{
			NMib::NSys::fg_Thread_Sleep(0.001f);
			DConOut("Out: {}{\n}", Test);
			DConErrOut("Err: {}{\n}", Test);
		}


		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_TestOutput);

