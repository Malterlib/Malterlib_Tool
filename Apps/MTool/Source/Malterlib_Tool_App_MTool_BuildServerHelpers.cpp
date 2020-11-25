// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Cryptography/UUID>


namespace
{
	struct CBuildServerIPC
	{
		CRegistry f_RunCommand(CStr const &_Command, CStr const &_Params, TCVector<CStr> const &_ParamsList)
		{
			CUniversallyUniqueIdentifier UniqueID(EUniversallyUniqueIdentifierGenerate_Random);
			CStr Prefix = UniqueID.f_GetAsString(EUniversallyUniqueIdentifierFormat_AlphaNum);
			CStr TempDir = fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildServerTemp");
			if (TempDir.f_IsEmpty())
				TempDir = fg_GetSys()->f_GetEnvironmentVariable("IdsBuildServerTemp");
			if (TempDir.f_IsEmpty())
				TempDir = CFile::fs_GetTemporaryDirectory() + "/Slave";
			TempDir += "/IPC";

			uint64 LaunchID = fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildServerLaunchID").f_ToInt(uint64(0));
			if (LaunchID == 0)
				LaunchID = fg_GetSys()->f_GetEnvironmentVariable("IdsBuildServerLaunchID").f_ToInt(uint64(0));

			CFile::fs_CreateDirectory(TempDir);
			CStr TempFile = CFile::fs_AppendPath(TempDir, Prefix + ".ipc");
			CStr InputFile = CFile::fs_AppendPath(TempDir, Prefix + ".ipc.input");
			CStr InputTempFile = CFile::fs_AppendPath(TempDir, Prefix + ".ipc.tmpinput");
			CStr OutputFile = CFile::fs_AppendPath(TempDir, Prefix + ".ipc.output");

			CStr FileData;
			fg_AppendFormat(FileData, "{}\r\n", LaunchID);

			fg_AppendFormat(FileData, "{}-Start-{};{};{}\r\n", Prefix, TempFile, _Command, _Params);
			for (auto iFile = _ParamsList.f_GetIterator(); iFile; ++iFile)
				fg_AppendFormat(FileData, "{}-Param-{}\r\n", Prefix, *iFile);
			fg_AppendFormat(FileData, "{}-End\r\n", Prefix);

			CFile::fs_WriteStringToFile(CStr(InputTempFile), FileData);

			// Create sync file
			{
				CFile File;
				File.f_Open(TempFile, EFileOpen_Write | EFileOpen_ShareAll);
			}
//			DConOut("Starting IPC: {}\n", TempFile);

			// Start IPC
			CFile::fs_RenameFile(InputTempFile, InputFile);

			// Wait for reply
			NMib::NThread::CSemaphore ChangeSemaphore;
			CFileChangeNotification Notification;
			Notification.f_Open(TempDir, EFileChange_All, &ChangeSemaphore);

			while (CFile::fs_FileExists(TempFile))
				ChangeSemaphore.f_WaitTimeout(1.0);

			CStr Output = CFile::fs_ReadStringFromFile(CStr(OutputFile));
			CFile::fs_DeleteFile(OutputFile);

			CRegistry Registry;
			Registry.f_ParseStr(Output, OutputFile);

			return Registry;
		}
		void f_HandleResults(CRegistry const &_Registry)
		{
			CStr Error;
			for (auto iError = _Registry.f_GetChildIterator("Error"); iError && iError->f_GetName() == "Error"; ++iError)
			{
				fg_AddStrSep(Error, "error: " + iError->f_GetThisValue(), "\r\n");
			}
			if (!Error.f_IsEmpty())
				DError(Error);

		}
		struct CRemoteFile
		{
			CStr m_LocalFile;
			TCVector<CStr> m_RemoteFiles;
		};
		void f_PutFiles(TCVector<CRemoteFile> const &_Files)
		{
			TCVector<CStr> Params;
			for (auto iFile = _Files.f_GetIterator(); iFile; ++iFile)
			{
				CStr Param = iFile->m_LocalFile;
				for (auto iRemoteFile = iFile->m_RemoteFiles.f_GetIterator(); iRemoteFile; ++iRemoteFile)
				{
					Param += ";";
					Param += *iRemoteFile;
				}
				Params.f_Insert(Param);
			}
			auto Results = f_RunCommand("BuildServerPut", CStr(), Params);
			f_HandleResults(Results);
		}
		TCMap<CStr, CStr> f_GetFiles(CStr const &_Source, CStr const &_DestinationDir, bool _bRecursive)
		{
			TCVector<CStr> Params;
			Params.f_Insert(_Source);
			Params.f_Insert(_DestinationDir);
			Params.f_Insert(CStr::CFormat("{}") << (int32)_bRecursive);

			auto Results = f_RunCommand("BuildServerGet", CStr(), Params);
			f_HandleResults(Results);

			TCMap<CStr, CStr> FilesCopied;

			for (auto iFile = Results.f_GetChildIterator("File"); iFile && iFile->f_GetName() == "File"; ++iFile)
				FilesCopied[iFile->f_GetThisValue()] = iFile->f_GetValue("To");

			return FilesCopied;
		}
		void f_DeleteFiles(TCVector<CStr> const &_Files)
		{
			TCVector<CStr> Params;
			Params.f_Insert(_Files);

			auto Results = f_RunCommand("BuildServerDelete", CStr(), Params);
			f_HandleResults(Results);
		}
		CRegistry f_RunTool(CStr const &_Command, TCVector<CStr> const &_Params)
		{
			return f_RunCommand("RunTool", _Command, _Params);
		}
	};
}

