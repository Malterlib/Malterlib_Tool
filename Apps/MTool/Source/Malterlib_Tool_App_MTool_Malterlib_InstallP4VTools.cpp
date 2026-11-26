// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Malterlib.h"

#include <Mib/Perforce/FileNames>
#include <Mib/XML/XML>

namespace
{
	struct CP4VTool
	{
		enum EType
		{
			EType_Tool
			, EType_Separator
			, EType_Folder
		};

		EType m_Type = EType_Tool;
		CStr m_Name;
		CStr m_Arguments;              // The MTool command line, with P4V's %t (stream), %c (changelist), %f (file) and $c (workspace)
		bool m_bAddToContext = true;   // Whether the tool is in the context menus as well as the Tools menu
		TCVector<CP4VTool> m_Children; // The entries of a folder
	};

	CP4VTool fg_Tool(CStr const &_Name, CStr const &_Arguments, bool _bAddToContext = true)
	{
		return {.m_Type = CP4VTool::EType_Tool, .m_Name = _Name, .m_Arguments = _Arguments, .m_bAddToContext = _bAddToContext};
	}

	CP4VTool fg_Separator()
	{
		return {.m_Type = CP4VTool::EType_Separator};
	}

	CP4VTool fg_Folder(CStr const &_Name, TCVector<CP4VTool> &&_Children)
	{
		return {.m_Type = CP4VTool::EType_Folder, .m_Name = _Name, .m_Children = fg_Move(_Children)};
	}

	// The P4V custom tools of MTool's Perforce helpers, with the arguments of the old install kit's tools. P4V gives the helpers its
	// connection in P4PORT, P4USER and P4CLIENT.
	TCVector<CP4VTool> fg_P4VTools()
	{
		return
			{
				fg_Tool("Run script", "PerforceRunScript \"File=%f\"")
				, fg_Tool("Submit in stream", "PerforceSubmitInStream Workspace=$c \"File=%f\"")
				, fg_Separator()
				, fg_Tool("Switch to stream", "PerforceSwitchWorkspaceTaskStream \"Stream=%t\"")
				, fg_Tool("Switch to stream and owned", "PerforceSwitchWorkspaceTaskStream \"Stream=%t\" SwitchOwned=true")
				, fg_Tool("Initialize stream workspace", "PerforceInitializeStream \"Stream=%t\"")
				, fg_Tool("Initialize all stream workspaces", "PerforceInitializeStream \"Stream=*\"", false)
				, fg_Separator()
				, fg_Folder
				(
					"Merge"
					,
					{
						fg_Tool("Merge stream to parent", "PerforceMerge \"Stream=%t\" Direction=Parent")
						, fg_Tool("Merge stream to child", "PerforceMerge \"Stream=%t\" Direction=Child")
						, fg_Tool("Merge stream to sibling", "PerforceMerge \"Stream=%t\" Direction=Sibling")
						, fg_Separator()
						, fg_Tool("Merge changelist to parent", "PerforceMerge \"ChangeList=%c\" Direction=Parent")
						, fg_Tool("Merge changelist to child", "PerforceMerge \"ChangeList=%c\" Direction=Child")
						, fg_Tool("Merge changelist to sibling", "PerforceMerge \"ChangeList=%c\" Direction=Sibling")
					}
				)
				, fg_Folder
				(
					"Streams"
					,
					{
						fg_Tool("Create task stream", "PerforceCreateStream Type=Task \"Stream=%t\"")
						, fg_Tool("Create stream", "PerforceCreateStream Type=Normal \"Stream=%t\"")
						, fg_Tool("Delete stream", "PerforceDeleteStream \"Stream=%t\"")
						, fg_Tool("Update imports", "PerforceCreateStream Update=true \"Stream=%t\"")
						, fg_Tool("Create branches", "PerforceCreateStreamBranches \"Stream=%t\"")
						, fg_Tool("Create patches", "PerforceCreatePatchesForStream \"Stream=%t\"")
						, fg_Tool("Create patches for children", "PerforceCreatePatchesForStream \"Stream=%t\" ForChildren=true")
						, fg_Tool("Add to build server", "PerforceSetBuildServer \"Stream=%t\" Action=Add")
						, fg_Tool("Remove from build server", "PerforceSetBuildServer \"Stream=%t\" Action=Remove")
						, fg_Folder("Unsafe", {fg_Tool("Delete stream and obliterate", "PerforceDeleteStream \"Stream=%t\" ForceObliterate=true")})
					}
				)
				, fg_Folder
				(
					"Changelist"
					,
					{
						fg_Tool("Take Ownership", "PerforceTakeOwnership \"ChangeList=%c\"")
						, fg_Tool("Unshelve related stream", "PerforceUnshelveRelatedStream \"ChangeList=%c\"")
						, fg_Tool("Force delete changelist", "PerforceForceDeleteChangelist \"Changelist=%c\"")
					}
				)
			}
		;
	}

