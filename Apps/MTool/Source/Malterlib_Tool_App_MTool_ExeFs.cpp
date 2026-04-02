// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/File/MalterlibFS>

class CTool_ExeFS : public CTool
{
public:

	enum ETarget
	{
		ETarget_File,		// Operates on a vfs file
		ETarget_Exe,		// Operates on a exe file
	};

	enum EAction
	{
		EAction_Add,			// Adds files to an existing VFS
		EAction_Create,			// Adds files to a new VFS
		EAction_Extract,	// Extracts files
		EAction_List,			// Lists files
	};

	aint f_Run(NContainer::CRegistry &_Params)
	{

		static char const* sc_pHelpText =
			"Usage:" DNewLine
			"MTool <Flags> TargetFile [ SourcePattern [DestPath] ]" DNewLine
			DNewLine
			"Where <Flags> is made up of:" DNewLine
			"	Either:" DNewLine
			"		-exe		- TargetFile specifies an exe" DNewLine
			"	or" DNewLine
			"		-file		- TargetFile specified a file name" DNewLine
			"and" DNewLine
			"	-add			- Files matching SourcePattern are added to target in DestPath." DNewLine
			"or" DNewLine
			"	-create			- Files matching SourcePattern are added to target in DestPath. The target is created." DNewLine
			"or" DNewLine
			"	-extract		- Files in TargetFile matching SourcePattern are extracted" DNewLine
			"or" DNewLine
			"	-list			- Files in TargetFile are listed." DNewLine;

		bool bVerbose = false;
		ETarget TargetType = ETarget_Exe;
		EAction Action = EAction_Add;

		auto f_IntToStr = [](int _Val) -> CStr
		{
			return CStr::CFormat("{}") << _Val;
		};

		int iCurArg = 0;
		CStr CurArg;
		for (;(CurArg = _Params.f_GetValue(f_IntToStr(iCurArg), "")); ++iCurArg)
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
				else if (CurArg.f_CmpNoCase("-file") == 0)
					TargetType = ETarget_File;
				else if (CurArg.f_CmpNoCase("-exe") == 0)
					TargetType = ETarget_Exe;
				else if (CurArg.f_CmpNoCase("-extract") == 0)
					Action = EAction_Extract;
				else if (CurArg.f_CmpNoCase("-list") == 0)
					Action = EAction_List;
				else if (CurArg.f_CmpNoCase("-add") == 0)
					Action = EAction_Add;
				else if (CurArg.f_CmpNoCase("-create") == 0)
					Action = EAction_Create;
				else
				{
					DConOut("ExeFS: Unrecognised flag: {}" DNewLine, CurArg);
					return -1;
				}
			}
			else
				break;
		}

		CStr TargetFilename = _Params.f_GetValue(f_IntToStr(iCurArg), "");
		++iCurArg;

		CStr SourcePattern = _Params.f_GetValue(f_IntToStr(iCurArg), "");
		++iCurArg;

		CStr DestPath = _Params.f_GetValue(f_IntToStr(iCurArg), "");
		++iCurArg;

		TargetFilename = TargetFilename.f_ReplaceChar('\\', '/');
		SourcePattern = SourcePattern.f_ReplaceChar('\\', '/');

		bool bTargetExists = NFile::CFile::fs_FileExists(TargetFilename, EFileAttrib_File);

		if (TargetType == ETarget_Exe && !bTargetExists)
		{
			DError("File does not exist");
		}

		NFile::TCBinaryStreamFile<> TargetFile;
		EFileOpen FileMode = EFileOpen_Read | EFileOpen_ShareAll;

		switch(Action)
		{
			case EAction_Add:
				FileMode |= EFileOpen_Write;
				FileMode |= EFileOpen_DontTruncate;
				break;
			case EAction_Create:
				FileMode |= EFileOpen_Write;
				break;
			case EAction_Extract:
			case EAction_List:
			break;
		}

		TargetFile.f_Open(TargetFilename, FileMode);

		bool bExistingFS = false;

		NStream::CBinaryStreamSubStream<> SubStream;
		NMib::NFile::CVirtualFS VirtualFS;

		const ch8 *pSigReversed = "SFEXESDI";
		CStr Sig = CStr(pSigReversed).f_Reverse();
		const ch8 *pSig = Sig;

		int32 const GrowSize = 64*1024;
		int32 const ClusterSize = 1024;

		int64 MalterlibFsFilePos;

		if (TargetType == ETarget_Exe)
		{
			CMibFilePos FileSize = TargetFile.f_GetLength() -(sizeof(int64) + 8);

			TargetFile.f_SetPosition(FileSize);

			MalterlibFsFilePos = fg_AlignUp(FileSize, ClusterSize);

			{
				int32 nClustersToSearch = 1024;
				CMibFilePos FileSize = fg_AlignDown(TargetFile.f_GetLength() -(sizeof(int64) + 8), ClusterSize);

				const umint SigSize = 8;
				ch8 Signature[SigSize+1];
				Signature[SigSize] = 0;

				while (nClustersToSearch && FileSize > 0)
				{
					--nClustersToSearch;
					TargetFile.f_SetPosition(FileSize);

					TargetFile.f_ConsumeBytes(Signature, SigSize);

					if (fg_StrCmp(Signature, pSig) == 0)
					{
						// We have an existing file system
						TargetFile >> MalterlibFsFilePos;
						SubStream.f_Open(&TargetFile, MalterlibFsFilePos);
						VirtualFS.f_Open(&SubStream);
						bExistingFS = true;
						break;
					}
					else
					{
						FileSize -= ClusterSize;
					}
				}
			}

			if (!bExistingFS)
			{
				// No file system lets create one
				TargetFile.f_SetPositionFromEnd(0);
				umint ToWrite = MalterlibFsFilePos - TargetFile.f_GetPosition();
				while (ToWrite)
				{
					TargetFile << uint8(0);
					--ToWrite;
				}
				SubStream.f_Open(&TargetFile, MalterlibFsFilePos);
				VirtualFS.f_Create(&SubStream, ClusterSize, GrowSize, GrowSize);
			}
		}
		else if (TargetType == ETarget_File)
		{
			bExistingFS = bTargetExists;

			if (	(bExistingFS && Action == EAction_Add)
				||	Action == EAction_List
				||	Action == EAction_Extract)
				VirtualFS.f_Open(&TargetFile);
			else
				VirtualFS.f_Create(&TargetFile, ClusterSize, GrowSize, GrowSize);
		}

		if (Action == EAction_Add || Action == EAction_Create)
		{
			TCVector<CStr> lSourceFiles = NFile::CFile::fs_FindFiles(SourcePattern, EFileAttrib_File, true);

			CStr BasePath = NFile::CFile::fs_GetPath(SourcePattern);
			{
				TCVector<CStr> BaseFiles = NFile::CFile::fs_FindFiles(BasePath, EFileAttrib_Directory, false);
				if (BaseFiles.f_GetLen())
					BasePath = BaseFiles[0];
			}

			umint Len = BasePath.f_GetLen();

			umint nFileAdded = 0;
			for (umint i = 0; i < lSourceFiles.f_GetLen(); ++i)
			{
				CStr SourceName = lSourceFiles[i].f_Extract(Len+1);
				if (SourceName[0] == '.')
					continue;

				CStr AddPath = NFile::CFile::fs_AppendPath<CStr>(DestPath, SourceName);

				if (bVerbose)
					DConOut("Adding '{}' as '{}'" DNewLine, lSourceFiles[i], AddPath);

				++nFileAdded;

				CStr Dir = NFile::CFile::fs_GetPath(AddPath);
				if (!Dir.f_IsEmpty())
					VirtualFS.f_CreateDirectory(Dir);

				VirtualFS.f_CopyFileToFS(lSourceFiles[i], AddPath);
			}
			if (bExistingFS)
				DConOut("{} files added to existing ExeFS" DNewLine, nFileAdded);
			else
				DConOut("{} files added to new ExeFS" DNewLine, nFileAdded);

			VirtualFS.f_Close();

			if (TargetType == ETarget_Exe)
			{
				const umint SigSize = 8;
				TargetFile.f_SetPositionFromEnd(0);
				TargetFile.f_FeedBytes(pSig, SigSize);
				TargetFile << MalterlibFsFilePos;
			}
		}
		else if (Action == EAction_List)
		{
			TCVector<CStr> Files = VirtualFS.f_FindFiles(SourcePattern, EFileAttrib_File, true);
			CStr BasePath = NFile::CFile::fs_GetPath(SourcePattern);
			umint Len = BasePath.f_GetLen();

			for (umint i = 0; i < Files.f_GetLen(); ++i)
			{
				CStr SourceName;
				if (Len == 0)
					SourceName = Files[i];
				else
					SourceName = Files[i].f_Extract(Len+1);

				DConOut("\t{}" DNewLine, SourceName);
			}
			// Close
			VirtualFS.f_Close();
		}
		else if (Action == EAction_Extract)
		{
			TCVector<CStr> Files = VirtualFS.f_FindFiles(SourcePattern, EFileAttrib_File, true);
			CStr BasePath = NFile::CFile::fs_GetPath(SourcePattern);
			umint Len = BasePath.f_GetLen();

			for (umint i = 0; i < Files.f_GetLen(); ++i)
			{
				CStr SourceName;
				if (Len == 0)
					SourceName = Files[i];
				else
					SourceName = Files[i].f_Extract(Len+1);

				CStr AddPath = NFile::CFile::fs_AppendPath<CStr>(DestPath, SourceName);

				DConOut("File '{}' Extracted as '{}'" DNewLine, Files[i], AddPath);

				CStr Dir = NFile::CFile::fs_GetPath(AddPath);
				if (!Dir.f_IsEmpty())
					NFile::CFile::fs_CreateDirectory(Dir);
				VirtualFS.f_CopyFileFromFS(Files[i], AddPath);
			}
			// Close
			VirtualFS.f_Close();
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_ExeFS);