void fg_LogVerbose(CFile::EDiffCopyChange _Change, CStr const &_Source, CStr const &_Destination, CStr const &_Link);

class CTool_BuildServerPut : public CTool2
{
public:

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{
		if (fg_GetSys()->f_GetEnvironmentVariable("BUILDSERVER") == "")
			DError("You are not running under a build server slave, so BuildServerPut is not supported");

		if (!_Files.f_IsEmpty())
			DError("Free arguments not allowed (Use X=Y form)");

		if (!_Params.f_FindEqual("Source"))
		{
			DError("Source not specified");
		}

		if (!_Params.f_FindEqual("DestinationDir"))
		{
			DError("DestinationDir not specified");
		}

		CStr Source = CFile::fs_GetExpandedPath(_Params["Source"], true);
		CStr DestinationDir = _Params["DestinationDir"];
		bool bRecursive = true;
		if (_Params.f_FindEqual("Recursive"))
			bRecursive = _Params["Recursive"].f_ToInt(1) != 0;

		CStr ExtraDir;
        if (NFile::CFile::fs_FileExists(Source, EFileAttrib_Directory))
        {
			ExtraDir = CFile::fs_GetFile(Source);
            bRecursive = true;
            Source += "/*";
        }
		CStr SourceDir = CFile::fs_GetPath(Source);

		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(Source, EFileAttrib_File | EFileAttrib_Link, bRecursive, false);

		if (Files.f_IsEmpty())
		{
			DError(CStr::CFormat("No files found for source: {}") << Source);
		}

		TCVector<CBuildServerIPC::CRemoteFile> RemoteFiles;

		for (auto iFile = Files.f_GetIterator(); iFile; ++iFile)
		{
			auto &RemoteFile = RemoteFiles.f_Insert();
			RemoteFile.m_LocalFile = *iFile;
			CStr DestinationDirs = DestinationDir;
			CStr FileName = iFile->f_Extract(SourceDir.f_GetLen() + 1);
			while (!DestinationDirs.f_IsEmpty())
			{
				CStr Dir = fg_GetStrSep(DestinationDirs, ";");
				CStr OutputDir;
				if (ExtraDir.f_IsEmpty())
					OutputDir = CFile::fs_AppendPath(Dir, FileName);
				else
					OutputDir = CFile::fs_AppendPath(Dir + "/" + ExtraDir, FileName);

				RemoteFile.m_RemoteFiles.f_Insert(OutputDir);

				DConOut("Put on build server store: {} -> {}" DNewLine, *iFile << OutputDir);
			}

		}

		CBuildServerIPC BuildServer;

		BuildServer.f_PutFiles(RemoteFiles);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_BuildServerPut);

class CTool_BuildServerGet : public CTool2
{
public:

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{
		if (fg_GetSys()->f_GetEnvironmentVariable("BUILDSERVER") == "")
			DError("You are not running under a build server slave, so BuildServerGet is not supported");

		if (!_Files.f_IsEmpty())
			DError("Free arguments not allowed (Use X=Y form)");

		if (!_Params.f_FindEqual("Source"))
		{
			DError("Source not specified");
		}

		if (!_Params.f_FindEqual("DestinationDir"))
		{
			DError("DestinationDir not specified");
		}

		CStr Source = _Params["Source"];
		CStr DestinationDir = CFile::fs_GetExpandedPath(_Params["DestinationDir"], true);
		bool bRecursive = true;
		if (_Params.f_FindEqual("Recursive"))
			bRecursive = _Params["Recursive"].f_ToInt(1) != 0;

		CBuildServerIPC BuildServer;

		TCMap<CStr, CStr> FilesCopied = BuildServer.f_GetFiles(Source, DestinationDir, bRecursive);

		for (auto iFile = FilesCopied.f_GetIterator(); iFile; ++iFile)
		{
			DConOut("Get from build server store: {} -> {}" DNewLine, iFile.f_GetKey() << *iFile);
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_BuildServerGet);

class CTool_BuildServerTool : public CTool2
{
public:
	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{
		if (!_Params.f_FindEqual("Tool"))
			DError("Tool not specified");

		if (fg_GetSys()->f_GetEnvironmentVariable("BUILDSERVER") == "")
			DError("You are not running under a build server slave, so BuildServerTool is not supported");

		CStr Tool = _Params["Tool"];
		TCVector<CStr> ProtectDirs;
		TCMap<CStr, CStr> ExtraUploads;
		if (_Params.f_FindEqual("ProtectedDirs"))
		{
			CStr ProtectDirsStr = _Params["ProtectedDirs"];
			while (!ProtectDirsStr.f_IsEmpty())
				ProtectDirs.f_Insert(CFile::fs_GetMalterlibPath(fg_GetStrSep(ProtectDirsStr, ";")));
		}
		if (_Params.f_FindEqual("ExtraUploads"))
		{
			CStr ExtraUploadsStr = _Params["ExtraUploads"];
			while (!ExtraUploadsStr.f_IsEmpty())
			{
				CStr Upload = fg_GetStrSep(ExtraUploadsStr, ";");
				CStr Source = fg_GetStrSep(Upload, "?");
				ExtraUploads[CFile::fs_GetMalterlibPath(Source)] = CFile::fs_GetMalterlibPath(Upload);
			}
		}
		bool bVerbose = false;
		if (_Params.f_FindEqual("Verbose"))
			bVerbose = _Params["Verbose"].f_ToInt(0) != 0;
		bool bQuiet = false;
		if (_Params.f_FindEqual("Quiet"))
			bQuiet = _Params["Quiet"].f_ToInt(0) != 0;
		CBuildServerIPC BuildServer;

		struct CUploaded
		{
			CStr m_Path;
			zbool m_bRecursive;
		};
		TCMap<CStr, CUploaded> UploadedFiles;
		TCVector<CStr> Params;
		TCVector<CStr> ExtraToUpload;
		TCVector<CStr> ToDelete;

		// Upload Files to build server
		{
			TCVector<CBuildServerIPC::CRemoteFile> RemoteFiles;
			CStr DestinationPath = "ToolTemp/";
			for (auto iParam = _Files.f_GetIterator(); iParam; ++iParam)
			{
				CStr Param = *iParam;

				if (CFile::fs_FileExists(Param))
				{
					CStr DestinationDir
						= DestinationPath
						+ CUniversallyUniqueIdentifier(EUniversallyUniqueIdentifierGenerate_Random).f_GetAsString(EUniversallyUniqueIdentifierFormat_AlphaNum)
					;
					ToDelete.f_Insert(DestinationDir);
					CStr OriginalParam = CFile::fs_GetExpandedPath(Param, true);
					CStr SearchForParam = OriginalParam;

					bool bRecursive = false;
					bool bDirectory = false;
					if (NFile::CFile::fs_FileExists(OriginalParam, EFileAttrib_Directory))
					{
						bDirectory = true;
						bool bProtected = false;
						for (auto iProtected = ProtectDirs.f_GetIterator(); iProtected; ++iProtected)
						{
							if (OriginalParam.f_StartsWithNoCase(*iProtected))
							{
								bProtected = true;
								break;
							}
						}
						if (bProtected)
						{
							//DConOut("Protected Param: {}" DNewLine, Param);
							Params.f_Insert(Param);

							continue;
						}
						bRecursive = true;
						SearchForParam += "/*";
					}
					CStr SourceDir = CFile::fs_GetPath(OriginalParam);

					TCVector<CStr> Files = NFile::CFile::fs_FindFiles(SearchForParam, EFileAttrib_File | EFileAttrib_Link, bRecursive, false);

					for (auto iFile = Files.f_GetIterator(); iFile; ++iFile)
					{
						auto &RemoteFile = RemoteFiles.f_Insert();
						RemoteFile.m_LocalFile = *iFile;
						CStr RemoteFileName = CFile::fs_AppendPath(DestinationDir, iFile->f_Extract(SourceDir.f_GetLen() + 1));
						RemoteFile.m_RemoteFiles.f_Insert(RemoteFileName);

						if (!bQuiet)
							DConOut("Put on build server store (Tool): {} -> {}" DNewLine, *iFile << RemoteFileName);

						{
							auto pExtraUpload = ExtraUploads.f_FindEqual(*iFile);
							if (pExtraUpload)
							{
								auto &ExtraRemoteFile = RemoteFiles.f_Insert();
								ExtraRemoteFile.m_LocalFile = *pExtraUpload;
								CStr ToUpload = CFile::fs_AppendPath(DestinationDir, CFile::fs_GetFile(*pExtraUpload));
								ExtraRemoteFile.m_RemoteFiles.f_Insert(ToUpload);
								ExtraToUpload.f_Insert(ToUpload);
								if (!bQuiet)
									DConOut("Put on build server store (Tool): {} -> {}" DNewLine, *pExtraUpload << ToUpload);
							}
						}
					}

					// Add directory
					if (bDirectory)
					{
						auto &RemoteFile = RemoteFiles.f_Insert();
						RemoteFile.m_LocalFile = OriginalParam;
						CStr RemoteFileName = CFile::fs_AppendPath(DestinationDir, OriginalParam.f_Extract(SourceDir.f_GetLen() + 1));
						RemoteFile.m_RemoteFiles.f_Insert(RemoteFileName);
					}

					Param = CFile::fs_AppendPath(DestinationDir, CFile::fs_GetFile(OriginalParam));
					auto &Uploaded = UploadedFiles[Param];
					Uploaded.m_Path = OriginalParam;
					Uploaded.m_bRecursive = bRecursive;
				}
				Params.f_Insert(Param);
			}

			if (!RemoteFiles.f_IsEmpty())
			{
/*				for (auto iFile = RemoteFiles.f_GetIterator(); iFile; ++iFile)
				{
					DConOut("Copying file to build server: {} -> {}" DNewLine, iFile->m_LocalFile << iFile->m_RemoteFile);
				}*/

				BuildServer.f_PutFiles(RemoteFiles);
			}
		}

		TCVector<CStr> AllParams = ExtraToUpload;
		AllParams.f_Insert(Params);

		CRegistry Results = BuildServer.f_RunTool(CStr::CFormat("{};{}") << ExtraToUpload.f_GetLen() << Tool, AllParams);
		CStr StdOut = Results.f_GetValue("StdOut", "");
		CStr StdErr = Results.f_GetValue("StdErr", "");
		uint32 ExitCode = Results.f_GetValue("ExitCode", "255").f_ToInt(uint32(255));

		if (!StdOut.f_IsEmpty())
			DConOut("{}", StdOut);
		if (!StdErr.f_IsEmpty())
			DConErrOut("{}", StdErr);

		for (auto iError = Results.f_GetChildIterator("Error"); iError && iError->f_GetName() == "Error"; ++iError)
		{
			DConErrOut("error: {}" DNewLine, iError->f_GetThisValue());
			if (ExitCode == 0)
				ExitCode = 1;
		}

		TCSet<CStr> ChangedFiles;
		for (auto iError = Results.f_GetChildIterator("ChangedFile"); iError && iError->f_GetName() == "ChangedFile"; ++iError)
			ChangedFiles[iError->f_GetThisValue()];

		if (ChangedFiles.f_IsEmpty() && !bQuiet)
			DConOut("No files were changed by tool" DNewLine, 0);

		// Download files from build server and copy back any changes
		{
			CStr TempDownloadDir = fg_GetSys()->f_GetEnvironmentVariable("MalterlibBuildServerTemp");
			if (TempDownloadDir.f_IsEmpty())
				TempDownloadDir = fg_GetSys()->f_GetEnvironmentVariable("IdsBuildServerTemp");
			if (TempDownloadDir.f_IsEmpty())
				TempDownloadDir = CFile::fs_GetTemporaryDirectory() + "/Slave";
			TempDownloadDir += "/Redownload";
			TCLinkedList<COnScopeExitShared> Cleanups;
			for (auto iFile = UploadedFiles.f_GetIterator(); iFile; ++iFile)
			{
				CStr FullPath = CFile::fs_AppendPath(TempDownloadDir, iFile.f_GetKey());
				if (!ChangedFiles.f_FindEqual(iFile.f_GetKey()))
					continue;
				CStr TempPath = CFile::fs_GetPath(FullPath);
				DConOut("Getting changed files from build server: {} -> {}" DNewLine, iFile.f_GetKey() << TempPath);

				TCMap<CStr, CStr> FilesCopied = BuildServer.f_GetFiles(iFile.f_GetKey(), TempPath, false);
				Cleanups.f_Insert
					(
						fg_OnScopeExitShared
						(
							[=]()
							{
								CFile::fs_DeleteDirectoryRecursive(TempPath, true);
							}
						)
					)
				;

				{
					for (auto iFile = FilesCopied.f_GetIterator(); iFile; ++iFile)
						DConOut("Get from build server store (Tool): {} -> {}" DNewLine, iFile.f_GetKey() << *iFile);
				}

				if
					(
						CFile::fs_DiffCopyFileOrDirectory
						(
							FullPath
							, iFile->m_Path
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
						DConOut("Copied back: {} -> {}" DNewLine, FullPath << iFile->m_Path);
				}
			}
		}
		// Delete files on build server store
		{
			BuildServer.f_DeleteFiles(ToDelete);
		}

		return ExitCode;
	}
};

DMibRuntimeClass(CTool, CTool_BuildServerTool);


class CTool_ChangeDirectory : public CTool2
{
public:
	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{

		CStr Directory = _Files[0];


		CUniversallyUniqueIdentifier UniqueID(EUniversallyUniqueIdentifierGenerate_Random);
		CStr FileName = UniqueID.f_GetAsString(EUniversallyUniqueIdentifierFormat_AlphaNum);

		DConOut("Adding file '{}' to directory: {}" DNewLine, FileName << Directory);

		CFile::fs_CreateDirectory(Directory + "/" + FileName);
		CFile File;
		File.f_Open(Directory + "/" + FileName + "/File", EFileOpen_Write | EFileOpen_ShareAll);

		CFile::fs_CreateDirectory(Directory + "/" + FileName + "/DirOnly");

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_ChangeDirectory);


class CTool_AppendFile : public CTool2
{
public:
	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{
		CStr FileName = _Files[0];

		DConOut("Appending file: {}" DNewLine, FileName);

		CFile File;
		File.f_Open(FileName, EFileOpen_Write | EFileOpen_DontTruncate | EFileOpen_ShareAll);

		File.f_SetPositionFromEnd(0);
		uint8 Test = 'a';
		File.f_Write(&Test, 1);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_AppendFile);

