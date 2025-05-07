// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Process/VirtualProcessLaunch>

namespace
{
	CRegistry fg_ExtractOptions(NContainer::CRegistry &_Params)
	{
		CRegistry Registry;

		CStr LastOption;

		for (mint i = 0; true; ++i)
		{
			auto pChild = _Params.f_GetChildNoPath(CStr::fs_ToStr(i));
			if (!pChild)
				break;
			CStr Value = pChild->f_GetThisValue();

			if (Value == "-Xlinker")
				continue;

			if (Value.f_StartsWith("-") && LastOption != "--doxygen-options")
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
				{
					auto pFilesReg = Registry.f_CreateChild("Files");
					if (CFile::fs_GetExtension(Value).f_CmpNoCase("a") == 0)
					{
						CRegistry LibraryContents;
						LibraryContents.f_ParseStr(CFile::fs_ReadStringFromFile(CStr(Value)), Value);

						auto pFiles = LibraryContents.f_GetChildNoPath("Files");
						if (pFiles)
						{
							for (auto iFile = pFiles->f_GetChildIterator(); iFile; ++iFile)
								pFilesReg->f_CreateChildNoPath(iFile->f_GetName(), true);
						}
					}
					else
						pFilesReg->f_CreateChildNoPath(Value, true);
				}
				else
					Registry.f_SetValueNoPath(LastOption, Value);

				LastOption.f_Clear();
			}
		}

		if (!LastOption.f_IsEmpty())
			Registry.f_SetValueNoPath(LastOption, "");

		return Registry;
	}

	struct CDependencyFile
	{
		CBinaryStreamMemory<> m_Stream;

		void f_AddDependencyInfoHelper(uint8 _Opcode, CStr const &_Path)
		{
			m_Stream << _Opcode;
			m_Stream.f_FeedBytes((uint8 const *)_Path.f_GetStr(), _Path.f_GetLen());
			m_Stream << uint8(0);
		}

		void f_AddTool(CStr const &_Name)
		{
			f_AddDependencyInfoHelper(0x00, _Name);
		}

		void f_AddInput(CStr const &_Path)
		{
			f_AddDependencyInfoHelper(0x10, _Path);
		}

		void f_AddNotFound(CStr const &_Path)
		{
			f_AddDependencyInfoHelper(0x11, _Path);
		}

		void f_AddOutput(CStr const &_Path)
		{
			f_AddDependencyInfoHelper(0x40, _Path);
		}
	};
}

namespace
{
	constexpr uint8 gc_EmptySerializedDiagnostics[] =
		{
			  0x44, 0x49, 0x41, 0x47, 0x01, 0x08, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x07, 0x01, 0xb2, 0x40
			, 0xb4, 0x42, 0x39, 0xd0, 0x43, 0x38, 0x3c, 0x20, 0x81, 0x2d, 0x94, 0x83, 0x3c, 0xcc, 0x43, 0x3a
			, 0xbc, 0x83, 0x3b, 0x1c, 0x04, 0x88, 0x62, 0x80, 0x40, 0x71, 0x10, 0x24, 0x0b, 0x04, 0x29, 0xa4
			, 0x43, 0x38, 0x9c, 0xc3, 0x43, 0x22, 0x90, 0x42, 0x3a, 0x84, 0xc3, 0x39, 0xa4, 0x82, 0x3b, 0x98
			, 0xc3, 0x3b, 0x3c, 0x24, 0xc3, 0x2c, 0xc8, 0xc3, 0x38, 0xc8, 0x42, 0x38, 0xb8, 0xc3, 0x39, 0x94
			, 0xc3, 0x03, 0x52, 0x8c, 0x42, 0x38, 0xd0, 0x83, 0x2b, 0x84, 0x43, 0x3b, 0x94, 0xc3, 0x43, 0x42
			, 0x90, 0x42, 0x3a, 0x84, 0xc3, 0x39, 0x98, 0x02, 0x3b, 0x84, 0xc3, 0x39, 0x3c, 0x24, 0x86, 0x29
			, 0xa4, 0x03, 0x3b, 0x94, 0x83, 0x2b, 0x84, 0x43, 0x3b, 0x94, 0xc3, 0x83, 0x71, 0x98, 0x42, 0x3a
			, 0xe0, 0x43, 0x2a, 0xd0, 0xc3, 0x41, 0x90, 0xa8, 0x0a, 0xc8, 0x10, 0x25, 0x50, 0x08, 0x14, 0x02
			, 0x85, 0x28, 0x51, 0x04, 0x83, 0x4a, 0x16, 0x08, 0x0c, 0x82, 0xd4, 0x74, 0x40, 0x94, 0x40, 0x21
			, 0x50, 0x08, 0x14, 0xa2, 0x04, 0x0a, 0x81, 0x42, 0xa0, 0x90, 0x24, 0x10, 0x25, 0x30, 0xa8, 0xa6
			, 0x81, 0x28, 0x81, 0x42, 0xa0, 0x10, 0x18, 0xd4, 0xf5, 0x40, 0x94, 0x40, 0x21, 0x50, 0x08, 0x14
			, 0xa2, 0x04, 0x0a, 0x81, 0x42, 0xa0, 0x10, 0x18, 0x14, 0x00, 0x00, 0x00, 0x21, 0x0c, 0x00, 0x00
			, 0x02, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		}
	;
}

