// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystem>
#include <Mib/Encoding/EJSON>

using namespace NMib::NBuildSystem;

class CTool_Malterlib : public CDistributedTool
{
public:
	static CStr fs_GetFileNameOrEmpty(NEncoding::CEJSON const &_Param, CStr const &_CurrentDirectory);
	static CStr fs_DefaultGenerator(CStr const &_RootPath);
	static CGenerateOptions fs_ParseSharedOptions(NEncoding::CEJSON const &_Params);
	static CEJSON::CKeyValue fs_CachedEnvironmentOption(bool _bDefault);

	uint32 f_RunBuildSystem(TCFunction<CBuildSystem::ERetry (NBuildSystem::CBuildSystem &_BuildSystem)> &&_fCommand);

	void f_Register_SharedOptions(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection);
	void f_Register_DummyCommands(CDistributedAppCommandLineSpecification &o_CommandLine);
	void f_Register_Core(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection);
	void f_Register_RepositoryManagement(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection);


	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		) override
	;
};