	// The settings of P4V are in ~/Library/Preferences/com.perforce.p4v on macOS, and in ~/.p4qt on Windows and Linux.
	CStr fg_DefaultCustomToolsFile()
	{
#ifdef DPlatformFamily_macOS
		return CFile::fs_GetUserHomeDirectory() / "Library/Preferences/com.perforce.p4v/customtools.xml";
#else
		return CFile::fs_GetUserHomeDirectory() / ".p4qt/customtools.xml";
#endif
	}

	// The MTool beside the running one. The safe copy that mib runs on Windows is replaced whenever the binaries change, so the
	// tools run the binaries it was copied from instead, which BuildSystem/SafeMib/SafeMib.source names.
	CStr fg_DefaultMTool()
	{
		CStr Directory = CFile::fs_GetProgramDirectory();
		CStr SafeMib = CFile::fs_GetPath(Directory);
		if (CFile::fs_GetFile(Directory) == "Binaries" && CFile::fs_GetFile(SafeMib) == "SafeMib" && CFile::fs_FileExists(SafeMib / "SafeMib.source"))
		{
			CStr Root = CFile::fs_GetPath(CFile::fs_GetPath(SafeMib));
			CStr Source = CFile::fs_ReadStringFromFile(SafeMib / "SafeMib.source", true).f_Trim();
			CStr SourceDirectory = CFile::fs_GetExpandedPath(Source, Root);
			if (CFile::fs_FileExists(SourceDirectory / ("MTool" + CFile::mc_ExecutableExtension)))
				Directory = SourceDirectory;
		}

		return Directory / ("MTool" + CFile::mc_ExecutableExtension);
	}

	// Only MTools are installed, so that a later install recognises the tools of an earlier one, also those of the old install
	// kit's MTool_x64.exe.
	bool fg_IsMToolCommand(CStr const &_Command)
	{
		CStr Program = CFile::fs_GetFileNoExt(_Command);
		return Program.f_CmpNoCase("MTool") == 0 || Program.f_StartsWithNoCase("MTool_");
	}

	void fg_AddArguments(TCVector<CP4VTool> const &_Tools, TCSet<CStr> &o_Arguments)
	{
		for (auto const &Tool : _Tools)
		{
			if (Tool.m_Type == CP4VTool::EType_Tool)
				o_Arguments[Tool.m_Arguments];
			fg_AddArguments(Tool.m_Children, o_Arguments);
		}
	}

	// A tool this command installed runs an MTool with the arguments of one of the template's tools.
	bool fg_IsManagedTool(CXMLElement const *_pTool, TCSet<CStr> const &_ManagedArguments)
	{
		CXMLNode const *pDefinition = CXMLDocument::fs_GetChildNode(_pTool, "Definition");
		if (!pDefinition)
			return false;

		CStr Command = CXMLDocument::fs_GetChildValue(pDefinition, "Command", CStr());
		return fg_IsMToolCommand(Command) && _ManagedArguments.f_FindEqual(CXMLDocument::fs_GetChildValue(pDefinition, "Arguments", CStr()));
	}

	// Removes the tools this command installed from the list and its folders, and the folders that removing them leaves without
	// tools, but no folder the user left empty. Whether it removed any.
	bool fg_RemoveManagedTools(CXMLElement *_pList, TCSet<CStr> const &_ManagedArguments)
	{
		bool bRemoved = false;
		for (CXMLElement *pChild = _pList->FirstChildElement(); pChild;)
		{
			CXMLElement *pNext = pChild->NextSiblingElement();
			if (CXMLElement *pFolderList = pChild->FirstChildElement("CustomToolDefList"))
			{
				if (fg_RemoveManagedTools(pFolderList, _ManagedArguments))
				{
					bRemoved = true;
					if (!pFolderList->FirstChildElement("CustomToolDef") && !pFolderList->FirstChildElement("CustomToolFolder"))
						_pList->DeleteChild(pChild);
				}
			}
			else if (fg_IsManagedTool(pChild, _ManagedArguments))
			{
				bRemoved = true;
				_pList->DeleteChild(pChild);
			}
			pChild = pNext;
		}

		return bRemoved;
	}

