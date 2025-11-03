// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

CStr CTool_Malterlib::fs_GetFileNameOrEmpty(NEncoding::CEJsonSorted const &_Param, CStr const &_CurrentDirectory)
{
	CStr FileName = _Param.f_String();
	if (!FileName)
		return FileName;
	return CFile::fs_GetExpandedPath(FileName, _CurrentDirectory);
}

CStr CTool_Malterlib::fs_DefaultGenerator(CStr const &_RootPath)
{
#if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Linux)
	CStr RepoConfigFile = _RootPath / "Repo.conf";
	CStr Version;

	if (CFile::fs_FileExists(RepoConfigFile))
	{
		for (auto Line : CFile::fs_ReadStringFromFile(RepoConfigFile).f_SplitLine<true>())
		{
			CStr Key = fg_GetStrSep(Line, " ");
			if (Key == "XcodeVersion")
				Version = Line;
		}
	}

#if defined(DPlatformFamily_macOS)
	if (Version.f_IsEmpty())
	{
		CStr XcodePath = "/Applications/Xcode.app/Contents";

		for (auto &pXcodeLink : {"/var/select/developer_dir", "/var/db/xcode_select_link", "/usr/share/xcode-select/xcode_dir_link", "/usr/share/xcode-select/xcode_dir_path"})
		{
			try
			{
				XcodePath = CFile::fs_GetPath(CFile::fs_ResolveSymbolicLink(pXcodeLink));
				break;
			}
			catch (CException const &)
			{
			}
		}

		if (CFile::fs_FileExists(XcodePath))
		{
			try
			{
				CStr StringContents = CFile::fs_ReadStringFromFile(XcodePath / "version.plist", true);

				bool bNextIsVersion = false;
				for (auto &Line : StringContents.f_SplitLine<true>())
				{
					if (bNextIsVersion)
					{
						CStr Major;
						CStr Minor;
						(CStr::CParse("<string>{}.{}</string>") >> Major >> Minor).f_Parse(Line.f_Trim());
						if (!Major.f_IsEmpty())
							Version = Major;
						break;
					}
					if (Line.f_Trim() == "<key>CFBundleShortVersionString</key>")
						bNextIsVersion = true;
				}
			}
			catch (CException const &)
			{
			}
		}
	}
#endif

	if (Version.f_IsEmpty())
		Version = "12";

	return "Xcode{}"_f << Version;

#elif defined(DPlatformFamily_Windows)
	CStr RepoConfigFile = _RootPath / "Repo.conf";

	CStr Errors;
	auto Versions = CBuildSystem::fs_GetVisualStudioVersions(Errors);

	CStr Version;
	if (Versions.f_FindEqual(18u))
		Version = "2026";
	else
		Version = "2022";

	if (CFile::fs_FileExists(RepoConfigFile))
	{
		for (auto Line : CFile::fs_ReadStringFromFile(RepoConfigFile).f_SplitLine<true>())
		{
			CStr Key = fg_GetStrSep(Line, " ");
			if (Key == "VisualStudioVersion")
				Version = Line;
		}
	}

	return "VisualStudio{}"_f << Version;
#else
	return "Xcode";
#endif
}