class CTool_DoxygenCompile : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CRegistry Registry = fg_ExtractOptions(_Params);

		auto pFile = Registry.f_GetChild("-c");
		if (pFile)
			Registry.f_CreateChild("Files")->f_CreateChildNoPath(pFile->f_GetThisValue(), true);

//		DConOut("\n{}\n", Registry.f_GenerateStr());
		CStr DiagFile = Registry.f_GetValue("--serialize-diagnostics", "");

		if (DiagFile)
		{
			CFile::fs_CreateDirectory(CFile::fs_GetPath(DiagFile));
			CFile::fs_WriteFile(DiagFile, CByteVector(gc_EmptySerializedDiagnostics, sizeof(gc_EmptySerializedDiagnostics)));
		}

		CStr OutputFile = Registry.f_GetValue("-o", "");
		if (OutputFile.f_IsEmpty())
			DError("No output file found in command line options");
		{
			//DConOut("Writing to: {}{\n}", OutputFile);
			CStr Data = Registry.f_GenerateStr();
			CFile::fs_WriteStringToFile(CStr(OutputFile), Data);
		}

		if (auto pDependenciesFile = Registry.f_GetChild("-MF"))
			CFile::fs_Touch(pDependenciesFile->f_GetThisValue());

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DoxygenCompile);

class CTool_DoxygenLibTool : public CDistributedTool
{
public:
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		)
	{
		if (!fg_IsLibTool())
			return;

		o_ToolsSection.f_RegisterDirectCommand
			(
				{
					"Names"_o= _o["-V"]
					, "Description"_o= "Libtool version.\n"
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "GreedyDefaultCommand"_o= true
				}
				, [](NEncoding::CEJsonSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					DMibConOut2("Apple Inc. version cctools-1010.6\n");
					return 0;
				}
			)
		;

		auto LibToolCommand = o_ToolsSection.f_RegisterDirectCommand
			(
				{
					"Names"_o= _o["libtool"]
					, "Description"_o= "Runs libtool command line.\n"
					, "Parameters"_o=
					{
						"Params...?"_o=
						{
							"Type"_o= _o[""]
							, "Description"_o= "The cmake params."
						}
					}
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "GreedyDefaultCommand"_o= true
				}
				, [](NEncoding::CEJsonSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					CDependencyFile DependencyFile;
					DependencyFile.f_AddTool("libtool");


					CRegistry Params;

					if (auto pParams = _Params.f_GetMember("Params"))
					{
						mint iParam = 0;
						for (auto &Param : pParams->f_Array())
						{
							Params.f_SetValue(CStr::fs_ToStr(iParam), Param.f_String());
							++iParam;
						}
					}

					CRegistry Registry = fg_ExtractOptions(Params);

					auto pFilesReg = Registry.f_CreateChild("Files");

					if (Registry.f_GetChild("-V"))
					{
						DMibConOut2("Apple Inc. version cctools-1010.6\n");
						return 0;
					}

					auto pFileFile = Registry.f_GetChild("-filelist");
					if (pFileFile)
					{
						CStr FileContents = CFile::fs_ReadStringFromFile(CStr(pFileFile->f_GetThisValue()));
						while (!FileContents.f_IsEmpty())
						{
							CStr FileName = fg_GetStrLineSep(FileContents);
							DependencyFile.f_AddInput(FileName);

							if (CFile::fs_GetExtension(FileName).f_CmpNoCase("o") == 0)
								pFilesReg->f_CreateChildNoPath(FileName, true);
							else if (CFile::fs_GetExtension(FileName).f_CmpNoCase("a") == 0)
							{
								CRegistry LibraryContents;
								LibraryContents.f_ParseStr(CFile::fs_ReadStringFromFile(CStr(FileName)), FileName);

								auto pFiles = LibraryContents.f_GetChildNoPath("Files");
								if (pFiles)
								{
									for (auto iFile = pFiles->f_GetChildIterator(); iFile; ++iFile)
									{
										DependencyFile.f_AddInput(iFile->f_GetName());
										pFilesReg->f_CreateChildNoPath(iFile->f_GetName(), true);
									}
								}
							}
						}
					}

					CStr OutputFile = Registry.f_GetValue("-o", "");
					if (OutputFile.f_IsEmpty())
						DError("No output file found in command line options");

					DependencyFile.f_AddOutput(OutputFile);

					{
						CStr Data = Registry.f_GenerateStr();
						CFile::fs_WriteStringToFile(CStr(OutputFile), Data);
					}

					if (auto pDependenciesFile = Registry.f_GetChild("-dependency_info"))
						CFile::fs_WriteFile(pDependenciesFile->f_GetThisValue(), DependencyFile.m_Stream.f_MoveVector());

					return 0;
				}
			)
		;

		o_CommandLine.f_SetDefaultCommand(LibToolCommand);
	}
};