	// Adds the tools at the end of the list, after one separator in place of the ones the list ends with, which also replaces the
	// separators that removing the tools of an earlier install leaves, so that a reinstall gives the same file. A folder that the
	// list already has, kept for the user's own tools in it, gets them instead of a second folder of the same name.
	void fg_AddTools(CXMLElement *_pList, TCVector<CP4VTool> const &_Tools, CStr const &_MTool)
	{
		for (CXMLElement *pLast = _pList->LastChildElement(); pLast && CStr(pLast->Name()) == "Separator"; pLast = _pList->LastChildElement())
			_pList->DeleteChild(pLast);

		if (_pList->FirstChildElement())
			CXMLDocument::fs_AddElement(_pList, "Separator");

		for (auto const &Tool : _Tools)
		{
			switch (Tool.m_Type)
			{
			case CP4VTool::EType_Separator: CXMLDocument::fs_AddElement(_pList, "Separator"); break;

			case CP4VTool::EType_Folder:
				{
					CXMLElement *pFolderList = nullptr;
					for (CXMLElement *pFolder = _pList->FirstChildElement("CustomToolFolder"); pFolder && !pFolderList; pFolder = pFolder->NextSiblingElement("CustomToolFolder"))
					{
						if (CXMLDocument::fs_GetChildValue(pFolder, "Name", CStr()) == Tool.m_Name)
							pFolderList = pFolder->FirstChildElement("CustomToolDefList");
					}

					if (!pFolderList)
					{
						CXMLElement *pFolder = CXMLDocument::fs_CreateElement(_pList, "CustomToolFolder");
						CXMLDocument::fs_AddElementAndText(pFolder, "Name", Tool.m_Name);
						CXMLDocument::fs_AddElementAndBool(pFolder, "AddToContext", Tool.m_bAddToContext);
						pFolderList = CXMLDocument::fs_CreateElement(pFolder, "CustomToolDefList");
					}
					fg_AddTools(pFolderList, Tool.m_Children, _MTool);
				}
				break;

			case CP4VTool::EType_Tool:
				{
					CXMLElement *pTool = CXMLDocument::fs_CreateElement(_pList, "CustomToolDef");
					CXMLElement *pDefinition = CXMLDocument::fs_CreateElement(pTool, "Definition");
					CXMLDocument::fs_AddElementAndText(pDefinition, "Name", Tool.m_Name);
					CXMLDocument::fs_AddElementAndText(pDefinition, "Command", _MTool);
					CXMLDocument::fs_AddElementAndText(pDefinition, "Arguments", Tool.m_Arguments);
					CXMLElement *pConsole = CXMLDocument::fs_CreateElement(pTool, "Console");
					CXMLDocument::fs_AddElementAndBool(pConsole, "CloseOnExit", false);
					if (Tool.m_bAddToContext)
						CXMLDocument::fs_AddElementAndBool(pTool, "AddToContext", true);
					CXMLDocument::fs_AddElementAndBool(pTool, "Refresh", true);
				}
				break;
			}
		}
	}

	enum EInstallResult
	{
		EInstallResult_Installed
		, EInstallResult_Updated
		, EInstallResult_Unchanged
	};

