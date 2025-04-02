// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystemPreprocessor>
#include <Mib/BuildSystem/BuildSystem>
#include <Mib/Perforce/Wrapper>
#include <Mib/Cryptography/MD5Cache>
#include <Mib/Container/Convert>
#include <Mib/Encoding/JSONShortcuts>

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

	static CBuildSystemSyntax::CRootKey fs_GetIdentifierKey(CStr const &_Identifier)
	{
		return CBuildSystemSyntax::CIdentifier
			{
				NMib::NBuildSystem::CStringAndHash(CAssertAddedToStringCache(), _Identifier, _Identifier.f_Hash())
				, EEntityType_Invalid
				, EPropertyType_Property
				, true
			}
			.f_RootKey()
		;
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr ConfigFile;
		CStr SourceFile;
		CStr DestinationProject;
		CStr DestinationFolder;

		bool bVerbose = false;
		bool bHash = false;
		bool bDirectory = false;
		bool bRecursive = false;

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
			else if (Key == "FindDirectory")
				bDirectory = Value.f_ToInt(0) != 0 || Value == "true";
			else if (Key == "Recursive")
				bRecursive = Value.f_ToInt(0) != 0 || Value == "true";
			else
				DError(CStr(CStr::CFormat("Unknown setting: {}") << Key));
		}

		if (ConfigFile.f_IsEmpty())
			DError("No config file specified");

		if (SourceFile.f_IsEmpty())
			DError("No source file specified");

		if (DestinationProject.f_IsEmpty())
			DError("No destination project specified");

		CBuildSystemRegistry Registry;
		CStringCache StringCache;
		TCMap<CStr, TCSharedPointer<CHashDigest_SHA256>> SourceFiles;
		NBuildSystem::CFindCache FindCache;
		if (CFile::fs_FileExists(ConfigFile))
		{
			TCMap<CStr, CStr> Environment = fg_GetSys()->f_Environment();
			NBuildSystem::CBuildSystemPreprocessor Preprocessor(Registry, SourceFiles, FindCache, Environment, StringCache);
			Preprocessor.f_ReadFile(ConfigFile);
		}

		CBuildSystemRegistry OriginalRegistry = Registry;

		TCVector<CStr> ExcludePatterns;

		auto ExcludePatternsKey = fs_GetIdentifierKey("ExcludePatterns");

		if (auto pValue = Registry.f_GetChildNoPath(ExcludePatternsKey))
		{
			if (pValue->f_GetThisValue().m_Value.f_IsConstantString())
			{
				if (!pValue->f_GetThisValue().m_Value.f_ConstantString().f_IsEmpty())
					ExcludePatterns = pValue->f_GetThisValue().m_Value.f_ConstantString().f_Split<true>(";");

				CEJSONSorted Array;
				for (auto &Pattern : ExcludePatterns)
					Array.f_Insert(Pattern);

				Registry.f_SetValueNoPath(ExcludePatternsKey, {{Array}});
			}
			else
			{
				auto &Value = pValue->f_GetThisValue().m_Value;
				if (!Value.f_IsConstant() || !Value.f_Constant().f_IsStringArray())
					CBuildSystem::fs_ThrowError(*pValue, "ExcludePatterns needs to be an array of strings");

				ExcludePatterns = Value.f_Constant().f_StringArray();
			}
		}
		else
		{
			ExcludePatterns = {"*/.git", "*/.DS_Store"};

			CEJSONSorted Array;
			for (auto &Pattern : ExcludePatterns)
				Array.f_Insert(Pattern);
			Registry.f_SetValueNoPath(ExcludePatternsKey, {{Array}});
		}

		auto TagsKey = fs_GetIdentifierKey("Tags");
		auto TagKey = fs_GetIdentifierKey("Tag");

		auto pProjects = Registry.f_CreateChildNoPath(fs_GetIdentifierKey("Projects"));
		auto pTags = Registry.f_CreateChildNoPath(TagsKey);
		TCSet<CStr> Tags;
		if (pTags)
		{
			if (pTags->f_GetThisValue().m_Value.f_IsConstant() && pTags->f_GetThisValue().m_Value.f_Constant().f_IsArray())
			{
				for (auto &Tag : pTags->f_GetThisValue().m_Value.f_Constant().f_Array())
				{
					if (!Tag.f_IsString())
						CBuildSystem::fs_ThrowError(*pTags, "Tags needs to be an array of strings");
					Tags[Tag.f_String()];
				}
			}
			else if (pTags->f_GetThisValue().m_Value.f_IsConstantString())
				pTags->f_SetThisValue(CBuildSystemSyntax::CRootValue{.m_Value = {CBuildSystemSyntax::CArray{}}});
			else
			{
				for (auto iTag = pTags->f_GetChildIterator(TagKey); iTag && iTag->f_GetName() == TagKey; ++iTag)
				{
					if (!iTag->f_GetThisValue().m_Value.f_IsConstantString())
						CBuildSystem::fs_ThrowError(*pTags, "Tags needs to be an strings");

					Tags[iTag->f_GetThisValue().m_Value.f_ConstantString()];
				}

				CEJSONSorted Array;
				for (auto &Tag : Tags)
					Array.f_Insert(Tag);

				pTags->f_SetThisValue({{Array}});
				pTags->f_DeleteAllChildren();
			}
		}

		CStr DefaultRootDefault = fg_GetSys()->f_GetEnvironmentVariable("MalterlibDeployRoot");

