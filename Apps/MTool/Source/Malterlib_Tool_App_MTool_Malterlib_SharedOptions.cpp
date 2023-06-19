// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_SharedOptions(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	CStr CurrentDirectory = CFile::fs_GetCurrentDirectory();
	auto BuildSystemFiles = CFile::fs_FindFiles(CurrentDirectory / "*.MBuildSystem");

	CEJSONOrdered DetailedPositionsDefault;
	{
		auto DetailedPositionsEnv = fg_GetSys()->f_GetEnvironmentVariable("Malterlib_UseDetailedPositions", "OnDemand");
		if (DetailedPositionsEnv == "true")
			DetailedPositionsDefault = true;
		else if (DetailedPositionsEnv == "false")
			DetailedPositionsDefault = false;
		else
			DetailedPositionsDefault = DetailedPositionsEnv;
	}
	
	o_ToolsSection.f_RegisterSectionOptions
		(
			{
				"CurrentDirectory?"_o=
				{
					"Names"_o= {"--current-directory", "-C"}
					, "Default"_o= CurrentDirectory
					, "Description"_o= "The current directory to interpret other path options in relation to."
				}
				, "BuildSystem?"_o=
				{
					"Names"_o= {"--build-system"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_BuildSystem", BuildSystemFiles.f_GetLen() == 1 ? BuildSystemFiles[0] : CStr())
					, "Description"_o= "The root build system file."
				}
				, "Generator?"_o=
				{
					"Names"_o= {"--generator"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_Generator", "")
					, "Description"_o= "The generator to use to generate the build system.\n"
					"Leave empty to use the default generator for the platform or the generator specified in Repo.conf\n"
				}
				, "OutputDirectory?"_o=
				{
					"Names"_o= {"--output-directory"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_OutputDirectory", CurrentDirectory / "BuildSystem/Default")
					, "Description"_o= "Where to output the generated build system."
				}
				, "AbsoluteFilePaths?"_o=
				{
					"Names"_o= {"--absolute-file-paths"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_AbsoluteFilePaths", "true") == "true"
					, "Description"_o= "Use absolute file paths when generating."
				}
				, "SingleThreaded?"_o=
				{
					"Names"_o= {"--single-threaded"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_SingleThreaded", "false") == "true"
					, "Description"_o= "Only use a single thread when generating."
				}
				, "UseUserSettings?"_o=
				{
					"Names"_o= {"--use-user-settings"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_UseUserSettings", "true") == "true"
					, "Description"_o= "Use user settings."
				}
				, "SkipUpdate?"_o=
				{
					"Names"_o= {"--skip-update"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_SkipUpdate", "false") == "true"
					, "Description"_o= "Skip repository updates."
				}
				, "ForceUpdateRemotes?"_o=
				{
					"Names"_o= {"--force-update-remotes"}
					, "Default"_o= true
					, "Description"_o= "Force update local tags when updating remotes.\n"
				}
				, "GitFetchTimeout?"_o=
				{
					"Names"_o= {"--fetch-timeout"}
					, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_GitFetchTimeout", "5").f_ToInt()
					, "Description"_o= "The number of seconds to wait before timing out git fetches."
				}
				, "Reconcile?"_o=
				{
					"Names"_o= {"--reconcile"}
					, "Default"_o= ""
					, "Description"_o= "Options for how to reconcile repository updates.\n"
					"Format: Wildcard:Action[,Wildcard:Action]...\n"
					"Valid actions\r"
					"@Indent=12\r"
					"   auto:    Accept recommended actions\r"
					"   rebase:  Rebase repositories\r"
					"   reset:   Reset repositories (WARNING changes can be lost)\r"
				}
				, "ReconcileRemoved?"_o=
				{
					"Names"_o= {"--reconcile-removed"}
					, "Default"_o= ""
					, "Description"_o= "Options for how to reconcile repositories that are no longer referenced.\n"
					"Format: Wildcard:Action[,Wildcard:Action]...\n"
					"Valid actions\r"
					"@Indent=12\r"
					"   leave:   Leave removed repositories on disk\r"
					"   delete:  Delete removed repositories from disk (WARNING repository and all unpushed history will be deleted permanently)\r"
				}
				, "ReconcileForce?"_o=
				{
					"Names"_o= {"--reconcile-force"}
					, "Default"_o= false
					, "Description"_o= "Force reconcile options to apply to all wildcards, even for repositories that you have not seen recommended actions for.\n"
				}
				, "ReconcileNoOptions?"_o=
				{
					"Names"_o= {"--reconcile-no-options"}
					, "Default"_o= false
					, "Hidden"_o= true
					, "Description"_o= "[INTERNAL] Used internally when relaunching to force reconcile options to be ignored.\n"
				}
				, "DetailedPositions?"_o=
				{
					"Names"_o= {"--detailed-positions"}
					, "Type"_o= COneOf{true, false, "OnDemand"}
					, "Default"_o= fg_Move(DetailedPositionsDefault)
					, "Description"_o= "Use detailed positions.\n"
					"Enabling detailed positions is going to significantly affect performance. The default is OnDemand which will re-run the command"
					" with detailed positions in case of an error."
				}
				, "DetailedValues?"_o=
				{
					"Names"_o= {"--detailed-values"}
					, "Default"_o= false
					, "Description"_o= "Show values for contributing positions.\n"
					"Enabling detailed values is going to significantly affect performance"
				}
			}
		)
	;
}