DMibRuntimeClass(CTool, CTool_DoxygenLibTool);

class CTool_DoxygenLD : public CTool
{
public:

	CDependencyFile m_DependencyFile;

	CStr m_DoxygenExecutable;
	CStr m_DoxygenInclude;
	CStr m_DoxygenTagInclude;
	CStr m_DoxygenRoot;
	TCSet<CStr> m_DoxygenImageExtensions;
	CStr m_OutputDir;
	bool m_bDoxygenEnableClang = false;

	TCVector<CStr> m_DoxygenOptions;

	TCLinkedList<CRegistry> m_Libraries;
	TCMap<CStr, CStr> m_Tags;

	struct CModule
	{
		TCSet<CStr> m_IncludePaths;
		TCSet<CStr> m_Predefines;
		TCMap<CStr, CStr> m_ClangOptions;
		TCSet<CStr> m_Inputs;
		TCSet<CStr> m_Images;
	};

	TCMap<CStr, CModule> m_Modules;

	void f_ExtractModules()
	{
		for (auto &Library : m_Libraries)
		{
			auto pFilesReg = Library.f_CreateChild("Files");

			for (auto iFile = pFilesReg->f_GetChildIterator(); iFile; ++iFile)
			{
				m_DependencyFile.f_AddInput(iFile->f_GetName());

				CRegistry const &ObjectRegistry = *iFile;

				CStr ModuleName = ObjectRegistry.f_GetValue("--documentation-module", "");

				if (ModuleName.f_IsEmpty())
					DMibError("Library '{}' File '{}' has no module specified"_f << Library.f_GetName() << iFile->f_GetName());

				auto &Module = m_Modules[ModuleName];

				bool bValid = false;
				auto *pFiles = ObjectRegistry.f_GetChild("Files");
				if (pFiles)
				{
					for (auto iFile = pFiles->f_GetChildIterator(); iFile; ++iFile)
					{
						m_DependencyFile.f_AddInput(iFile->f_GetName());

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

		auto fAddModules = [&]()
			{
				for (auto &Module : m_Modules)
				{
					TCVector<CStr> LaunchParams = m_DoxygenOptions;

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

						CFile::fs_CreateDirectory(OutputDir);

						CStr DoxygenConfigFile = CFile::fs_GetPath(m_OutputDir) + "/" + LibraryName + ".doxygen";

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

						//DConOut("DoxygenFileContents: \n{}\n", DoxygenFileContents);

						CFile::fs_WriteStringToFile(CStr(DoxygenConfigFile), DoxygenFileContents, false);
						LaunchParams.f_Insert(DoxygenConfigFile);
					}

					//DConOut2("Launching Doxygen at '{}' with: \n{}\n", CFile::fs_GetPath(m_OutputDir), LaunchParams);

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
					Params.m_Environment["PATH"] = "/opt/homebrew/sbin:/opt/homebrew/bin:/usr/local/sbin:/usr/local/bin:{}"_f << fg_GetSys()->f_GetEnvironmentVariable("PATH");

					Handler.f_AddLaunch(Params, false);
				}

			}
		;

		fAddModules();

		Handler.f_BlockOnExit();

		if (ExitCode.f_Load())
			DError(fg_Format("doxygen exited with code: {}", ExitCode.f_Load()));
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{
		m_DependencyFile.f_AddTool("ld64");

		CRegistry Registry = fg_ExtractOptions(_Params);

		CStr OutputFile = Registry.f_GetValue("-o", "");
		if (OutputFile.f_IsEmpty())
			DError("No output file found in command line options");

		auto &LocalLibrary = m_Libraries.f_Insert();

		m_DependencyFile.f_AddOutput(OutputFile);

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
			m_DependencyFile.f_AddInput(FileName);
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
				m_DoxygenImageExtensions[fg_GetStrSep(ImageExtensions, ",")];
		}
		{
			CStr Options = Registry.f_GetValue("--doxygen-options", "");
			while (!Options.f_IsEmpty())
				m_DoxygenOptions.f_Insert(fg_GetStrSep(Options, ","));
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

		NFile::CFile::fs_SetWriteTime(OutputFile, NTime::CTime::fs_NowUTC());

		if (auto pDependenciesFile = Registry.f_GetChild("-dependency_info"))
			CFile::fs_WriteFile(pDependenciesFile->f_GetThisValue(), m_DependencyFile.m_Stream.f_MoveVector());

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_DoxygenLD);



