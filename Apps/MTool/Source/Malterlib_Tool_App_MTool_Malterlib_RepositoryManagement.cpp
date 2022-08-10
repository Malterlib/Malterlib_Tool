// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_RepositoryManagement(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_= {"update_repos"}
				, "Description"_= "Update repositories.\n"
				, "Category"_= "Repository management"
				, "Options"_= {fs_CachedEnvironmentOption(true)}
			}
			, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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

	auto Filter_Name = "RepoName?"_=
		{
			"Names"_= {"--repo-name", "-n"}
			, "Default"_= "*"
			, "Description"_= "Only run command on repositories that have a name that match the specified wildcard."
		}
	;

	auto Filter_Branch = "RepoBranch?"_=
		{
			"Names"_= {"--repo-branch"}
			, "Default"_= ""
			, "Description"_= "Only run command on repositories that are currently on specified branch."
		}
	;

	auto fFilter_Type = [](CStr const &_Default)
		{
			return "RepoType?"_=
				{
					"Names"_= {"--repo-type"}
					, "Default"_= _Default
					, "Description"_= "Only run command no repositories of specified type.\n"
					"Repository type is specified in the build system with Repository.Type\n"
				}
			;
		}
	;

	auto Filter_Tags = "RepoTags?"_=
		{
			"Names"_= {"--repo-tags"}
			, "Default"_= ""
			, "Description"_= "Only run command on repositories that have all tags specified.\n"
			"Format: [Tag1;[Tag2;[...]]]\n"
			"Only repositories with all tags specified will be included.\n"
			"Repository tags is specified in the build system with Repository.Tags\n"
		}
	;

	auto fFilter_OnlyChanged = [](bool _bDefault)
		{
			return "RepoOnlyChanged?"_=
				{
					"Names"_= {"--repo-only-changed", "-c"}
					, "Default"_= _bDefault
					, "Description"_= "Only run command on repositories that have changes.\n"
					"The repository is deemed changed if the branch is not the default branch, if any files have been changed"
					", added or removed (git status -s), or if changes needs to be pushed.\n"
				}
			;
		}
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_= {"status"}
				, "Description"_= "Get the status of all repositories and update repository states.\n"
				, "Category"_= "Repository management"
				, "Options"_=
				{
					"OpenEditor?"_=
					{
						"Names"_= {"--open-editor", "-e"}
						, "Default"_= false
						, "Description"_= "Opens the repositories that require action in your repository editor.\n"
						"The editor can be configured in UserSettings.MSettings or UserSettingsGlobal.MSettings. Look for MalterlibRepositoryEditor\n"
					}
					, "UpdateRemotes?"_=
					{
						"Names"_= {"--update-remotes", "-r"}
						, "Default"_= false
						, "Description"_= "Fetch all remotes before showing status.\n"
					}
					, "ShowUnchanged?"_=
					{
						"Names"_= {"--show-unchanged", "-u"}
						, "Default"_= false
						, "Description"_= "Show status of repositories that does not have any actions you need to take on.\n"
					}
					, "Verbose?"_=
					{
						"Names"_= {"--verbose", "-v"}
						, "Default"_= false
						, "Description"_= "Verbose mode, show which files were changed, added or removed and differences to all remotes.\n"
					}
					, "OnlyTracked?"_=
					{
						"Names"_= {"--only-tracked", "-t"}
						, "Default"_= false
						, "Description"_= "Show only tracked changes. Otherwise untraked files will be considered as well.\n"
					}
					, "AllBranches?"_=
					{
						"Names"_= {"--all-branches", "-a"}
						, "Default"_= false
						, "Description"_= "Show the status of all branches, not just the current branch.\n"
					}
					, "UseDefaultUpstreamBranch?"_=
					{
						"Names"_= {"--use-default-upstream-branch", "-b"}
						, "Default"_= false
						, "Description"_= "Compare to the default upstream branch instead of the current branch.\n"
					}
					, "NeedActionOnPush?"_=
					{
						"Names"_= {"--need-action-on-push", "-p"}
						, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPush", "false") == "true"
						, "Description"_= "Consider repositories that needs to be pushed as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnPull?"_=
					{
						"Names"_= {"--need-action-on-pull"}
						, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPull", "true") == "true"
						, "Description"_= "Consider repositories that needs to be pulled as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnLocalChanes?"_=
					{
						"Names"_= {"--need-action-on-local-changes"}
						, "Default"_= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnLocalChanges", "true") == "true"
						, "Description"_= "Consider repositories that has local changes as requiring action when --open-editor is specified.\n"
					}
					, "NonDefaultToAll?"_=
					{
						"Names"_= {"--non-default-to-all", "-d"}
						, "Default"_= false
						, "Description"_= "When on a non-default branch, show push state to all remotes, not just origin.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem &_BuildSystem) -> TCFuture<CBuildSystem::ERetry>
						{
							co_await ECoroutineFlag_AllowReferences;
							co_return co_await _BuildSystem.f_Action_Repository_Status(GenerateOptions, RepoFilter, Flags);
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
					"Names"_= {"git"}
					, "Description"_= "Run git for all repositories.\n"
					, "Category"_= "Repository management"
					, "ErrorOnCommandAsParameter"_= false
					, "ErrorOnOptionAsParameter"_= false
					, "Options"_=
					{
						"Synchronous?"_=
						{
							"Names"_= {"--sync", "-s"}
							, "Default"_= false
							, "Description"_= "Run all git commands synchronously. By default all git invocations will bu run in parallel.\n"
						}
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fs_CachedEnvironmentOption(true)
					}
					, "Parameters"_=
					{
						"GitParameters...?"_=
						{
							"Type"_= {""}
							, "Default"_= _[_]
							, "Description"_= "The parameters to send to git.\n"
						}
					}
				}
				, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
		auto Option_Pretend = "Pretend?"_=
			{
				"Names"_= {"--pretend", "-p"}
				, "Default"_= false
				, "Description"_= "Only pretend to run the command, only report the actions that would be taken.\n"
			}
		;

		auto Option_Force = "Force?"_=
			{
				"Names"_= {"--force", "-f"}
				, "Default"_= false
				, "Description"_= "Overwrite any destination branches that already exists.\n"
			}
		;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_= {"branch"}
					, "Description"_= "Check out branch for matching repositories.\n"
					, "Category"_= "Repository management"
					, "Options"_=
					{
						Option_Pretend
						, Option_Force
						, fFilter_Type("")
						, fFilter_OnlyChanged(true)
						, fs_CachedEnvironmentOption(true)
					}
					, "Parameters"_=
					{
						"Branch"_=
						{
							"Type"_= ""
							, "Description"_= "The branch to checkout.\n"
						}
					}
				}
				, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
					"Names"_= {"unbranch"}
					, "Description"_= "Check out the default branch for matching repositories.\n"
					, "Category"_= "Repository management"
					, "Options"_=
					{
						Option_Pretend
						, Option_Force
						, fFilter_Type("")
						, fFilter_OnlyChanged(true)
						, fs_CachedEnvironmentOption(true)
					}
				}
				, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
				"Names"_= {"cleanup-branches"}
				, "Description"_= "Clean up branches that have been pushed.\n"
				, "Category"_= "Repository management"
				, "Options"_=
				{
					"Pretend?"_=
					{
						"Names"_= {"--pretend", "-p"}
						, "Default"_= false
						, "Description"_= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "AllRemotes?"_=
					{
						"Names"_= {"--all-remotes", "-a"}
						, "Default"_= false
						, "Description"_= "Also delete branches on remotes specified as writable.\n"
					}
					, "UpdateRemotes?"_=
					{
						"Names"_= {"--update-remotes", "-r"}
						, "Default"_= false
						, "Description"_= "Fetch all remotes before determining what to delete.\n"
					}
					, "Verbose?"_=
					{
						"Names"_= {"--verbose", "-v"}
						, "Default"_= false
						, "Description"_= "List branches that are not deleted and why.\n"
					}
					, "Force?"_=
					{
						"Names"_= {"--force", "-f"}
						, "Default"_= false
						, "Description"_= "Delete branches even though they are not merged to $(remote)/$(default_branch).\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_=
				{
					"Branches...?"_=
					{
						"Type"_= {""}
						, "Default"_= _[_]
						, "Description"_= "The branches to clean up. Uses wildcards. Leave empty to clean up all branches.\n"
					}
				}
			}
			, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
				"Names"_= {"cleanup-tags"}
				, "Description"_= "Clean up tags that are ancestors of the default branch.\n"
				, "Category"_= "Repository management"
				, "Options"_=
				{
					"Pretend?"_=
					{
						"Names"_= {"--pretend", "-p"}
						, "Default"_= false
						, "Description"_= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "AllRemotes?"_=
					{
						"Names"_= {"--all-remotes", "-a"}
						, "Default"_= false
						, "Description"_= "Also delete tags on remotes specified as writable.\n"
					}
					, "UpdateRemotes?"_=
					{
						"Names"_= {"--update-remotes", "-r"}
						, "Default"_= false
						, "Description"_= "Fetch all remotes before determining what to delete.\n"
					}
					, "Verbose?"_=
					{
						"Names"_= {"--verbose", "-v"}
						, "Default"_= false
						, "Description"_= "List tags that are not deleted and why.\n"
					}
					, "Force?"_=
					{
						"Names"_= {"--force", "-f"}
						, "Default"_= false
						, "Description"_= "Delete tags even though they are not ancestors of $(remote)/$(default_branch).\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_=
				{
					"Tags...?"_=
					{
						"Type"_= {""}
						, "Default"_= _[_]
						, "Description"_= "The tags to clean up. Leave empty to clean up all tags. Uses wildcards.\n"
					}
				}
			}
			, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
				"Names"_= {"push"}
				, "Description"_= "Push all repositories that need pushing.\n"
				, "Category"_= "Repository management"
				, "Options"_=
				{
					"Pretend?"_=
					{
						"Names"_= {"--pretend", "-p"}
						, "Default"_= false
						, "Description"_= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "FollowTags?"_=
					{
						"Names"_= {"--follow-tags"}
						, "Default"_= true
						, "Description"_= "Also push tags that are reachable from the refs pushed.\n"
					}
					, "NonDefaultToAll?"_=
					{
						"Names"_= {"--non-default-to-all", "-d"}
						, "Default"_= false
						, "Description"_= "When on a non-default branch, push to all remotes, not just origin.\n"
					}
					, "Force?"_=
					{
						"Names"_= {"--force", "-f"}
						, "Default"_= false
						, "Description"_= "Force push to remotes.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_=
				{
					"Remotes...?"_=
					{
						"Type"_= {""}
						, "Default"_= _[_]
						, "Description"_= "The remotes to push to. By default pushes to all remotes.\n"
					}
				}
			}
			, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
				"Names"_= {"list-commits"}
				, "Description"_= "List commits in all repositories between two commits in main repository.\n"
				, "Category"_= "Repository management"
				, "Options"_=
				{
					"Local?"_=
					{
						"Names"_= {"--local", "-l"}
						, "Default"_= false
						, "Description"_= "Don't fetch all remotes before listing commits.\n"
					}
					, "Compact?"_=
					{
						"Names"_= {"--compact"}
						, "Default"_= false
						, "Description"_= "If possible, make size columns to fit content.\n"
					}
					, "ChangeLog?"_=
					{
						"Names"_= {"--changelog"}
						, "Default"_= false
						, "Description"_= "List all commits sorted by date.\n"
					}
					, "Columns?"_=
					{
						"Names"_= {"--columns"}
						, "Default"_= ""
						, "Description"_= "Add columns that extract data from .\n"
						"Format: [Name:Wildcard[;Name:Wildcard]...]\n"
					}
					, "Prefix?"_=
					{
						"Names"_= {"--prefix"}
						, "Default"_= ""
						, "Description"_= "Prefix to put in front of every output line.\n"
					}
					, "MaxCommits?"_=
					{
						"Names"_= {"--max-commits"}
						, "Default"_= 50
						, "Description"_= "Max commits to display for non-main repositories.\n"
					}
					, "MaxCommitsMain?"_=
					{
						"Names"_= {"--max-commits-main"}
						, "Default"_= 500
						, "Description"_= "Max commits to display for main repository.\n"
					}
					, "MaxMessageWidth?"_=
					{
						"Names"_= {"--max-message-width"}
						, "Default"_= 60
						, "Description"_= "Max width of the message column.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_=
				{
					"FromReference?"_=
					{
						"Default"_= "origin/master"
						, "Description"_= "The commit to start showing commits from.\n"
					}
					, "ToReference?"_=
					{
						"Default"_= "HEAD"
						, "Description"_= "The commit to end showing commits to.\n"
					}
				}
			}
			, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
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
}
