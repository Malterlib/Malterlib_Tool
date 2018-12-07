// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Process/VirtualProcessLaunch>

namespace
{
	CRegistry_CStr fg_ExtractOptions(NContainer::CRegistry_CStr &_Params)
	{
		CRegistry_CStr Registry;
		
		CStr LastOption;
		
		for (mint i = 0; true; ++i)
		{
			auto pChild = _Params.f_GetChildNoPath(CStr::fs_ToStr(i));
			if (!pChild)
				break;
			CStr Value = pChild->f_GetThisValue();
			
			if (Value.f_StartsWith("-"))
			{
				if (!LastOption.f_IsEmpty())
				{
					Registry.f_SetValueNoPath(LastOption, "");
				}
				LastOption = Value;
			}
			else
			{
				if (LastOption.f_IsEmpty())
					Registry.f_CreateChild("Files")->f_CreateChildNoPath(Value, true);
				else
					Registry.f_SetValueNoPath(LastOption, Value);
				
				LastOption.f_Clear();
			}
		}
		
		if (!LastOption.f_IsEmpty())
			Registry.f_SetValueNoPath(LastOption, "");
		
		return Registry;
	}
}
class CTool_DoxygenCompile : public CTool
{
public:

	aint f_Run(NContainer::CRegistry_CStr &_Params)
	{
		CRegistry_CStr Registry = fg_ExtractOptions(_Params);

		auto pFile = Registry.f_GetChild("-c");
		if (pFile)
			Registry.f_CreateChild("Files")->f_CreateChildNoPath(pFile->f_GetThisValue(), true);

//		DConOut("\n{}\n", Registry.f_GenerateStr());
		
		CStr OutputFile = Registry.f_GetValue("-o", "");
		if (OutputFile.f_IsEmpty())
			DError("No output file found in command line options");
		{
			//DConOut("Writing to: {}{\n}", OutputFile);
			CStr Data = Registry.f_GenerateStr();
			CFile::fs_WriteStringToFile(CStr(OutputFile), Data);
		}
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DoxygenCompile);

class CTool_DoxygenLibTool : public CTool
{
public:

	aint f_Run(NContainer::CRegistry_CStr &_Params)
	{
		CRegistry_CStr Registry = fg_ExtractOptions(_Params);

		auto pFilesReg = Registry.f_CreateChild("Files");
	
		auto pFileFile = Registry.f_GetChild("-filelist");
		if (pFileFile)
		{
			CStr FileContents = CFile::fs_ReadStringFromFile(CStr(pFileFile->f_GetThisValue()));
			while (!FileContents.f_IsEmpty())
			{
				CStr FileName = fg_GetStrLineSep(FileContents);
				if (CFile::fs_GetExtension(FileName).f_CmpNoCase("o") == 0)
					pFilesReg->f_CreateChildNoPath(FileName, true);
			}
		}

//		DConOut("\n{}\n", Registry.f_GenerateStr());
		
		CStr OutputFile = Registry.f_GetValue("-o", "");
		if (OutputFile.f_IsEmpty())
			DError("No output file found in command line options");
		
		{
			//DConOut("Writing to: {}{\n}", OutputFile);
			CStr Data = Registry.f_GenerateStr();
			CFile::fs_WriteStringToFile(CStr(OutputFile), Data);
		}
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DoxygenLibTool);


class CTool_DoxygenLD : public CTool
{
public:

	CStr m_DoxygenExecutable;
	CStr m_DoxygenInclude;
	CStr m_DoxygenTagInclude;
	CStr m_DoxygenRoot;
	TCSet<CStr> m_DoxygenImageExtensions;
	CStr m_OutputDir;
	bool m_bDoxygenEnableClang = false;
	
	TCLinkedList<CRegistry_CStr> m_Libraries;
	TCMap<CStr, CStr> m_Tags;
	
	struct CModule
	{
		TCSet<CStr> m_IncludePaths;
		TCSet<CStr> m_Predefines;
		TCMap<CStr, CStr> m_ClangOptions;
		TCSet<CStr> m_Inputs;
		TCSet<CStr> m_Images;
		CStr m_DocsetFeedname;
		CStr m_DocsetBundleID;
		CStr m_DocsetPublisherID;
		CStr m_DocsetPublisherName;
	};

