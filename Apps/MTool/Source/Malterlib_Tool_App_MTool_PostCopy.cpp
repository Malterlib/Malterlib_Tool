// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystemPreprocessor>
#include <Mib/Perforce/Wrapper>
#include <Mib/Cryptography/MD5Cache>

class CTool_PostCopy : public CTool
{
public:
	void fr_DeleteEmptyDirs(CStr _Path)
	{
		TCVector<CStr> Files = NFile::CFile::fs_FindFiles(_Path + "/*", EFileAttrib_Directory, false);

		for (mint i = 0; i < Files.f_GetLen(); ++i)
		{
			fr_DeleteEmptyDirs(Files[i]);
		}

		Files = NFile::CFile::fs_FindFiles(_Path + "/*", EFileAttrib_Directory|EFileAttrib_File, false);
		if (Files.f_GetLen() == 0)
		{
			NFile::CFile::fs_DeleteDirectory(_Path);
			DConOut("Deleting empty dir: {}" DNewLine, _Path);
			return;
		}
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr ConfigFile;
		CStr SourceFile;
		CStr DestinationProject;
		CStr DestinationFolder;

		bool bVerbose = false;
		bool bHash = false;

		for (mint i = 0; true; ++i)
		{
			CStr Value = _Params.f_GetValue(CStr::fs_ToStr(i), "");
			if (Value.f_IsEmpty())
				break;
			if (Value.f_Find("=") < 0)
			{
				if (!SourceFile.f_IsEmpty())
					DError(CStr(CStr::CFormat("Source file already specified: {}") << SourceFile));
				SourceFile = CFile::fs_GetExpandedPath(Value);
				continue;
			}
			CStr Key = fg_GetStrSep(Value, "=");

			if (Key == "Project")
				DestinationProject = Value;
			else if (Key == "OutFolder")
				DestinationFolder = Value;
			else if (Key == "Config")
				ConfigFile = Value;
			else if (Key == "Verbose")
				bVerbose = Value.f_ToInt(0) != 0 || Value == "true";
			else if (Key == "Hash")
				bHash = Value.f_ToInt(0) != 0 || Value == "true";
			else
				DError(CStr(CStr::CFormat("Unknown setting: {}") << Key));
		}

		if (ConfigFile.f_IsEmpty())
			DError("No config file specified");

		if (SourceFile.f_IsEmpty())
			DError("No source file specified");

		if (DestinationProject.f_IsEmpty())
			DError("No destination project specified");

		CRegistryPreserveAll Registry;
		TCSet<CStr> SourceFiles;
		NBuildSystem::CFindCache FindCache;
		if (CFile::fs_FileExists(ConfigFile))
		{
			TCMap<CStr, CStr> Environment = fg_GetSys()->f_Environment();
			NBuildSystem::CBuildSystemPreprocessor Preprocessor(Registry, SourceFiles, FindCache, Environment);
			Preprocessor.f_ReadFile(ConfigFile);
		}

		CRegistryPreserveAll OriginalRegistry = Registry;

		TCVector<CStr> ExcludePatterns;

		if (auto pValue = Registry.f_GetChild("ExcludePatterns"))
			ExcludePatterns = pValue->f_GetThisValue().f_Split(";");
		else
		{
			ExcludePatterns = {"*/.git", "*/.DS_Store"};
			Registry.f_SetValue("ExcludePatterns", CStr::fs_Join(ExcludePatterns, ";"));
		}

		auto pProjects = Registry.f_CreateChild("Projects");
		auto pTags = Registry.f_CreateChild("Tags");
		TCSet<CStr> Tags;
		if (pTags)
		{
			for (auto iTag = pTags->f_GetChildIterator("Tag"); iTag && iTag->f_GetName() == "Tag"; ++iTag)
			{
				Tags[iTag->f_GetThisValue()];
			}
		}

#if DPlatform_Windows
		CStr DefaultRoot = Registry.f_GetValue("DefaultRoot", "X:/Deploy");
#else
		CStr DefaultRoot = Registry.f_GetValue("DefaultRoot", "/Deploy");
#endif

		auto pProject = pProjects->f_GetChild(DestinationProject);
		if (!pProject)
		{
			pProject = pProjects->f_CreateChild(DestinationProject);
			auto pDestination = pProject->f_CreateChild("Destination", true);
			pDestination->f_SetThisValue(CFile::fs_AppendPath(DefaultRoot, DestinationProject));
			DConOut("Added {} post copy project to config file" DNewLine, DestinationProject);
		}

		auto fCopySourceFile = [&](CStr const &_SourceFile, CStr const &_SubPath)
			{
				if (!CFile::fs_FileExists(_SourceFile))
					DError(CStr(CStr::CFormat("Source file does not exist: {}") << _SourceFile));

				CStr SourceFileName = CFile::fs_GetFile(_SourceFile);
				for (auto iDest = pProject->f_GetChildIterator("Destination"); iDest && iDest->f_GetName() == "Destination"; ++iDest)
				{
					CHashCache HashCache(CStr(), false, false);

					bool bTagFound = false;
					bool bTagMatched = false;
					for (auto iTag = iDest->f_GetChildIterator("Tag"); iTag && iTag->f_GetName() == "Tag"; ++iTag)
					{
						bTagFound = true;
						if (Tags.f_FindEqual(iTag->f_GetThisValue()))
						{
							bTagMatched = true;
							break;
						}
					}
					if (bTagFound && !bTagMatched)
						continue;

					bool bEnabledIf = true;
					for (auto iEnableIf = iDest->f_GetChildIterator("EnableIf"); iEnableIf && iEnableIf->f_GetName() == "EnableIf"; ++iEnableIf)
					{
						if (_SourceFile.f_Find(iEnableIf->f_GetThisValue()) < 0)
						{
							bEnabledIf = false;
							break;
						}
					}

					if (!bEnabledIf)
						continue;

					CStr NewFileName = iDest->f_GetValue("Rename", SourceFileName);

					CStr Destination = iDest->f_GetThisValue();
					CStr FullDestination;

					TCUniquePointer<CHashCache> pOldHashCache;
					if (bHash)
						pOldHashCache = fg_Construct(Destination + "/Files.hashes", false, false);

					CStr FullDestinationFolder;
					if (DestinationFolder.f_IsEmpty())
						FullDestinationFolder = _SubPath;
					else
						FullDestinationFolder = CFile::fs_AppendPath(DestinationFolder, _SubPath);

					if (FullDestinationFolder.f_IsEmpty())
						FullDestination = CFile::fs_AppendPath(Destination, NewFileName);
					else
						FullDestination = CFile::fs_AppendPath(CFile::fs_AppendPath(Destination, FullDestinationFolder), NewFileName);

					EFileAttrib SupportedAttributes = CFile::fs_GetSupportedAttributes();
					EFileAttrib ValidAttributes = CFile::fs_GetValidAttributes();
					CStr LastReportedError;
					if
						(
							CFile::fs_DiffCopyFileOrDirectory
							(
								CFile::fs_GetExpandedPath(_SourceFile)
								, CFile::fs_GetExpandedPath(FullDestination)
								, [&](CFile::EDiffCopyChange _Change, CStr const &_Source, CStr const &_Destination, CStr const &_Link)
								{
									switch (_Change)
									{
									case CFile::EDiffCopyChange_FileDeleted:
										{
											EFileAttrib Attributes = CFile::fs_GetAttributes(_Destination);
											if ((Attributes & EFileAttrib_ReadOnly) || (!(Attributes & EFileAttrib_UserWrite) && (SupportedAttributes & EFileAttrib_UserWrite)))
												CFile::fs_SetAttributes(_Destination, (Attributes & ~EFileAttrib_ReadOnly) | (SupportedAttributes & EFileAttrib_UserWrite) | ValidAttributes);
										}
										break;
									case CFile::EDiffCopyChange_FileChanged:
										{
											EFileAttrib Attributes = CFile::fs_GetAttributes(_Destination);

											if ((Attributes & EFileAttrib_ReadOnly) || (!(Attributes & EFileAttrib_UserWrite) && (SupportedAttributes & EFileAttrib_UserWrite)))
											{
												bool bSuccess = false;

												try
												{
													CPerforceClientThrow Client;
													if (CPerforceClientThrow::fs_GetFromP4Config(_Destination, Client))
													{
														Client.f_OpenForEdit(_Destination);
														DConOut("Opened file for edit in perforce: {}{\n}", _Destination);
														bSuccess = true;
													}
												}
												catch (NException::CException const &_Error)
												{
													CStr Error = _Error.f_GetErrorStr();
													if (Error != LastReportedError)
													{
														LastReportedError = Error;
														DConErrOut("Failed to checkout via perforce:{\n}{}{\n}", Error);
													}
												}

												if (!bSuccess)
													CFile::fs_SetAttributes(_Destination, (Attributes & ~EFileAttrib_ReadOnly)  | (SupportedAttributes & EFileAttrib_UserWrite) | ValidAttributes);
											}
										}
										break;
									}
									if (bHash)
									{
										switch (_Change)
										{
										case CFile::EDiffCopyChange_FileCreated:
										case CFile::EDiffCopyChange_FileChanged:
											{
												HashCache.f_GetHash(_Source);
												HashCache.f_GetHash(_Destination, _Source);
											}
											break;
										case CFile::EDiffCopyChange_NoChange:
											{
												// No change, just use tho old hash
												HashCache.f_SetHash(_Source, pOldHashCache->f_GetHash(_Source));
												HashCache.f_SetHash(_Destination, pOldHashCache->f_GetHash(_Destination, _Source));
											}
											break;
										}
									}
									if (bVerbose)
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
										}
									}
									return CFile::EDiffCopyChangeAction_Perform;
								}
							 	, ExcludePatterns
							)
						)
					{
						DConOut("{} -> {}" DNewLine, _SourceFile << FullDestination);
					}
					if (bHash)
						HashCache.f_SaveToFile(Destination + "/Files.hashes");
				}
			}
		;

		CStr SourceFileName = CFile::fs_GetFile(SourceFile);

		if (CFile::fs_GetFile(SourceFile).f_FindChars("~^*?") >= 0)
		{
			CStr BasePath = CFile::fs_GetPath(SourceFile);
			mint BasePathLen = BasePath.f_GetLen() + 1;

			bool bDirectory = false;
			if (SourceFileName.f_StartsWith("~"))
			{
				bDirectory = true;
				SourceFileName = SourceFileName.f_Extract(1);
			}

			bool bRecursive = false;
			if (SourceFileName.f_StartsWith("^"))
			{
				bRecursive = true;
				SourceFileName = SourceFileName.f_Extract(1);
			}

			SourceFile = CFile::fs_AppendPath(BasePath, SourceFileName);

			NMib::NFile::CFile::CFindFilesOptions FindOptions{SourceFile, bRecursive};

			FindOptions.m_ExcludePatterns = ExcludePatterns;

			if (bDirectory)
				FindOptions.m_AttribMask = EFileAttrib_Directory;
			else
				FindOptions.m_AttribMask = EFileAttrib_File;

			auto Files = CFile::fs_FindFiles(FindOptions);
			if (Files.f_IsEmpty())
				DError(fg_Format("No files found for pattern '{}'", SourceFile));

			for (auto &FoundFile : Files)
			{
				CStr SubPath = CFile::fs_GetPath(FoundFile.m_Path.f_Extract(BasePathLen));
				if (bVerbose)
					DConOut("Wildcard found : {} in {}" DNewLine, FoundFile.m_Path << SubPath);

				fCopySourceFile(FoundFile.m_Path, SubPath);
			}
		}
		else
			fCopySourceFile(SourceFile, "");

		if (Registry != OriginalRegistry)
		{
			CStr NewRegistry = Registry.f_GenerateStr();
			CByteVector Temp;
			CFile::fs_WriteStringToVector(Temp, NewRegistry);

			CFile::fs_CreateDirectory(CFile::fs_GetPath(ConfigFile));
			if (CFile::fs_CopyFileDiff(Temp, ConfigFile, CTime::fs_NowUTC()))
			{
				DConOut("Wrote new config file: {}" DNewLine, ConfigFile);
			}
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PostCopy);

