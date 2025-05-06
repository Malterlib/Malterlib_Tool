// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_DummyCommands(CDistributedAppCommandLineSpecification &o_CommandLine)
{
	auto fDummyCommand = [](NEncoding::CEJSONSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
		{
			DMibError("This command is dummy only, run through the ./mib script");
		}
	;

	auto HelpGlobalOptions = CDistributedAppCommandLineSpecification::fs_RelevantHelpGlobalOptions();

	auto Section = o_CommandLine.f_AddSection("Utilities", "Various utilities.", "Default");
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["test"]
				, "Description"_o= "Build and run tests."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
				, "Parameters"_o=
				{
					"Configuration?"_o=
					{
						"Default"_o= "Debug"
						, "Description"_o= "The configuration to build the tests for.\n"
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
				"Names"_o= _o["test_release"]
				, "Description"_o= "Build and run tests with Release configuration."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
			}
			, fDummyCommand
		)
	;
#if DPlatformFamily_macOS
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["setup"]
				, "Description"_o= "Setup prerequisites."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
				, "Options"_o=
				{
					"All?"_o=
					{
						"Names"_o= _o["--all"]
						, "Default"_o= false
						, "Description"_o= "Install all plugins and highlighting for Xcode.\n"
						, "CanNegate"_o= false
					}
					, "Default?"_o=
					{
						"Names"_o= _o["--default"]
						, "Default"_o= false
						, "Description"_o= "Install default plugins and highlighting for Xcode.\n"
						, "CanNegate"_o= false
					}
					, "None?"_o=
					{
						"Names"_o= _o["--none"]
						, "Default"_o= false
						, "Description"_o= "Install no plugins or highlighting for Xcode.\n"
						, "CanNegate"_o= false
					}
				}
				, "ShowOptionsInCommandEntry"_o= true
			}
			, fDummyCommand
		)
	;
#endif
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["build"]
				, "Description"_o= "Build a workspace."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
				, "Parameters"_o=
				{
					"Workspace"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The workspace to build."
					}
					, "Platform"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The platform to build.\n"
						"For example: " DMibStringize(DPlatform)
					}
					, "Architecture"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The architecture to build.\n"
						"For example: " DMibStringize(DArchitecture)
					}
					, "Configuration"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The configuration to build.\n"
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
				"Names"_o= _o["build_target"]
				, "Description"_o= "Build a targen in a workspace."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
				, "Parameters"_o=
				{
					"Workspace"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The workspace to build."
					}
					, "Target"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The target to build."
					}
					, "Platform"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The platform to build.\n"
						"For example: " DMibStringize(DPlatform)
					}
					, "Architecture"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The architecture to build.\n"
						"For example: " DMibStringize(DArchitecture)
					}
					, "Configuration"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The configuration to build.\n"
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
				"Names"_o= _o["prebuild"]
				, "Description"_o= "Run prebuild actions.\n"
				"Generates the build system and cleans intermediate and output files"
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
				, "Options"_o=
				{
					"Generator?"_o=
					{
						"Names"_o= _o["Generator"]
						, "Default"_o= ""
						, "Description"_o= "The generator to use when generating files."
					}
					, "Extension?"_o=
					{
						"Names"_o= _o["Extension"]
						, "Default"_o= "MBuildSystem"
						, "Description"_o= "The extension of the build system file."
					}
				}
				, "Parameters"_o=
				{
					"BuildSystemName"_o=
					{
						"Type"_o= ""
						, "Description"_o= "The name of the build sytem file, minus the MBuildSystem extension."
					}
					, "Workspaces..."_o=
					{
						"Type"_o= _o[""]
						, "Description"_o= "The workspaces to generate build systems for."
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["postbuild"]
				, "Description"_o= "Run postbuild actions.\n"
				"Cleans intermediate and output files for workspaces."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
				, "Parameters"_o=
				{
					"Workspaces..."_o=
					{
						"Type"_o= _o[""]
						, "Description"_o= "The workspaces to clean intermediate and output files for."
					}
				}
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["bootstrap_only"]
				, "Description"_o= "Only bootstrap malterlib."
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
			}
			, fDummyCommand
		)
	;
	Section.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["detect_system"]
				, "Description"_o= "Source this command to get build system info in environment.\n"
				"@Indent=21\r"
				"   MToolPath:        The path to the MTool executable. Is added to PATH as well when sourcing.\r"
				"   CallDirect:       Helper to call executabels without mangling parameters in MinGW.\r"
				"   BuildSystemRoot:  The root of the build system.\r"
				, "GlobalOptions"_o= HelpGlobalOptions
				, "ShowParametersStart"_o= false
			}
			, fDummyCommand
		)
	;
}
