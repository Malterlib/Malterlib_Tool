// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_Core(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	o_ToolsSection.f_RegisterDirectCommand
		(
			{
				"Names"_= {"generate"}
				, "Description"_= "Generate build system.\n"
				, "Category"_= "Core"
				, "Options"_=
				{
					"Action?"_=
					{
						"Names"_= {"--action"}
						, "Default"_= "Build"
						, "Type"_= COneOf{"Build", "Clean", "ReBuild"}
						, "Description"_= "Action from build system when generating as part of build.\n"
						"One of: Build, Clean or ReBuild\n"
					}
					, fs_CachedEnvironmentOption(false)
				}
				, "Parameters"_=
				{
					"Workspace?"_=
					{
						"Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_Workspace", "")
						, "Description"_= "Generate only this specific workspace."
					}
				}
			}
			, [=](NEncoding::CEJSON const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
			{
				CGenerateOptions GenerateOptions = fs_ParseSharedOptions(_Params);

				GenerateOptions.m_Settings.m_Workspace = _Params["Workspace"].f_String();
				GenerateOptions.m_Settings.m_Action = _Params["Action"].f_String();

				bool bChanged = false;

				auto ExitValue = f_RunBuildSystem
					(
						[&](NBuildSystem::CBuildSystem &_BuildSystem)
						{
							CBuildSystem::ERetry Retry = CBuildSystem::ERetry_None;
							if (_BuildSystem.f_Action_Generate(GenerateOptions, Retry))
								bChanged = true;
							return Retry;
						}
					 	, _CommandLineClient.f_AnsiEncodingFlags()
					)
				;

				if (ExitValue)
					return ExitValue;

				return bChanged ? 2 : 0;
			}
		)
	;

#if 0
	o_ToolsSection.f_RegisterDirectCommand
		(
			{
				"Names"_= {"create"}
				, "Description"_= "Create a new Malterlib build system.\n"
				, "Category"_= "Core"
				, "Options"_=
				{
					fs_CachedEnvironmentOption(true)
				}
			}
			, [=](NEncoding::CEJSON const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
			{
				return f_RunBuildSystem
					(
						[GenerateOptions = fs_ParseSharedOptions(_Params)](NBuildSystem::CBuildSystem &_BuildSystem)
						{
							return _BuildSystem.f_Action_Create(GenerateOptions);
						}
					 	, _CommandLineClient.f_AnsiEncodingFlags()
					)
				;
			}
		)
	;
#endif
}