#if DPlatform_Windows
		if (!DefaultRootDefault)
			DefaultRootDefault = "X:/Deploy";
#else
		if (!DefaultRootDefault)
			DefaultRootDefault = "/opt/Deploy";
#endif
		CBuildSystemSyntax::CRootValue DefaultRootDefaultRootValue{.m_Value = {DefaultRootDefault}};

		CStr DefaultRoot;
		if (auto DefaultRootJSON = Registry.f_GetValueNoPath(fs_GetIdentifierKey("DefaultRoot"), DefaultRootDefaultRootValue); DefaultRootJSON.m_Value.f_IsConstantString())
			DefaultRoot = DefaultRootJSON.m_Value.f_ConstantString();
		else
			DefaultRoot = DefaultRootDefault;

		auto DestinationKey = fs_GetIdentifierKey("Destination");
		auto EnableIfKey = fs_GetIdentifierKey("EnableIf");
		auto DestinationProjectKey = fs_GetIdentifierKey(DestinationProject);

		auto pProject = pProjects->f_GetChildNoPath(DestinationProjectKey);
		if (!pProject)
		{
			pProject = pProjects->f_CreateChildNoPath(DestinationProjectKey);
			auto pDestination = pProject->f_CreateChildNoPath(DestinationKey, true);
			pDestination->f_SetThisValue(CBuildSystemSyntax::CRootValue{.m_Value = {CFile::fs_AppendPath(DefaultRoot, DestinationProject)}});
			DConOut("Added {} post copy project to config file" DNewLine, DestinationProject);
		}

		auto fCopySourceFile = [&](CStr const &_SourceFile, CStr const &_SubPath)
			{
				if (!CFile::fs_FileExists(_SourceFile))
					DError(CStr(CStr::CFormat("Source file does not exist: {}") << _SourceFile));

				CStr SourceFileName = CFile::fs_GetFile(_SourceFile);
				for (auto iDest = pProject->f_GetChildIterator(DestinationKey); iDest && iDest->f_GetName() == DestinationKey; ++iDest)
				{
					CHashCache HashCache(CStr(), false, false);

					bool bTagFound = false;
					bool bTagMatched = false;
					for (auto iTag = iDest->f_GetChildIterator(TagKey); iTag && iTag->f_GetName() == TagKey; ++iTag)
					{
						bTagFound = true;
						if (!iTag->f_GetThisValue().m_Value.f_IsConstantString())
							CBuildSystem::fs_ThrowError(*iTag, "Tag needs to be a constant string");

						if (Tags.f_FindEqual(iTag->f_GetThisValue().m_Value.f_ConstantString()))
						{
							bTagMatched = true;
							break;
						}
					}
					if (bTagFound && !bTagMatched)
						continue;

					bool bEnabledIf = true;
					for (auto iEnableIf = iDest->f_GetChildIterator(EnableIfKey); iEnableIf && iEnableIf->f_GetName() == EnableIfKey; ++iEnableIf)
					{
						if (!iEnableIf->f_GetThisValue().m_Value.f_IsConstantString())
							CBuildSystem::fs_ThrowError(*iEnableIf, "EnableIf needs to be a constant string");

						if (_SourceFile.f_Find(iEnableIf->f_GetThisValue().m_Value.f_ConstantString()) < 0)
						{
							bEnabledIf = false;
							break;
						}
					}

					if (!bEnabledIf)
						continue;

					auto SetNewfileName = iDest->f_GetValueNoPath(fs_GetIdentifierKey("Rename"), CBuildSystemSyntax::CRootValue{.m_Value = {SourceFileName}});
					CStr NewFileName = SourceFileName;
					if (SetNewfileName.m_Value.f_IsConstantString())
						NewFileName = SetNewfileName.m_Value.f_ConstantString();

					if (!iDest->f_GetThisValue().m_Value.f_IsConstantString())
						CBuildSystem::fs_ThrowError(*iDest, "Destination needs to be a constant string");

					CStr Destination = iDest->f_GetThisValue().m_Value.f_ConstantString();
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
									default:
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
										default:
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
										case CFile::EDiffCopyChange_NoChange:
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

		if (bRecursive || bDirectory || CFile::fs_GetFile(SourceFile).f_FindChars("*?") >= 0)
		{
			CStr BasePath = CFile::fs_GetPath(SourceFile);
			mint BasePathLen = BasePath.f_GetLen() + 1;

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
			CFile::fs_WriteStringToVector(Temp, NewRegistry, false);

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

