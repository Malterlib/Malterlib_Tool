// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/XML/XML>

struct CTool_VSToMalterlibBuild : public CTool
{
	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr SourcePath = _Params.f_GetValue("0", "NotExist");

		auto Files = CFile::fs_FindFiles(SourcePath, EFileAttrib_File, true);

		if (Files.f_IsEmpty())
			DError("No source files found to convert");

		for (auto iFile = Files.f_GetIterator(); iFile; ++iFile)
		{
			CXMLDocument XMLFile;
			CXMLDocument XMLFiltersFile;
			CStr ProjectName = CFile::fs_GetFileNoExt(*iFile);
			XMLFile.f_ParseFile(*iFile);
			if (CFile::fs_FileExists(*iFile + ".filters"))
				XMLFiltersFile.f_ParseFile(*iFile + ".filters");

			auto pProject = XMLFile.fs_GetChildNode(XMLFile.f_GetRootNode(), "Project");
			auto pFiltersProject = XMLFiltersFile.fs_GetChildNode(XMLFiltersFile.f_GetRootNode(), "Project");

			CRegistryPreserveAll OutRegistry;
			auto pTarget = OutRegistry.f_SetValue("%Target", ProjectName);

			TCMap<CStr, CStr> Files;
			TCMap<CStr, CRegistryPreserveAll *> Groups;

			{
				CXMLNode const *pChild = nullptr;
				while ((pChild = XMLFile.fs_Iterate(pProject, pChild)))
				{
					if (auto pElement = pChild->ToElement())
					{
						CStr Text = XMLFile.fs_GetValue(pChild);
						CStr Label = XMLFile.fs_GetAttribute(pElement, "Label");
						if (Text == "ItemGroup" && Label.f_IsEmpty())
						{
							CXMLNode const *pFile = nullptr;
							while ((pFile = XMLFile.fs_Iterate(pChild, pFile)))
							{
								if (auto pFileElement = pFile->ToElement())
								{
									CStr FileName = XMLFile.fs_GetAttribute(pFileElement, "Include");

									if (!FileName.f_IsEmpty())
										Files[FileName];
								}
							}
						}
					}
				}
			}
			if (pFiltersProject)
			{
				CXMLNode const *pChild = nullptr;
				while ((pChild = XMLFiltersFile.fs_Iterate(pFiltersProject, pChild)))
				{
					if (auto pElement = pChild->ToElement())
					{
						CStr Text = XMLFiltersFile.fs_GetValue(pChild);
						CStr Label = XMLFiltersFile.fs_GetAttribute(pElement, "Label");
						if (Text == "ItemGroup" && Label.f_IsEmpty())
						{
							CXMLNode const *pFile = nullptr;
							while ((pFile = XMLFiltersFile.fs_Iterate(pChild, pFile)))
							{
								CStr NodeName = XMLFiltersFile.fs_GetValue(pFile);
								if (auto pFileElement = pFile->ToElement())
								{
									CStr FileName = XMLFiltersFile.fs_GetAttribute(pFileElement, "Include");

									if (!FileName.f_IsEmpty())
									{
										if (NodeName == "Filter")
										{
											auto Mapped = Groups(FileName);
											if (Mapped.f_WasCreated())
											{
												CStr GroupIter = FileName;
												auto pParent = pTarget;
												while (!GroupIter.f_IsEmpty())
												{
													CStr Name = fg_GetStrSep(GroupIter, "\\");
													CRegistryPreserveAll *pGroup = nullptr;
													for (auto iChild = pParent->f_GetChildIterator(); iChild; ++iChild)
													{
														if (iChild->f_GetName() == "%Group" && iChild->f_GetThisValue() == Name)
														{
															pGroup = iChild;
															break;
														}
													}
													if (pGroup == nullptr)
													{
														pGroup = pParent->f_CreateChildNoPath("%Group", true);
														pGroup->f_SetThisValue(Name);
													}
													pParent = pGroup;
												}
												*Mapped = pParent;
											}
										}
										else
										{
											auto pNode = XMLFiltersFile.fs_GetChildNode(pFile, "Filter");
											if (pNode)
											{
												CStr Group = XMLFiltersFile.fs_GetNodeText(pNode);
												Files[FileName] = Group;
											}
										}
									}
								}
							}
						}
					}
				}
			}

			{
				for (auto iFile = Files.f_GetIterator(); iFile; ++iFile)
				{
					auto *pParent = pTarget;
					auto pGroup = Groups.f_FindEqual(*iFile);
					if (pGroup)
						pParent = *pGroup;
					auto pFile = pParent->f_CreateChildNoPath("%File", true);
					pFile->f_SetThisValue(iFile.f_GetKey().f_ReplaceChar('\\', '/'));
				}
			}

			//CStr OutFileName = CFile::fs_GetPath(*iFile) + "/" + ProjectName + ".MTargetConv";
			//CFile::fs_WriteStringToFile(CStr(OutFileName), OutRegistry.f_GenerateStr());
			CStr OutFileName2 = CFile::fs_GetPath(*iFile) + "/" + ProjectName + ".MTarget";
			if (!CFile::fs_FileExists(OutFileName2))
				CFile::fs_WriteStringToFile(CStr(OutFileName2), OutRegistry.f_GenerateStr());
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_VSToMalterlibBuild);