	TCMap<CStr, CModule> m_Modules;
	
	void f_ExtractModules()
	{
		for (auto &Library : m_Libraries)
		{
			auto pFilesReg = Library.f_CreateChild("Files");
			
			for (auto iFile = pFilesReg->f_GetChildIterator(); iFile; ++iFile)
			{
				CRegistry_CStr const &ObjectRegistry = *iFile;
				
				CStr ModuleName = ObjectRegistry.f_GetValue("--documentation-module");
				
				auto &Module = m_Modules[ModuleName];
				
				Module.m_DocsetFeedname = ObjectRegistry.f_GetValue("--documentation-docset-feedname", "");
				Module.m_DocsetBundleID = ObjectRegistry.f_GetValue("--documentation-docset-bundle-id", "");
				Module.m_DocsetPublisherID = ObjectRegistry.f_GetValue("--documentation-docset-publisher-id", "");
				Module.m_DocsetPublisherName = ObjectRegistry.f_GetValue("--documentation-docset-publisher-name", "");

				bool bValid = false;
				auto *pFiles = ObjectRegistry.f_GetChild("Files");
				if (pFiles)
				{
					for (auto iFile = pFiles->f_GetChildIterator(); iFile; ++iFile)
					{
						if (!m_DoxygenRoot.f_IsEmpty() && !iFile->f_GetName().f_StartsWith(m_DoxygenRoot))
							continue;
						
						bValid = true;
						
						auto &FileName = iFile->f_GetName();
						if (m_DoxygenImageExtensions.f_FindEqual(CFile::fs_GetExtension(FileName)))
							Module.m_Images[FileName];
						else
							Module.m_Inputs[FileName];
					}
				}
				
				if (!bValid)
					continue;			
				
				for (auto iConfig = ObjectRegistry.f_GetChildIterator(); iConfig; ++iConfig)
				{
					if (iConfig->f_HasChildren())
						continue;
					CStr Name = iConfig->f_GetName();
					if (Name.f_StartsWith("--documentation-"))
						continue;
					if (Name.f_StartsWith("-I"))
					{
						CStr Path = Name.f_Extract(2);
						Module.m_IncludePaths[CFile::fs_GetExpandedPath(Path)];
					}
					else
					{
						if 
						(
							Name != "-o" 
							&& Name != "-c" 
							&& Name != "--serialize-diagnostics" 
							&& Name != "-MF" 
							&& Name != "-MMD" 
							&& Name != "-MT" 
							&& Name != "-g" 
							&& Name != "-x"
							&& Name != "-Werror"
							&& Name != "-target"
							&& !Name.f_StartsWith("-O") 
						)
							Module.m_ClangOptions[Name] = iConfig->f_GetThisValue();
					}
					if (Name.f_StartsWith("-D"))
					{
						CStr Define = Name.f_Extract(2);
						Module.m_Predefines[Define];
					}
				}
				
			}
		}
	}