CGenerateOptions CTool_Malterlib::fs_ParseSharedOptions(NEncoding::CEJsonSorted const &_Params)
{
	CStr CurrentDirectory = _Params["CurrentDirectory"].f_String();

	CGenerateOptions GenerateOptions;
	auto &GenerateSettings = GenerateOptions.m_Settings;
	GenerateSettings.m_SourceFile = fs_GetFileNameOrEmpty(_Params["BuildSystem"], CurrentDirectory);
	GenerateSettings.m_OutputDir = fs_GetFileNameOrEmpty(_Params["OutputDirectory"], CurrentDirectory);
	GenerateSettings.m_Generator = _Params["Generator"].f_String();
	if (GenerateSettings.m_Generator.f_IsEmpty())
		GenerateSettings.m_Generator = fs_DefaultGenerator(CFile::fs_GetPath(GenerateSettings.m_SourceFile));

	if (GenerateSettings.m_SourceFile.f_IsEmpty())
		DMibError("You must specify a valid build sytem file with --build-system");

	if (_Params["AbsoluteFilePaths"].f_Boolean())
		GenerateSettings.m_GenerationFlags |= EGenerationFlag_AbsoluteFilePaths;
	if (_Params["SingleThreaded"].f_Boolean())
		GenerateSettings.m_GenerationFlags |= EGenerationFlag_SingleThreaded;
	if (_Params["UseCachedEnvironment"].f_Boolean())
		GenerateSettings.m_GenerationFlags |= EGenerationFlag_UseCachedEnvironment;
	if (!_Params["UseUserSettings"].f_Boolean())
		GenerateSettings.m_GenerationFlags |= EGenerationFlag_DisableUserSettings;

	GenerateOptions.m_GitFetchTimeout = _Params["GitFetchTimeout"].f_Integer();

	GenerateOptions.m_bSkipUpdate = _Params["SkipUpdate"].f_Boolean();
	GenerateOptions.m_bForceUpdateRemotes = _Params["ForceUpdateRemotes"].f_Boolean();
	GenerateOptions.f_ParseReconcileActions(_Params);

	auto &DetailedPositions = _Params["DetailedPositions"];
	if (DetailedPositions.f_IsString() && DetailedPositions.f_String() == "OnDemand")
		GenerateOptions.m_DetailedPositions = EDetailedPositions_OnDemand;
	else if (DetailedPositions.f_IsBoolean())
	{
		if (DetailedPositions.f_Boolean())
			GenerateOptions.m_DetailedPositions = EDetailedPositions_Enable;
		else
			GenerateOptions.m_DetailedPositions = EDetailedPositions_Disable;
	}

	GenerateOptions.m_bDetailedValues = _Params["DetailedValues"].f_Boolean();

	return GenerateOptions;
}

CEJsonOrdered::CKeyValue CTool_Malterlib::fs_CachedEnvironmentOption(bool _bDefault)
{
	return "UseCachedEnvironment?"_o=
		{
			"Names"_o= _o["--use-cached-environment"]
			, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_UseCachedEnvironment", _bDefault ? "true" : "false") == "true"
			, "Description"_o= "Use the cached environment instead of current environment."
		}
	;
}

TCFuture<uint32> CTool_Malterlib::f_RunBuildSystem
	(
		NFunction::TCFunctionMovable<TCFuture<CBuildSystem::ERetry> (CBuildSystem *_pBuildSystem)> _fCommand
		, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine
		, CGenerateOptions const *_pGenerateOptions
	)
{
	CFile::CSetAttributeEmulationScope DisableAttributeEmulationScope(false);

	auto RetryResult = co_await CBuildSystem::fs_RunBuildSystem
		(
			fg_Move(_fCommand)
			, _pCommandLine
			, [_pCommandLine](NStr::CStr const &_Output, bool _bError)
			{
				if (_bError)
					*_pCommandLine %= _Output;
				else
					*_pCommandLine += _Output;
			}
			, *_pGenerateOptions
		)
		.f_Wrap()
	;

	if (!RetryResult)
	{
		auto AnsiEncoding = _pCommandLine->f_AnsiEncoding();

		*_pCommandLine %=
			"\n"
			"{}Errors:{}\n"
			"{}\n"_f
			<< AnsiEncoding.f_StatusError()
			<< AnsiEncoding.f_Default()
			<< RetryResult.f_GetExceptionStr()
		;

		co_return 1;
	}

	CBuildSystem::ERetry Retry = *RetryResult;

	if (Retry == CBuildSystem::ERetry_Relaunch)
		co_return 3;
	else if (Retry == CBuildSystem::ERetry_Relaunch_NoReconcileOptions)
		co_return 4;

	co_return 0;
}

void CTool_Malterlib::f_Register
	(
		TCActor<CDistributedToolAppActor> const &_ToolActor
		, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
		, CDistributedAppCommandLineSpecification &o_CommandLine
		, NStr::CStr const &_ClassName
	)
{
	if (!fg_IsMalterlib())
		return;

	f_Register_DummyCommands(o_CommandLine);
	f_Register_SharedOptions(o_ToolsSection);
	f_Register_LfsReleaseStore(o_CommandLine);
	f_Register_RepositoryManagement(o_ToolsSection);
	f_Register_Core(o_ToolsSection);
}

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_Malterlib);
