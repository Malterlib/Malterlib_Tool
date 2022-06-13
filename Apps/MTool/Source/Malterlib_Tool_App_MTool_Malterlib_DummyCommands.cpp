// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_DummyCommands(CDistributedAppCommandLineSpecification &o_CommandLine)
{
	auto fDummyCommand = [](NEncoding::CEJSON const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
		{
			DMibError("This command is dummy only, run through the ./mib script");
		}
	;

	auto HelpGlobalOptions = CDistributedAppCommandLineSpecification::fs_RelevantHelpGlobalOptions();

	auto Section = o_CommandLine.f_AddSection("Utilities", "Various utilities.", "Default");
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"test"}
				, "Description"_= "Build and run tests."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
				, "Parameters"_=
				{
					"Configuration?"_=
					{
						"Default"_= "Debug"
						, "Description"_= "The configuration to build the tests for.\n"
						"For Example: Release (Tests)"
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"test_release"}
				, "Description"_= "Build and run tests with Release configuration."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
			}
			, fDummyCommand
		)
	;
#if DPlatformFamily_macOS
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"setup"}
				, "Description"_= "Setup prerequisites."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
				, "Options"_=
				{
					"All?"_=
					{
						"Names"_= {"--all"}
						, "Default"_= false
						, "Description"_= "Install all plugins and highlighting for Xcode.\n"
						, "CanNegate"_= false
					}
					, "Default?"_=
					{
						"Names"_= {"--default"}
						, "Default"_= false
						, "Description"_= "Install default plugins and highlighting for Xcode.\n"
						, "CanNegate"_= false
					}
					, "None?"_=
					{
						"Names"_= {"--none"}
						, "Default"_= false
						, "Description"_= "Install no plugins or highlighting for Xcode.\n"
						, "CanNegate"_= false
					}
				}
				, "ShowOptionsInCommandEntry"_= true
			}
			, fDummyCommand
		)
	;
#endif
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"build"}
				, "Description"_= "Build a workspace."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
				, "Parameters"_=
				{
					"Workspace"_=
					{
						"Type"_= ""
						, "Description"_= "The workspace to build."
					}
					, "Platform"_=
					{
						"Type"_= ""
						, "Description"_= "The platform to build.\n"
						"For example: " DMibStringize(DPlatform)
					}
					, "Architecture"_=
					{
						"Type"_= ""
						, "Description"_= "The architecture to build.\n"
						"For example: " DMibStringize(DArchitecture)
					}
					, "Configuration"_=
					{
						"Type"_= ""
						, "Description"_= "The configuration to build.\n"
						"For example: " DMibStringize(DConfig)
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"build_target"}
				, "Description"_= "Build a targen in a workspace."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
				, "Parameters"_=
				{
					"Workspace"_=
					{
						"Type"_= ""
						, "Description"_= "The workspace to build."
					}
					, "Target"_=
					{
						"Type"_= ""
						, "Description"_= "The target to build."
					}
					, "Platform"_=
					{
						"Type"_= ""
						, "Description"_= "The platform to build.\n"
						"For example: " DMibStringize(DPlatform)
					}
					, "Architecture"_=
					{
						"Type"_= ""
						, "Description"_= "The architecture to build.\n"
						"For example: " DMibStringize(DArchitecture)
					}
					, "Configuration"_=
					{
						"Type"_= ""
						, "Description"_= "The configuration to build.\n"
						"For example: " DMibStringize(DConfig)
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"prebuild"}
				, "Description"_= "Run prebuild actions.\n"
				"Generates the build system and cleans intermediate and output files"
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
				, "Options"_=
				{
					"Generator?"_=
					{
						"Names"_= {"Generator"}
						, "Default"_= ""
						, "Description"_= "The generator to use when generating files."
					}
					, "Extension?"_=
					{
						"Names"_= {"Extension"}
						, "Default"_= "MBuildSystem"
						, "Description"_= "The extension of the build system file."
					}
				}
				, "Parameters"_=
				{
					"BuildSystemName"_=
					{
						"Type"_= ""
						, "Description"_= "The name of the build sytem file, minus the MBuildSystem extension."
					}
					, "Workspaces..."_=
					{
						"Type"_= {""}
						, "Description"_= "The workspaces to generate build systems for."
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"postbuild"}
				, "Description"_= "Run postbuild actions.\n"
				"Cleans intermediate and output files for workspaces."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
				, "Parameters"_=
				{
					"Workspaces..."_=
					{
						"Type"_= {""}
						, "Description"_= "The workspaces to clean intermediate and output files for."
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"bootstrap_only"}
				, "Description"_= "Only bootstrap malterlib."
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_= {"detect_system"}
				, "Description"_= "Source this command to get build system info in environment.\n"
				"@Indent=21\r"
				"   MToolPath:        The path to the MTool executable. Is added to PATH as well when sourcing.\r"
				"   CallDirect:       Helper to call executabels without mangling parameters in MinGW.\r"
				"   BuildSystemRoot:  The root of the build system.\r"
				, "GlobalOptions"_= HelpGlobalOptions
				, "ShowParametersStart"_= false
			}
			, fDummyCommand
		)
	;
}
