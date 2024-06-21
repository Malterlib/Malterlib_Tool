// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

#include <Mib/Process/ProcessLaunch>

void CTool_Malterlib::f_Register_RepositoryManagement(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"update_repos"}
				, "Description"_o= "Update repositories.\n"
				, "Category"_o= "Repository management"
				, "Options"_o= 
				{
					"ApplyRepoPolicy?"_o=
					{
						"Names"_o= {"--apply-policy"}
						, "Default"_o= false
						, "Description"_o= "Apply repo policies.\n"
					}
					, "ApplyRepoPolicyPretend?"_o=
					{
						"Names"_o= {"--apply-policy-pretend"}
						, "Default"_o= true
						, "Description"_o= "Instead of applying repo policy, show what would be changed.\n"
					}
					, "ApplyRepoPolicyCreateMissing?"_o=
					{
						"Names"_o= {"--apply-policy-create-missing"}
						, "Default"_o= false
						, "Description"_o= "Create missing repositories when applying policies.\n"
					}
					, "UpdateLfsReleaseIndexes?"_o=
					{
						"Names"_o= {"--update-lfs-release-indexes"}
						, "Default"_o= false
						, "Description"_o= "Update lfs release indexes on remotes.\n"
					}
					, "UpdateLfsReleaseIndexesPretend?"_o=
					{
						"Names"_o= {"--update-lfs-release-indexes-pretend"}
						, "Default"_o= false
						, "Description"_o= "Don't do any actions, just log what would be done.\n"
					}
					, "UpdateLfsReleaseIndexesPruneOrphanedAssets?"_o=
					{
						"Names"_o= {"--update-lfs-release-indexes-prune-orphaned-assets"}
						, "Default"_o= false
						, "Description"_o= "Remove orphaned LFS assets from releases on hosting provider.\n"
					}
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[GenerateOptions](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_Update(GenerateOptions);
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;

	auto Filter_Name = "RepoName?"_o=
		{
			"Names"_o= {"--repo-name", "-n"}
			, "Default"_o= "*"
			, "Description"_o= "Only run command on repositories that have a name that match the specified wildcard."
		}
	;

	auto Filter_Branch = "RepoBranch?"_o=
		{
			"Names"_o= {"--repo-branch"}
			, "Default"_o= ""
			, "Description"_o= "Only run command on repositories that are currently on specified branch."
		}
	;

	auto fFilter_Type = [](CStr const &_Default)
		{
			return "RepoType?"_o=
				{
					"Names"_o= {"--repo-type"}
					, "Default"_o= _Default
					, "Description"_o= "Only run command no repositories of specified type.\n"
					"Repository type is specified in the build system with Repository.Type\n"
				}
			;
		}
	;

	auto Filter_Tags = "RepoTags?"_o=
		{
			"Names"_o= {"--repo-tags"}
			, "Default"_o= ""
			, "Description"_o= "Only run command on repositories that have all tags specified.\n"
			"Format: [Tag1;[Tag2;[...]]]\n"
			"Only repositories with all tags specified will be included.\n"
			"Repository tags is specified in the build system with Repository.Tags\n"
		}
	;

	auto fFilter_OnlyChanged = [](bool _bDefault)
		{
			return "RepoOnlyChanged?"_o=
				{
					"Names"_o= {"--repo-only-changed", "-c"}
					, "Default"_o= _bDefault
					, "Description"_o= "Only run command on repositories that have changes.\n"
					"The repository is deemed changed if the branch is not the default branch, if any files have been changed"
					", added or removed (git status -s), or if changes needs to be pushed.\n"
				}
			;
		}
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"status"}
				, "Description"_o= "Get the status of all repositories and update repository states.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"OpenEditor?"_o=
					{
						"Names"_o= {"--open-editor", "-e"}
						, "Default"_o= false
						, "Description"_o= "Opens the repositories that require action in your repository editor.\n"
						"The editor can be configured in UserSettings.MSettings or UserSettingsGlobal.MSettings. Look for MalterlibRepositoryEditor\n"
					}
					, "UpdateRemotes?"_o=
					{
						"Names"_o= {"--update-remotes", "-r"}
						, "Default"_o= false
						, "Description"_o= "Fetch all remotes before showing status.\n"
					}
					, "ShowUnchanged?"_o=
					{
						"Names"_o= {"--show-unchanged", "-u"}
						, "Default"_o= false
						, "Description"_o= "Show status of repositories that does not have any actions you need to take on.\n"
					}
					, "Verbose?"_o=
					{
						"Names"_o= {"--verbose", "-v"}
						, "Default"_o= false
						, "Description"_o= "Verbose mode, show which files were changed, added or removed and differences to all remotes.\n"
					}
					, "OnlyTracked?"_o=
					{
						"Names"_o= {"--only-tracked", "-t"}
						, "Default"_o= false
						, "Description"_o= "Show only tracked changes. Otherwise untraked files will be considered as well.\n"
					}
					, "AllBranches?"_o=
					{
						"Names"_o= {"--all-branches", "-a"}
						, "Default"_o= false
						, "Description"_o= "Show the status of all branches, not just the current branch.\n"
					}
					, "UseDefaultUpstreamBranch?"_o=
					{
						"Names"_o= {"--use-default-upstream-branch", "-b"}
						, "Default"_o= false
						, "Description"_o= "Compare to the default upstream branch instead of the current branch.\n"
					}
					, "NeedActionOnPush?"_o=
					{
						"Names"_o= {"--need-action-on-push", "-p"}
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPush", "false") == "true"
						, "Description"_o= "Consider repositories that needs to be pushed as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnPull?"_o=
					{
						"Names"_o= {"--need-action-on-pull"}
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPull", "false") == "true"
						, "Description"_o= "Consider repositories that needs to be pulled as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnLocalChanes?"_o=
					{
						"Names"_o= {"--need-action-on-local-changes"}
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnLocalChanges", "true") == "true"
						, "Description"_o= "Consider repositories that has local changes as requiring action when --open-editor is specified.\n"
					}
					, "NonDefaultToAll?"_o=
					{
						"Names"_o= {"--non-default-to-all", "-d"}
						, "Default"_o= false
						, "Description"_o= "When on a non-default branch, show push state to all remotes, not just origin.\n"
					}
					, "HideBranches?"_o=
					{
						"Names"_o= {"--hide-branches", "-H"}
						, "Type"_o= {""}
						, "Default"_o= _[_]
						, "Description"_o= "Hide these branchs. Supports wildcards.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::ERepoStatusFlag Flags = CBuildSystem::ERepoStatusFlag_None;

				if (_Params["ShowUnchanged"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_ShowUnchanged;
				if (_Params["Verbose"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_Verbose;
				if (_Params["UpdateRemotes"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_UpdateRemotes;
				if (_Params["OnlyTracked"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_OnlyTracked;
				if (_Params["AllBranches"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_AllBranches;
				if (_Params["UseDefaultUpstreamBranch"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_UseDefaultUpstreamBranch;
				if (_Params["OpenEditor"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_OpenEditor;
				if (_Params["NonDefaultToAll"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NonDefaultToAll;
				if (_Params["NeedActionOnPush"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NeedActionOnPush;
				if (_Params["NeedActionOnPull"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NeedActionOnPull;
				if (_Params["NeedActionOnLocalChanes"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NeedActionOnLocalChanges;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				auto HideBranches = _Params["HideBranches"].f_StringArray();

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_Status(GenerateOptions, RepoFilter, Flags, HideBranches, _pCommandLine);
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;

	{
		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= {"git"}
					, "Description"_o= "Run git for all repositories.\n"
					, "Category"_o= "Repository management"
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "Options"_o=
					{
						"Synchronous?"_o=
						{
							"Names"_o= {"--sync", "-s"}
							, "Default"_o= false
							, "Description"_o= "Run all git commands synchronously. By default all git invocations will bu run in parallel.\n"
						}
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fs_CachedEnvironmentOption(true)
					}
					, "Parameters"_o=
					{
						"GitParameters...?"_o=
						{
							"Type"_o= {""}
							, "Default"_o= _[_]
							, "Description"_o= "The parameters to send to git.\n"
						}
					}
				}
				, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);
					bool bParallel = !_Params["Synchronous"].f_Boolean();

					TCVector<CStr> GitParameters;
					for (auto &Param : _Params["GitParameters"].f_Array())
						GitParameters.f_Insert(Param.f_String());

					auto GenerateOptions = fs_ParseSharedOptions(_Params);
					co_return co_await f_RunBuildSystem
						(
							[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
							{
								co_await ECoroutineFlag_AllowReferences;
								co_return co_await _BuildSystem.f_Action_Repository_ForEachRepo(GenerateOptions, RepoFilter, bParallel, GitParameters);
							}
							, _pCommandLine
							, &GenerateOptions
						)
					;
				}
			)
		;
	}

	{
		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= {"repo-run"}
					, "Description"_o= "Run commands with git directory as current path.\n"
					, "Category"_o= "Repository management"
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "Options"_o=
					{
						"Synchronous?"_o=
						{
							"Names"_o= {"--sync", "-s"}
							, "Default"_o= false
							, "Description"_o= "Run all commands synchronously. By default all invocations will bu run in parallel.\n"
						}
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fs_CachedEnvironmentOption(true)
						, "Shell?"_o=
						{
							"Names"_o= {"--shell"}
							, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("SHELL", "")
							, "Description"_o= "Run commands in this shell.\n"
						}
						, "ShellOptions?"_o=
						{
							"Names"_o= {"--shell-options"}
							, "Default"_o= {"-c"}
							, "Description"_o= "The options to send to the shell.\n"
						}
						, "ShellParamsInString?"_o=
						{
							"Names"_o= {"--shell-params-in-string"}
							, "Default"_o= true
							, "Description"_o= "If the arguments should be sent in a string to the shell, or if not it will be sent as separate params.\n"
						}
					}
					, "Parameters"_o=
					{
						"RunCommand"_o=
						{
							"Type"_o= ""
							, "Description"_o= "The command to run.\n"
						}
						, "Parameters...?"_o=
						{
							"Type"_o= {""}
							, "Default"_o= _[_]
							, "Description"_o= "The parameters to send to the command.\n"
						}
					}
				}
				, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

					CStr Shell = _Params["Shell"].f_String();
					TCVector<CStr> ShellOptions = _Params["ShellOptions"].f_StringArray();
					bool bShellParamsInString = _Params["ShellParamsInString"].f_Boolean();

					CStr Command = _Params["RunCommand"].f_String();
					TCVector<CStr> Parameters = _Params["Parameters"].f_StringArray();

					CBuildSystem::CForEachRepoDirOptions Options;
					if (Shell)
					{
						Options.m_Application = Shell;
						Options.m_Params.f_Insert(ShellOptions);

						TCVector<CStr> CommandParameters;
						CommandParameters.f_Insert(Command);
						CommandParameters.f_Insert(Parameters);

						if (bShellParamsInString)
							Options.m_Params.f_Insert(CProcessLaunchParams::fs_GetParamsBash(CommandParameters));
						else
							Options.m_Params.f_Insert(CommandParameters);
					}
					else
					{
						Options.m_Application = Command;
						Options.m_Params.f_Insert(Parameters);
					}

					Options.m_bParallel = !_Params["Synchronous"].f_Boolean();

					auto GenerateOptions = fs_ParseSharedOptions(_Params);
					co_return co_await f_RunBuildSystem
						(
							[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
							{
								co_await ECoroutineFlag_AllowReferences;
								co_return co_await _BuildSystem.f_Action_Repository_ForEachRepoDir(GenerateOptions, RepoFilter, Options);
							}
							, _pCommandLine
							, &GenerateOptions
						)
					;
				}
			)
		;
	}

	{
		auto Option_Pretend = "Pretend?"_o=
			{
				"Names"_o= {"--pretend", "-p"}
				, "Default"_o= false
				, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
			}
		;

		auto Option_Force = "Force?"_o=
			{
				"Names"_o= {"--force", "-f"}
				, "Default"_o= false
				, "Description"_o= "Overwrite any destination branches that already exists.\n"
			}
		;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= {"branch"}
					, "Description"_o= "Check out branch for matching repositories.\n"
					, "Category"_o= "Repository management"
					, "Options"_o=
					{
						Option_Pretend
						, Option_Force
						, fFilter_Type("")
						, fFilter_OnlyChanged(true)
						, fs_CachedEnvironmentOption(true)
					}
					, "Parameters"_o=
					{
						"Branch"_o=
						{
							"Type"_o= ""
							, "Description"_o= "The branch to checkout.\n"
						}
					}
				}
				, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

					CStr Branch = _Params["Branch"].f_String();
					if (Branch.f_IsEmpty())
						DMibError("Branch cannot be empty, use unbranch to checkout default branch");

					CBuildSystem::ERepoBranchFlag Flags = CBuildSystem::ERepoBranchFlag_None;

					if (_Params["Pretend"].f_Boolean())
						Flags |= CBuildSystem::ERepoBranchFlag_Pretend;

					if (_Params["Force"].f_Boolean())
						Flags |= CBuildSystem::ERepoBranchFlag_Force;

					auto GenerateOptions = fs_ParseSharedOptions(_Params);
					co_return co_await f_RunBuildSystem
						(
							[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
							{
								co_await ECoroutineFlag_AllowReferences;
								co_return co_await _BuildSystem.f_Action_Repository_Branch(GenerateOptions, RepoFilter, Branch, Flags);
							}
							, _pCommandLine
							, &GenerateOptions
						)
					;
				}
			)
		;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= {"unbranch"}
					, "Description"_o= "Check out the default branch for matching repositories.\n"
					, "Category"_o= "Repository management"
					, "Options"_o=
					{
						Option_Pretend
						, Option_Force
						, fFilter_Type("")
						, fFilter_OnlyChanged(true)
						, fs_CachedEnvironmentOption(true)
					}
				}
				, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

					CBuildSystem::ERepoBranchFlag Flags = CBuildSystem::ERepoBranchFlag_None;

					if (_Params["Pretend"].f_Boolean())
						Flags |= CBuildSystem::ERepoBranchFlag_Pretend;

					if (_Params["Force"].f_Boolean())
						Flags |= CBuildSystem::ERepoBranchFlag_Force;

					auto GenerateOptions = fs_ParseSharedOptions(_Params);
					co_return co_await f_RunBuildSystem
						(
							[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
							{
								co_await ECoroutineFlag_AllowReferences;
								co_return co_await _BuildSystem.f_Action_Repository_Unbranch(GenerateOptions, RepoFilter, Flags);
							}
							, _pCommandLine
							, &GenerateOptions
						)
					;
				}
			)
		;
	}

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"cleanup-branches"}
				, "Description"_o= "Clean up branches that have been pushed.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Pretend?"_o=
					{
						"Names"_o= {"--pretend", "-p"}
						, "Default"_o= false
						, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "AllRemotes?"_o=
					{
						"Names"_o= {"--all-remotes", "-a"}
						, "Default"_o= false
						, "Description"_o= "Also delete branches on remotes specified as writable.\n"
					}
					, "UpdateRemotes?"_o=
					{
						"Names"_o= {"--update-remotes", "-r"}
						, "Default"_o= false
						, "Description"_o= "Fetch all remotes before determining what to delete.\n"
					}
					, "Verbose?"_o=
					{
						"Names"_o= {"--verbose", "-v"}
						, "Default"_o= false
						, "Description"_o= "List branches that are not deleted and why.\n"
					}
					, "Force?"_o=
					{
						"Names"_o= {"--force", "-f"}
						, "Default"_o= false
						, "Description"_o= "Delete branches even though they are not merged to $(remote)/$(default_branch).\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"Branches...?"_o=
					{
						"Type"_o= {""}
						, "Default"_o= _[_]
						, "Description"_o= "The branches to clean up. Uses wildcards. Leave empty to clean up all branches.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CBuildSystem::ERepoCleanupBranchesFlag Flags = CBuildSystem::ERepoCleanupBranchesFlag_None;

				TCVector<CStr> Branches;
				for (auto &BranchJSON : _Params["Branches"].f_Array())
					Branches.f_Insert(BranchJSON.f_String());

				if (_Params["Pretend"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupBranchesFlag_Pretend;
				if (_Params["AllRemotes"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupBranchesFlag_AllRemotes;
				if (_Params["UpdateRemotes"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupBranchesFlag_UpdateRemotes;
				if (_Params["Verbose"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupBranchesFlag_Verbose;
				if (_Params["Force"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupBranchesFlag_Force;

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_CleanupBranches(GenerateOptions, RepoFilter, Flags, Branches);
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"cleanup-tags"}
				, "Description"_o= "Clean up tags that are ancestors of the default branch.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Pretend?"_o=
					{
						"Names"_o= {"--pretend", "-p"}
						, "Default"_o= false
						, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "AllRemotes?"_o=
					{
						"Names"_o= {"--all-remotes", "-a"}
						, "Default"_o= false
						, "Description"_o= "Also delete tags on remotes specified as writable.\n"
					}
					, "UpdateRemotes?"_o=
					{
						"Names"_o= {"--update-remotes", "-r"}
						, "Default"_o= false
						, "Description"_o= "Fetch all remotes before determining what to delete.\n"
					}
					, "Verbose?"_o=
					{
						"Names"_o= {"--verbose", "-v"}
						, "Default"_o= false
						, "Description"_o= "List tags that are not deleted and why.\n"
					}
					, "Force?"_o=
					{
						"Names"_o= {"--force", "-f"}
						, "Default"_o= false
						, "Description"_o= "Delete tags even though they are not ancestors of $(remote)/$(default_branch).\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"Tags...?"_o=
					{
						"Type"_o= {""}
						, "Default"_o= _[_]
						, "Description"_o= "The tags to clean up. Leave empty to clean up all tags. Uses wildcards.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CBuildSystem::ERepoCleanupTagsFlag Flags = CBuildSystem::ERepoCleanupTagsFlag_None;

				TCVector<CStr> Tags;
				for (auto &TagJSON : _Params["Tags"].f_Array())
					Tags.f_Insert(TagJSON.f_String());

				if (_Params["Pretend"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupTagsFlag_Pretend;
				if (_Params["AllRemotes"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupTagsFlag_AllRemotes;
				if (_Params["UpdateRemotes"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupTagsFlag_UpdateRemotes;
				if (_Params["Verbose"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupTagsFlag_Verbose;
				if (_Params["Force"].f_Boolean())
					Flags |= CBuildSystem::ERepoCleanupTagsFlag_Force;

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_CleanupTags(GenerateOptions, RepoFilter, Flags, Tags);
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"push"}
				, "Description"_o= "Push all repositories that need pushing.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Pretend?"_o=
					{
						"Names"_o= {"--pretend", "-p"}
						, "Default"_o= false
						, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "FollowTags?"_o=
					{
						"Names"_o= {"--follow-tags"}
						, "Default"_o= true
						, "Description"_o= "Also push tags that are reachable from the refs pushed.\n"
					}
					, "NonDefaultToAll?"_o=
					{
						"Names"_o= {"--non-default-to-all", "-d"}
						, "Default"_o= false
						, "Description"_o= "When on a non-default branch, push to all remotes, not just origin.\n"
					}
					, "Force?"_o=
					{
						"Names"_o= {"--force", "-f"}
						, "Default"_o= false
						, "Description"_o= "Force push to remotes.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"Remotes...?"_o=
					{
						"Type"_o= {""}
						, "Default"_o= _[_]
						, "Description"_o= "The remotes to push to. By default pushes to all remotes.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CBuildSystem::ERepoPushFlag Flags = CBuildSystem::ERepoPushFlag_None;

				if (_Params["Pretend"].f_Boolean())
					Flags |= CBuildSystem::ERepoPushFlag_Pretend;
				if (_Params["FollowTags"].f_Boolean())
					Flags |= CBuildSystem::ERepoPushFlag_FollowTags;
				if (_Params["NonDefaultToAll"].f_Boolean())
					Flags |= CBuildSystem::ERepoPushFlag_NonDefaultToAll;
				if (_Params["Force"].f_Boolean())
					Flags |= CBuildSystem::ERepoPushFlag_Force;

				TCVector<CStr> Remotes;
				for (auto &Param : _Params["Remotes"].f_Array())
					Remotes.f_Insert(Param.f_String());

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_Push(GenerateOptions, RepoFilter, Remotes, Flags);
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"list-commits"}
				, "Description"_o= "List commits in all repositories between two commits in main repository.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Local?"_o=
					{
						"Names"_o= {"--local", "-l"}
						, "Default"_o= false
						, "Description"_o= "Don't fetch all remotes before listing commits.\n"
					}
					, "Compact?"_o=
					{
						"Names"_o= {"--compact"}
						, "Default"_o= false
						, "Description"_o= "If possible, make size columns to fit content.\n"
					}
					, "ChangeLog?"_o=
					{
						"Names"_o= {"--changelog"}
						, "Default"_o= false
						, "Description"_o= "List all commits sorted by date.\n"
					}
					, "Columns?"_o=
					{
						"Names"_o= {"--columns"}
						, "Default"_o= ""
						, "Description"_o= "Add columns that extract data from .\n"
						"Format: [Name:Wildcard[;Name:Wildcard]...]\n"
					}
					, "Prefix?"_o=
					{
						"Names"_o= {"--prefix"}
						, "Default"_o= ""
						, "Description"_o= "Prefix to put in front of every output line.\n"
					}
					, "MaxCommits?"_o=
					{
						"Names"_o= {"--max-commits"}
						, "Default"_o= 50
						, "Description"_o= "Max commits to display for non-main repositories.\n"
					}
					, "MaxCommitsMain?"_o=
					{
						"Names"_o= {"--max-commits-main"}
						, "Default"_o= 500
						, "Description"_o= "Max commits to display for main repository.\n"
					}
					, "MaxMessageWidth?"_o=
					{
						"Names"_o= {"--max-message-width"}
						, "Default"_o= 60
						, "Description"_o= "Max width of the message column.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"FromReference?"_o=
					{
						"Default"_o= "origin/master"
						, "Description"_o= "The commit to start showing commits from.\n"
					}
					, "ToReference?"_o=
					{
						"Default"_o= "HEAD"
						, "Description"_o= "The commit to end showing commits to.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CStr FromRef = _Params["FromReference"].f_String();
				if (FromRef.f_IsEmpty())
					DMibError("FromReference must be specified");

				CStr ToRef = _Params["ToReference"].f_String();
				if (ToRef.f_IsEmpty())
					DMibError("ToReference must be specified");

				CBuildSystem::ERepoListCommitsFlag Flags = CBuildSystem::ERepoListCommitsFlag_None;
				if (!_Params["Local"].f_Boolean())
					Flags |= CBuildSystem::ERepoListCommitsFlag_UpdateRemotes;
				if (_Params["Compact"].f_Boolean())
					Flags |= CBuildSystem::ERepoListCommitsFlag_Compact;
				if (_Params["ChangeLog"].f_Boolean())
					Flags |= CBuildSystem::ERepoListCommitsFlag_Changelog;

				TCVector<CBuildSystem::CWildcardColumn> WildcardColumns;
				for (auto &Column : _Params["Columns"].f_String().f_Split<true>(";"))
				{
					CStr Wildcard = Column;
					CStr Name = fg_GetStrSep(Wildcard, ":");
					WildcardColumns.f_Insert({Name, Wildcard});
				}

				CStr Prefix = _Params["Prefix"].f_String();

				uint32 MaxCommits = _Params["MaxCommits"].f_Integer();
				uint32 MaxCommitsMain = _Params["MaxCommitsMain"].f_Integer();
				uint32 MaxMessageWidth = _Params["MaxMessageWidth"].f_Integer();

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[&](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_ListCommits
								(
									GenerateOptions
									, RepoFilter
									, FromRef
									, ToRef
									, Flags
									, WildcardColumns
									, Prefix
									, MaxCommitsMain
									, MaxCommits
									, MaxMessageWidth
									, _pCommandLine
								)
							;
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= {"release-package"}
				, "Description"_o= "Release package to repository hosting provider.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[&](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_ReleasePackage
								(
									GenerateOptions
									, RepoFilter
									, _pCommandLine
								)
							;
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;
}
