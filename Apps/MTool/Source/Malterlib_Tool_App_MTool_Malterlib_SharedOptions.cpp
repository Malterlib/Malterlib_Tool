// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_SharedOptions(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	CStr CurrentDirectory = CFile::fs_GetCurrentDirectory();
	auto BuildSystemFiles = CFile::fs_FindFiles(CurrentDirectory / "*.MBuildSystem");

	CEJSON DetailedPositionsDefault;
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
				"CurrentDirectory?"_=
				{
					"Names"_= {"--current-directory", "-C"}
					, "Default"_= CurrentDirectory
					, "Description"_= "The current directory to interpret other path options in relation to."
				}
				, "BuildSystem?"_=
				{
					"Names"_= {"--build-system"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_BuildSystem", BuildSystemFiles.f_GetLen() == 1 ? BuildSystemFiles[0] : CStr())
					, "Description"_= "The root build system file."
				}
				, "Generator?"_=
				{
					"Names"_= {"--generator"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_Generator", "")
					, "Description"_= "The generator to use to generate the build system.\n"
					"Leave empty to use the default generator for the platform or the generator specified in Repo.conf\n"
				}
				, "OutputDirectory?"_=
				{
					"Names"_= {"--output-directory"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_OutputDirectory", CurrentDirectory / "BuildSystem/Default")
					, "Description"_= "Where to output the generated build system."
				}
				, "AbsoluteFilePaths?"_=
				{
					"Names"_= {"--absolute-file-paths"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_AbsoluteFilePaths", "true") == "true"
					, "Description"_= "Use absolute file paths when generating."
				}
				, "SingleThreaded?"_=
				{
					"Names"_= {"--single-threaded"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_SingleThreaded", "false") == "true"
					, "Description"_= "Only use a single thread when generating."
				}
				, "UseUserSettings?"_=
				{
					"Names"_= {"--use-user-settings"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_UseUserSettings", "true") == "true"
					, "Description"_= "Use user settings."
				}
				, "SkipUpdate?"_=
				{
					"Names"_= {"--skip-update"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_SkipUpdate", "false") == "true"
					, "Description"_= "Skip repository updates."
				}
				, "ForceUpdateRemotes?"_=
				{
					"Names"_= {"--force-update-remotes"}
					, "Default"_= true
					, "Description"_= "Force update local tags when updating remotes.\n"
				}
				, "GitFetchTimeout?"_=
				{
					"Names"_= {"--fetch-timeout"}
					, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_GitFetchTimeout", "5").f_ToInt()
					, "Description"_= "The number of seconds to wait before timing out git fetches."
				}
				, "Reconcile?"_=
				{
					"Names"_= {"--reconcile"}
					, "Default"_= ""
					, "Description"_= "Options for how to reconcile repository updates.\n"
					"Format: Wildcard:Action[,Wildcard:Action]...\n"
					"Valid actions\r"
					"@Indent=12\r"
					"   auto:    Accept recommended actions\r"
					"   rebase:  Rebase repositories\r"
					"   reset:   Reset repositories (WARNING changes can be lost)\r"
				}
				, "ReconcileRemoved?"_=
				{
					"Names"_= {"--reconcile-removed"}
					, "Default"_= ""
					, "Description"_= "Options for how to reconcile repositories that are no longer referenced.\n"
					"Format: Wildcard:Action[,Wildcard:Action]...\n"
					"Valid actions\r"
					"@Indent=12\r"
					"   leave:   Leave removed repositories on disk\r"
					"   delete:  Delete removed repositories from disk (WARNING repository and all unpushed history will be deleted permanently)\r"
				}
				, "ReconcileForce?"_=
				{
					"Names"_= {"--reconcile-force"}
					, "Default"_= false
					, "Description"_= "Force reconcile options to apply to all wildcards, even for repositories that you have not seen recommended actions for.\n"
				}
				, "ReconcileNoOptions?"_=
				{
					"Names"_= {"--reconcile-no-options"}
					, "Default"_= false
					, "Hidden"_= true
					, "Description"_= "[INTERNAL] Used internally when relaunching to force reconcile options to be ignored.\n"
				}
				, "DetailedPositions?"_=
				{
					"Names"_= {"--detailed-positions"}
					, "Type"_= COneOf{true, false, "OnDemand"}
					, "Default"_= fg_Move(DetailedPositionsDefault)
					, "Description"_= "Use detailed positions.\n"
					"Enabling detailed positions is going to significantly affect performance. The default is OnDemand which will re-run the command"
					" with detailed positions in case of an error."
				}
				, "DetailedValues?"_=
				{
					"Names"_= {"--detailed-values"}
					, "Default"_= false
					, "Description"_= "Show values for contributing positions.\n"
					"Enabling detailed values is going to significantly affect performance"
				}
			}
		)
	;
}
