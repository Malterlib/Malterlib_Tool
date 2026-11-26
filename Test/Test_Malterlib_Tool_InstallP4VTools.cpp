// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/XML/XML>

namespace NMib::NTool
{
	using namespace NTestHelpers;
	using namespace NXML;

	namespace
	{
		struct CP4VTools
		{
			TCVector<CStr> m_Tools;  // Each tool as <folders>/<name>: <command> <arguments>
			TCVector<CStr> m_Layout; // The entries of the top level, with Separator for a separator and the name of a folder or tool
		};

		void fg_ReadTools(CXMLElement const *_pList, CStr const &_Prefix, CP4VTools &o_Tools)
		{
			for (CXMLElement const *pChild = _pList->FirstChildElement(); pChild; pChild = pChild->NextSiblingElement())
			{
				CStr Element = pChild->Name();
				CStr Name = Element == "Separator" ? CStr("Separator") : CXMLDocument::fs_GetChildValue(pChild, "Name", CStr());
				CXMLNode const *pDefinition = CXMLDocument::fs_GetChildNode(pChild, "Definition");
				if (pDefinition)
					Name = CXMLDocument::fs_GetChildValue(pDefinition, "Name", CStr());

				if (_Prefix.f_IsEmpty())
					o_Tools.m_Layout.f_Insert(Name);

				if (auto pFolderList = pChild->FirstChildElement("CustomToolDefList"))
					fg_ReadTools(pFolderList, _Prefix + Name + "/", o_Tools);
				else if (pDefinition)
				{
					CStr Command = CXMLDocument::fs_GetChildValue(pDefinition, "Command", CStr());
					CStr Arguments = CXMLDocument::fs_GetChildValue(pDefinition, "Arguments", CStr());
					o_Tools.m_Tools.f_Insert("{}{}: {} {}"_f << _Prefix << Name << Command << Arguments);
				}
			}
		}

		CP4VTools fg_ReadToolsFile(CStr const &_File)
		{
			CXMLDocument Document(false);
			DMibRequire(Document.f_ParseString(CFile::fs_ReadStringFromFile(_File, true)));

			CXMLElement const *pList = Document.f_GetRootNode()->FirstChildElement("CustomToolDefList");
			DMibRequire(pList);

			CP4VTools Tools;
			fg_ReadTools(pList, "", Tools);
			return Tools;
		}

		// The MTool that runs the command, which the tools run unless told otherwise.
		CStr fg_ExpectedMTool(CRepositoryFixture const &_Repo)
		{
			CStr MTool = _Repo.m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension);
#ifdef DPlatformFamily_Windows
			MTool = MTool.f_Replace("/", "\\");
#endif
			return MTool;
		}

		TCVector<CStr> fg_ToolsRunning(CP4VTools const &_Tools, CStr const &_MTool, bool _bRunning)
		{
			TCVector<CStr> Tools;
			for (auto const &Tool : _Tools.m_Tools)
			{
				if ((Tool.f_Find(CStr(": {} "_f << _MTool)) >= 0) == _bRunning)
					Tools.f_Insert(Tool);
			}
			return Tools;
		}

		constexpr umint gc_nManagedTools = 25;

