// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystem>
#include <Mib/Encoding/EJSON>
#include <Mib/CommandLine/AnsiEncoding>

class CTool_Malterlib : public CDistributedTool, public CAllowUnsafeThis
{
public:
	static CStr fs_GetFileNameOrEmpty(CEJSONSorted const &_Param, CStr const &_CurrentDirectory);
	static CStr fs_DefaultGenerator(CStr const &_RootPath);
	static CGenerateOptions fs_ParseSharedOptions(CEJSONSorted const &_Params);
	static CEJSONOrdered::CKeyValue fs_CachedEnvironmentOption(bool _bDefault);

	TCFuture<uint32> f_RunBuildSystem
		(
			NFunction::TCFunctionMovable<TCFuture<CBuildSystem::ERetry> (CBuildSystem &_BuildSystem)> _fCommand
			, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine
			, CGenerateOptions const *_pGenerateOptions
		)
	;

	void f_Register_SharedOptions(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection);
	void f_Register_DummyCommands(CDistributedAppCommandLineSpecification &o_CommandLine);
	void f_Register_Core(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection);
	void f_Register_RepositoryManagement(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection);
	void f_Register_LfsReleaseStore(CDistributedAppCommandLineSpecification &o_CommandLine);

	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		) override
	;
};