	void f_LaunchModules(bool _bGenerateTags, CStr const &_IncludeFile)
	{
		CProcessLaunchHandler Handler;
		
		CMutual OutputLock;
		TCAtomic<uint32> ExitCode(0);

		auto fAddModules
			= [&](bool _bGenerateDocset)
			{
				for (auto &Module : m_Modules)
				{
					TCVector<CStr> LaunchParams;
					
					struct CState
					{
						CStr m_StdOut;
						CStr m_StdErr;
					};
					
					TCSharedPointer<CState> pState = fg_Construct();

					{
						CStr DoxygenFileContents;
						
						if (!_IncludeFile.f_IsEmpty())
							DoxygenFileContents += CStr::CFormat("@INCLUDE={}\n") << _IncludeFile;

						CStr LibraryName = m_Modules.fs_GetKey(Module);
						
						DoxygenFileContents += CStr::CFormat("PROJECT_NAME = {}\n") << LibraryName;
						
						CStr OutputDir = m_OutputDir + "/" + LibraryName;
						if (_bGenerateDocset)
							OutputDir = CFile::fs_GetPath(m_OutputDir) + "/DocSetsTemp/" + LibraryName;
						
						CFile::fs_CreateDirectory(OutputDir);

						CStr DoxygenConfigFile = CFile::fs_GetPath(m_OutputDir) + "/" + LibraryName + ".doxygen";
						if (_bGenerateDocset)
							DoxygenConfigFile = CFile::fs_GetPath(m_OutputDir) + "/" + LibraryName + "Docset.doxygen";
						
						DoxygenFileContents += CStr::CFormat("OUTPUT_DIRECTORY={}\n") << OutputDir;
						if (_bGenerateTags)
						{
							CStr DoxygenTagOutput = CFile::fs_GetPath(m_OutputDir) + "/" + LibraryName + ".tag";
							DoxygenFileContents += CStr::CFormat("GENERATE_TAGFILE = {}\n") << DoxygenTagOutput;
							m_Tags[DoxygenTagOutput] = CStr::CFormat("../../{}/html") << LibraryName;
						}
						else
						{
							CStr DoxygenTagOutput = CFile::fs_GetPath(m_OutputDir) + "/" + LibraryName + ".tag";
							DoxygenFileContents += "TAGFILES	= \\\n";
							for (auto iTag = m_Tags.f_GetIterator(); iTag; ++iTag)
							{
								if (iTag.f_GetKey() != DoxygenTagOutput)
									DoxygenFileContents += CStr::CFormat("	{}={} \\\n") << iTag.f_GetKey() << *iTag;
							}
							DoxygenFileContents += "\n";
						}
						if (!m_DoxygenRoot.f_IsEmpty())
						{
							DoxygenFileContents += CStr::CFormat("STRIP_FROM_INC_PATH = {}\n") << m_DoxygenRoot;
							DoxygenFileContents += CStr::CFormat("STRIP_FROM_PATH = {}\n") << m_DoxygenRoot;
						}
								
						if (!Module.m_DocsetBundleID.f_IsEmpty())
						{
							if (_bGenerateDocset)
							{
								DoxygenFileContents += "GENERATE_DOCSET   = YES\n";
								DoxygenFileContents += "DISABLE_INDEX   = YES\n";
								DoxygenFileContents += "SEARCHENGINE   = NO\n";
								DoxygenFileContents += "GENERATE_TREEVIEW   = NO\n";
								DoxygenFileContents += CStr::CFormat("DOCSET_BUNDLE_ID = {}\n") << Module.m_DocsetBundleID;
								if (!Module.m_DocsetFeedname.f_IsEmpty())
									DoxygenFileContents += CStr::CFormat("DOCSET_FEEDNAME = {}\n") << Module.m_DocsetFeedname;
								if (!Module.m_DocsetPublisherID.f_IsEmpty())
									DoxygenFileContents += CStr::CFormat("DOCSET_PUBLISHER_ID = {}\n") << Module.m_DocsetPublisherID;
								if (!Module.m_DocsetPublisherName.f_IsEmpty())
									DoxygenFileContents += CStr::CFormat("DOCSET_PUBLISHER_NAME = {}\n") << Module.m_DocsetPublisherName;
							}
						}
						else if (_bGenerateDocset)
							continue;
						
						CStr ImagePath = CFile::fs_GetPath(m_OutputDir) + "/Images";
						CFile::fs_CreateDirectory(ImagePath);
						DoxygenFileContents += CStr::CFormat("IMAGE_PATH	= {}\n") << ImagePath;
						for (auto &FileName : Module.m_Images)
							CFile::fs_CopyFile(FileName, CFile::fs_AppendPath(ImagePath, CFile::fs_GetFile(FileName)));
						
						DoxygenFileContents += "INPUT	= \\\n";

						for (auto &FileName : Module.m_Inputs)
							DoxygenFileContents += CStr::CFormat("	{} \\\n") << FileName;
						DoxygenFileContents += "\n";
						
						DoxygenFileContents += "PREDEFINED	= \\\n";

						for (auto &Define : Module.m_Predefines)
							DoxygenFileContents += CStr::CFormat("	{} \\\n") << Define;
						DoxygenFileContents += "\n";

						{
							DoxygenFileContents += "INCLUDE_PATH	= \\\n";
							for (auto &Name : Module.m_IncludePaths)
							{
								if (CFile::fs_FileExists(Name, EFileAttrib_Directory))
									DoxygenFileContents += CStr::CFormat("	{} \\\n") << Name;
							}
							DoxygenFileContents += "\n";
						}

						if (m_bDoxygenEnableClang)
						{
							CStr AllOptions;
							for (auto iOption = Module.m_ClangOptions.f_GetIterator(); iOption; ++iOption)
							{
								fg_AddStrSepEscaped(AllOptions, iOption.f_GetKey(), ' ');
								if (!iOption->f_IsEmpty())
									fg_AddStrSepEscaped(AllOptions, *iOption, ' ');
							}
							DoxygenFileContents += CStr::CFormat("CLANG_OPTIONS	= {}") << AllOptions;
							DoxygenFileContents += "CLANG_ASSISTED_PARSING   = YES\n";
						}

		//				DConOut("DoxygenFileContents: \n{}\n", DoxygenFileContents);
						
						CFile::fs_WriteStringToFile(CStr(DoxygenConfigFile), DoxygenFileContents);
						LaunchParams.f_Insert(DoxygenConfigFile);				
					}			

					CProcessLaunchParams Params = CProcessLaunchParams::fs_LaunchExecutable
						(
							m_DoxygenExecutable
							, LaunchParams
							, CFile::fs_GetPath(m_OutputDir)
							, [&, pState](CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
							{
								if (_State.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
								{
									DConErrOut("Error launching doxygen: {}\n", _State.f_Get<EProcessLaunchState_LaunchFailed>());
								}
								else if (_State.f_GetTypeID() == EProcessLaunchState_Exited)
								{
									uint32 ThisExit = _State.f_Get<EProcessLaunchState_Exited>();
									if (ThisExit)
										ExitCode.f_Exchange(ThisExit);

									{
										DLock(OutputLock);
										DConErrOutRaw(pState->m_StdErr);
									}
								}
							}
						)
					;
					
					Params.m_fOnOutput = [&, pState](EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
						{
							DLock(OutputLock);
							if (_OutputType == EProcessLaunchOutputType_StdOut)
								DConOutRaw(_Output);
							else
								pState->m_StdErr += _Output;
					
						}
					;
					
					Params.m_bMergeEnvironment = true;

					Handler.f_AddLaunch(Params, false);
				}
				
			}
		;
		
		fAddModules(false);
		fAddModules(true);
		
		Handler.f_BlockOnExit();

		if (ExitCode.f_Load())
			DError(fg_Format("doxygen exited with code: {}", ExitCode.f_Load()));
	}

	void f_GenerateDocsets()
	{
		CProcessLaunchHandler Handler;
		
		CMutual OutputLock;
		TCAtomic<uint32> ExitCode(0);

		for (auto &Module : m_Modules)
		{
			if (Module.m_DocsetBundleID.f_IsEmpty())
				continue;
			
			TCVector<CStr> LaunchParams;
			
			struct CState
			{
				CStr m_StdOut;
				CStr m_StdErr;
			};
			
			TCSharedPointer<CState> pState = fg_Construct();

			CStr LibraryName = m_Modules.fs_GetKey(Module);
			CStr OutputDir = CFile::fs_GetPath(m_OutputDir) + "/DocSetsTemp/" + LibraryName;

			DConOut("Launching make from: {}\n", OutputDir);
			
			CProcessLaunchParams Params = CProcessLaunchParams::fs_LaunchExecutable
				(
					"make"
					, LaunchParams
					, OutputDir
					, [&, pState](CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
					{
						if (_State.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
						{
							DConErrOut("Error launching make: {}\n", _State.f_Get<EProcessLaunchState_LaunchFailed>());
						}
						else if (_State.f_GetTypeID() == EProcessLaunchState_Exited)
						{
							uint32 ThisExit = _State.f_Get<EProcessLaunchState_Exited>();
							if (ThisExit)
								ExitCode.f_Exchange(ThisExit);

							{
								DLock(OutputLock);
								DConErrOutRaw(pState->m_StdErr);
							}
						}
					}
				)
			;
			
			Params.m_fOnOutput = [&, pState](EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
				{
					DLock(OutputLock);
					if (_OutputType == EProcessLaunchOutputType_StdOut)
						DConOutRaw(_Output);
					else
						pState->m_StdErr += _Output;
			
				}
			;
			
			Params.m_bMergeEnvironment = true;
			Params.m_bAllowExecutableLocate = true;

			Handler.f_AddLaunch(Params, false);
		}
		
		Handler.f_BlockOnExit();

		if (ExitCode.f_Load())
			DError(fg_Format("make exited with code: {}", ExitCode.f_Load()));
		
		for (auto &Module : m_Modules)
		{
			if (Module.m_DocsetBundleID.f_IsEmpty())
				continue;
			
			CStr LibraryName = m_Modules.fs_GetKey(Module);
			CStr OutputDir = CFile::fs_GetPath(m_OutputDir) + "/DocSetsTemp/" + LibraryName;
			CStr OutputDirDest = m_OutputDir + "/" + LibraryName;
			CStr DocSetName = Module.m_DocsetBundleID + ".docset";

			CFile::fs_DiffCopyFileOrDirectory(OutputDir + "/" + DocSetName, OutputDirDest + "/" + DocSetName, fg_Default());
		 }
	}
	
	aint f_Run(NContainer::CRegistry_CStr &_Params)
	{
		CRegistry_CStr Registry = fg_ExtractOptions(_Params);
		
		CStr OutputFile = Registry.f_GetValue("-o", "");
		if (OutputFile.f_IsEmpty())
			DError("No output file found in command line options");
		
		auto &LocalLibrary = m_Libraries.f_Insert();
		
		LocalLibrary.f_SetValue("-o", OutputFile);

		auto pLocalFilesReg = LocalLibrary.f_CreateChild("Files");
		
		TCSet<CStr> Files;
		
		auto pFileFile = Registry.f_GetChild("-filelist");
		if (pFileFile)
		{
			CStr FileContents = CFile::fs_ReadStringFromFile(CStr(pFileFile->f_GetThisValue()));
			while (!FileContents.f_IsEmpty())
			{
				CStr FileName = fg_GetStrLineSep(FileContents);
				Files[FileName];
			}
		}
		
		{
			auto pFiles = Registry.f_GetChildNoPath("Files");
			for (auto iFile = pFiles->f_GetChildIterator(); iFile; ++iFile)
				Files[iFile->f_GetName()];
		}
		
		for (auto &FileName : Files)
		{
			CStr Extension = CFile::fs_GetExtension(FileName);
			if (Extension.f_CmpNoCase("a") == 0)
			{
				auto &Launch = m_Libraries.f_Insert();
				Launch.f_ParseStr(CFile::fs_ReadStringFromFile(CStr(FileName)), FileName);

				auto pFiles = Launch.f_GetChildNoPath("Files");
				if (pFiles)
				{
					for (auto iFile = pFiles->f_GetChildIterator(); iFile; ++iFile)
					{
						iFile->f_ParseStr(CFile::fs_ReadStringFromFile(CStr(iFile->f_GetName())), iFile->f_GetName());
					}
				}
			}
			else if (Extension.f_CmpNoCase("o") == 0)
			{
				auto pChild = pLocalFilesReg->f_CreateChildNoPath(FileName, true);
				pChild->f_ParseStr(CFile::fs_ReadStringFromFile(CStr(FileName)), FileName);
			}
		}
		
		m_OutputDir = OutputFile;
		CFile::fs_CreateDirectory(OutputFile);
		
		m_DoxygenExecutable = Registry.f_GetValue("--doxygen-executable", "");
		if (m_DoxygenExecutable.f_IsEmpty())
			DError("No doxygen executable specified");
		
		m_DoxygenInclude = Registry.f_GetValue("--doxygen-include", "");
		m_DoxygenTagInclude = Registry.f_GetValue("--doxygen-tag-include", "");
		m_DoxygenRoot = Registry.f_GetValue("--doxygen-root", "");
		m_bDoxygenEnableClang = Registry.f_GetValue("--doxygen-enable-clang", "") == "true";
		
		{
			CStr ImageExtensions = Registry.f_GetValue("--doxygen-image-extensions", "");
			while (!ImageExtensions.f_IsEmpty())
				m_DoxygenImageExtensions[fg_GetStrSep(ImageExtensions, ";")];
		}

		
//		DConOut("\n{}\n", Registry.f_GenerateStr());
/*
		for (auto &Launch : m_Libraries)
		{
			DConOut("\n{}\n", Launch.f_GenerateStr());
		}
*/
		f_ExtractModules();
		
		f_LaunchModules(true, m_DoxygenTagInclude);
		f_LaunchModules(false, m_DoxygenInclude);
		f_GenerateDocsets();
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DoxygenLD);