	// Merges the tools into the P4V custom tools file _File, keeping the user's own tools, and keeps the previous file beside it as
	// <file>.bak.
	EInstallResult fg_InstallP4VTools(CStr const &_File, CStr const &_MTool)
	{
		CXMLDocument Document(false);
		tinyxml2::XMLDocument *pDocument = Document.f_GetRootNode()->ToDocument();
		CXMLElement *pList = nullptr;
		bool bExists = CFile::fs_FileExists(_File);
		CByteVector Previous;

		if (bExists)
		{
			Previous = CFile::fs_ReadFile(_File);
			if (!Document.f_ParseString(CFile::fs_ReadStringFromVector(Previous, true)))
				DMibError("Failed to parse '{}': {}"_f << _File << pDocument->ErrorStr());

			pList = pDocument->FirstChildElement("CustomToolDefList");
			if (!pList)
				DMibError("'{}' has no CustomToolDefList"_f << _File);

			TCSet<CStr> ManagedArguments;
			fg_AddArguments(fg_P4VTools(), ManagedArguments);
			fg_RemoveManagedTools(pList, ManagedArguments);
		}
		else
		{
			pDocument->InsertEndChild(pDocument->NewDeclaration());
			pDocument->InsertEndChild(pDocument->NewComment("perforce-xml-version=1.0"));
			pList = CXMLDocument::fs_CreateElement(pDocument, "CustomToolDefList");
			CXMLDocument::fs_SetAttribute(pList, "varName", "customtooldeflist");
		}

		fg_AddTools(pList, fg_P4VTools(), _MTool);

		CByteVector Data;
		CFile::fs_WriteStringToVector(Data, Document.f_GetAsString(), false);
		if (bExists && Data == Previous)
			return EInstallResult_Unchanged;

		NTime::CTime Now = NTime::CTime::fs_NowUTC();
		CFile::fs_CreateDirectoryForFile(_File);
		if (bExists)
			CFile::fs_CopyFileDiff(Previous, _File + ".bak", Now);
		CFile::fs_CopyFileDiff(Data, _File, Now);

		return bExists ? EInstallResult_Updated : EInstallResult_Installed;
	}
}

void CTool_Malterlib::f_Register_InstallP4VTools(CDistributedAppCommandLineSpecification::CSection &o_UtilitiesSection)
{
	o_UtilitiesSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["install-p4v-tools"]
				, "Description"_o= "Install the custom tools that run MTool's Perforce stream, merge and changelist helpers in P4V.\n"
				"The tools are merged into P4V's custom tools file: the tools of an earlier install are replaced, and the user's own are "
				"kept. The previous file is kept beside it with a .bak extension. Restart P4V, or reopen Tools > Manage Custom Tools, "
				"to see the change.\n"
				, "GlobalOptions"_o= CDistributedAppCommandLineSpecification::fs_RelevantHelpGlobalOptions()
				, "Options"_o=
				{
					"File?"_o=
					{
						"Names"_o= _o["--file"]
						, "Default"_o= ""
						, "Description"_o= "The P4V custom tools file to update. Defaults to P4V's: "
						"~/Library/Preferences/com.perforce.p4v/customtools.xml on macOS, and ~/.p4qt/customtools.xml on Windows and Linux."
					}
					, "MTool?"_o=
					{
						"Names"_o= _o["--mtool"]
						, "Default"_o= ""
						, "Description"_o= "The MTool the tools run, which has to be called MTool or MTool_<Name>. Defaults to the "
						"checkout's MTool that runs this command."
					}
				}
			}
			, [](CEJsonSorted _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				auto Capture = co_await (g_CaptureExceptions % "Installing the P4V custom tools");

				CStr CurrentDirectory = fs_GetLogicalCurrentDirectory();
				CStr File = _Params["File"].f_String();
				CStr MTool = _Params["MTool"].f_String();

				auto [Result, InstalledFile, Command] = co_await
					(
						g_Dispatch(fg_BlockingActor()) / [File, MTool, CurrentDirectory]() mutable -> TCTuple<EInstallResult, CStr, CStr>
						{
							File = File ? CFile::fs_GetExpandedPath(File, CurrentDirectory) : fg_DefaultCustomToolsFile();
							MTool = MTool ? CFile::fs_GetExpandedPath(MTool, CurrentDirectory) : fg_DefaultMTool();
							if (!fg_IsMToolCommand(MTool))
								DMibError("--mtool has to name an MTool or MTool_<Name>, which a later install recognises: {}"_f << MTool);
							MTool = NPerforce::fg_PerforcePath(MTool);
							return {fg_InstallP4VTools(File, MTool), File, MTool};
						}
					)
				;

				if (Result == EInstallResult_Unchanged)
				{
					*_pCommandLine += "The P4V custom tools in {} are up to date, running {}\n"_f << InstalledFile << Command;
					co_return 0;
				}

				*_pCommandLine += "{} the P4V custom tools in {}\n"_f << (Result == EInstallResult_Updated ? "Updated" : "Installed") << InstalledFile;
				*_pCommandLine += "The tools run {}\n"_f << Command;
				if (Result == EInstallResult_Updated)
					*_pCommandLine += "The previous file is kept as {}.bak\n"_f << InstalledFile;
				*_pCommandLine += "Restart P4V, or reopen Tools > Manage Custom Tools, to see the change.\n";

				co_return 0;
			}
		)
	;
}