		// A file of the old install kit's tools, in the layout P4V writes, with the user's own tools among them: at the top, in the
		// Merge folder, and ones that run MTool with other arguments.
		constexpr CStr gc_ExistingFile = gc_Str
			<
				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<!--perforce-xml-version=1.0-->\n"
				"<CustomToolDefList varName=\"customtooldeflist\">\n"
				" <CustomToolDef>\n"
				"  <Definition>\n"
				"   <Name>My tool</Name>\n"
				"   <Command>/usr/bin/env</Command>\n"
				"   <Arguments>echo %f</Arguments>\n"
				"  </Definition>\n"
				" </CustomToolDef>\n"
				" <Separator/>\n"
				" <CustomToolDef>\n"
				"  <Definition>\n"
				"   <Name>Run script</Name>\n"
				"   <Command>X:\\Development\\Malterlib\\Tools\\MTool\\Windows\\MTool_x64.exe</Command>\n"
				"   <Arguments>PerforceRunScript \"File=%f\"</Arguments>\n"
				"  </Definition>\n"
				" </CustomToolDef>\n"
				" <CustomToolDef>\n"
				"  <Definition>\n"
				"   <Name>Convert to Git</Name>\n"
				"   <Command>/Development/Malterlib/Tools/MTool/OSX/x64/MTool</Command>\n"
				"   <Arguments>PerforceConvertChangelistsToGit SearchPath=//...</Arguments>\n"
				"  </Definition>\n"
				" </CustomToolDef>\n"
				" <CustomToolFolder>\n"
				"  <Name>Empty</Name>\n"
				"  <CustomToolDefList/>\n"
				" </CustomToolFolder>\n"
				" <CustomToolFolder>\n"
				"  <Name>Merge</Name>\n"
				"  <AddToContext>true</AddToContext>\n"
				"  <CustomToolDefList>\n"
				"   <CustomToolDef>\n"
				"    <Definition>\n"
				"     <Name>Merge stream to parent</Name>\n"
				"     <Command>/Development/Malterlib/Tools/MTool/OSX/x64/MTool</Command>\n"
				"     <Arguments>PerforceMerge \"Stream=%t\" Direction=Parent</Arguments>\n"
				"    </Definition>\n"
				"   </CustomToolDef>\n"
				"   <CustomToolDef>\n"
				"    <Definition>\n"
				"     <Name>My merge</Name>\n"
				"     <Command>/usr/bin/env</Command>\n"
				"     <Arguments>echo %t</Arguments>\n"
				"    </Definition>\n"
				"   </CustomToolDef>\n"
				"  </CustomToolDefList>\n"
				" </CustomToolFolder>\n"
				" <CustomToolFolder>\n"
				"  <Name>Changelist</Name>\n"
				"  <CustomToolDefList>\n"
				"   <CustomToolDef>\n"
				"    <Definition>\n"
				"     <Name>Take Ownership</Name>\n"
				"     <Command>/Development/Malterlib/Tools/MTool/OSX/x64/MTool</Command>\n"
				"     <Arguments>PerforceTakeOwnership \"ChangeList=%c\"</Arguments>\n"
				"    </Definition>\n"
				"   </CustomToolDef>\n"
				"  </CustomToolDefList>\n"
				" </CustomToolFolder>\n"
				"</CustomToolDefList>\n"
			>
		;
	}

	struct CInstallP4VTools_Tests : CTest
	{
		void f_DoTests();
	};

	void CInstallP4VTools_Tests::f_DoTests()
	{
		DMibTestSuite("InstallP4VTools") -> TCFuture<void>
		{
			auto Capture = co_await (g_CaptureExceptions % "Testing install-p4v-tools");

			// Without a file the directory and the file are made, in the layout of the template.
			DMibTestCategory("New") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CStr File = Repo.m_Root / "home/.p4qt/customtools.xml";
				auto Result = co_await Repo.f_Tool({"install-p4v-tools", "--no-color", "--file", File}, true);
				DMibAssert(Result.m_ExitCode, ==, 0u);
				DMibExpect(Result.f_GetCombinedOut().f_Find(CStr("Installed the P4V custom tools in {}"_f << File)), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(File, true).f_Find("<!--perforce-xml-version=1.0-->"), >=, 0);
				DMibExpectFalse(CFile::fs_FileExists(File + ".bak"));

				CP4VTools Tools = fg_ReadToolsFile(File);
				DMibExpect(fg_ToolsRunning(Tools, fg_ExpectedMTool(Repo), true).f_GetLen(), ==, gc_nManagedTools);
				DMibExpect(fg_ToolsRunning(Tools, fg_ExpectedMTool(Repo), false), ==, TCVector<CStr>{});
				DMibExpect
					(
						Tools.m_Layout
						, ==
						,
						(
							TCVector<CStr>
							{
								"Run script"
								, "Submit in stream"
								, "Separator"
								, "Switch to stream"
								, "Switch to stream and owned"
								, "Initialize stream workspace"
								, "Initialize all stream workspaces"
								, "Separator"
								, "Merge"
								, "Streams"
								, "Changelist"
							}
						)
					)
				;

				co_return {};
			};

			// The user's tools are kept, the old install's tools are replaced, the previous file is kept as a backup, and an install
			// that changes nothing leaves the file and its backup as they are.
			DMibTestCategory("Merge") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CStr File = Repo.m_Root / "home/Library/Preferences/com.perforce.p4v/customtools.xml";
				CFile::fs_CreateDirectoryForFile(File);
				CFile::fs_WriteStringToFile(File, gc_ExistingFile, false);

				auto Result = co_await Repo.f_Tool({"install-p4v-tools", "--no-color", "--file", File}, true);
				CStr Output = Result.f_GetCombinedOut();
				DMibAssert(Result.m_ExitCode, ==, 0u);
				DMibExpect(Output.f_Find(CStr("Updated the P4V custom tools in {}"_f << File)), >=, 0);
				DMibExpect(Output.f_Find(CStr("The previous file is kept as {}.bak"_f << File)), >=, 0);
				DMibExpect(Output.f_Find("Restart P4V"), >=, 0);
				DMibExpect(CFile::fs_ReadStringFromFile(File + ".bak", true), ==, gc_ExistingFile);

				CP4VTools Tools = fg_ReadToolsFile(File);
				CStr MTool = fg_ExpectedMTool(Repo);
				DMibExpect(fg_ToolsRunning(Tools, MTool, true).f_GetLen(), ==, gc_nManagedTools);
				DMibExpect
					(
						fg_ToolsRunning(Tools, MTool, false)
						, ==
						,
						(
							TCVector<CStr>
							{
								"My tool: /usr/bin/env echo %f"
								, "Convert to Git: /Development/Malterlib/Tools/MTool/OSX/x64/MTool PerforceConvertChangelistsToGit SearchPath=//..."
								, "Merge/My merge: /usr/bin/env echo %t"
							}
						)
					)
				;

				// The kept Merge folder takes the new merge tools, the emptied Changelist folder is replaced, and the user's empty folder is
				// kept.
				DMibExpect
					(
						Tools.m_Layout
						, ==
						,
						(
							TCVector<CStr>
							{
								"My tool"
								, "Separator"
								, "Convert to Git"
								, "Empty"
								, "Merge"
								, "Separator"
								, "Run script"
								, "Submit in stream"
								, "Separator"
								, "Switch to stream"
								, "Switch to stream and owned"
								, "Initialize stream workspace"
								, "Initialize all stream workspaces"
								, "Separator"
								, "Streams"
								, "Changelist"
							}
						)
					)
				;

				CStr FirstInstall = CFile::fs_ReadStringFromFile(File, true);
				auto Again = co_await Repo.f_Tool({"install-p4v-tools", "--no-color", "--file", File}, true);
				DMibAssert(Again.m_ExitCode, ==, 0u);
				DMibExpect(Again.f_GetCombinedOut().f_Find("are up to date"), >=, 0);
				CStr SecondInstall = CFile::fs_ReadStringFromFile(File, true);
				CStr KeptBackup = CFile::fs_ReadStringFromFile(File + ".bak", true);
				DMibExpect(SecondInstall, ==, FirstInstall);
				DMibExpect(KeptBackup, ==, gc_ExistingFile);

				co_return {};
			};

			co_return {};
		};
	}

	DMibTestRegister(CInstallP4VTools_Tests, Malterlib::Tool);
}
