// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"
#include "Malterlib_Tool_App_MTool_Format.h"
#include "Malterlib_Tool_App_MTool_Validate.h"
#include "Malterlib_Tool_App_MTool_Tidy.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Time/Stopwatch>

namespace NMib::NTool
{
	struct CFormatOutcome
	{
		uint32 m_ExitCode = 0;
	};
}

void CTool_Malterlib::f_Register_RepositoryManagement(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["update-repos", "update_repos"]
				, "Description"_o= "Update repositories.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"ApplyRepoPolicy?"_o=
					{
						"Names"_o= _o["--apply-policy"]
						, "Default"_o= false
						, "Description"_o= "Apply repo policies.\n"
					}
					, "ApplyRepoPolicyPretend?"_o=
					{
						"Names"_o= _o["--apply-policy-pretend"]
						, "Default"_o= true
						, "Description"_o= "Instead of applying repo policy, show what would be changed.\n"
					}
					, "ApplyRepoPolicyCreateMissing?"_o=
					{
						"Names"_o= _o["--apply-policy-create-missing"]
						, "Default"_o= false
						, "Description"_o= "Create missing repositories when applying policies.\n"
					}
					, "UpdateLfsReleaseIndexes?"_o=
					{
						"Names"_o= _o["--update-lfs-release-indexes"]
						, "Default"_o= false
						, "Description"_o= "Update lfs release indexes on remotes.\n"
					}
					, "UpdateLfsReleaseIndexesPretend?"_o=
					{
						"Names"_o= _o["--update-lfs-release-indexes-pretend"]
						, "Default"_o= false
						, "Description"_o= "Don't do any actions, just log what would be done.\n"
					}
					, "UpdateLfsReleaseIndexesPruneOrphanedAssets?"_o=
					{
						"Names"_o= _o["--update-lfs-release-indexes-prune-orphaned-assets"]
						, "Default"_o= false
						, "Description"_o= "Remove orphaned LFS assets from releases on hosting provider.\n"
					}
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[GenerateOptions](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_Update(GenerateOptions);
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
			"Names"_o= _o["--repo-name", "-n"]
			, "Default"_o= "*"
			, "Description"_o= "Only run command on repositories that have a name that match the specified wildcard."
		}
	;

	auto Filter_Branch = "RepoBranch?"_o=
		{
			"Names"_o= _o["--repo-branch"]
			, "Default"_o= ""
			, "Description"_o= "Only run command on repositories that are currently on specified branch."
		}
	;

	auto fFilter_Type = [](CStr const &_Default)
		{
			return "RepoType?"_o=
				{
					"Names"_o= _o["--repo-type"]
					, "Default"_o= _Default
					, "Description"_o= "Only run command no repositories of specified type.\n"
					"Repository type is specified in the build system with Repository.Type\n"
				}
			;
		}
	;

	auto Filter_Tags = "RepoTags?"_o=
		{
			"Names"_o= _o["--repo-tags"]
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
					"Names"_o= _o["--repo-only-changed", "-c"]
					, "Default"_o= _bDefault
					, "Description"_o= "Only run command on repositories that have changes.\n"
					"The repository is deemed changed if the branch is not the default branch, if any files have been changed"
					", added or removed (git status -s), or if changes needs to be pushed.\n"
				}
			;
		}
	;

	auto fFilter_IncludePull = [](bool _bDefault)
		{
			return "RepoIncludePull?"_o=
				{
					"Names"_o= _o["--repo-include-pull"]
					, "Default"_o= _bDefault
					, "Description"_o= "When filtering for changed repositories, also include repositories that need to be pulled.\n"
				}
			;
		}
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["status"]
				, "Description"_o= "Get the status of all repositories and update repository states.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"OpenEditor?"_o=
					{
						"Names"_o= _o["--open-editor", "-e"]
						, "Default"_o= false
						, "Description"_o= "Opens the repositories that require action in your repository editor.\n"
						"The editor can be configured in UserSettings.MSettings or UserSettingsGlobal.MSettings. Look for MalterlibRepositoryEditor\n"
					}
					, "UpdateRemotes?"_o=
					{
						"Names"_o= _o["--update-remotes", "-r"]
						, "Default"_o= false
						, "Description"_o= "Fetch all remotes before showing status.\n"
					}
					, "ShowUnchanged?"_o=
					{
						"Names"_o= _o["--show-unchanged", "-u"]
						, "Default"_o= false
						, "Description"_o= "Show status of repositories that does not have any actions you need to take on.\n"
					}
					, "Verbose?"_o=
					{
						"Names"_o= _o["--verbose", "-v"]
						, "Default"_o= false
						, "Description"_o= "Verbose mode, show which files were changed, added or removed and differences to all remotes.\n"
					}
					, "OnlyTracked?"_o=
					{
						"Names"_o= _o["--only-tracked", "-t"]
						, "Default"_o= false
						, "Description"_o= "Show only tracked changes. Otherwise untraked files will be considered as well.\n"
					}
					, "AllBranches?"_o=
					{
						"Names"_o= _o["--all-branches", "-a"]
						, "Default"_o= false
						, "Description"_o= "Show the status of all branches, not just the current branch.\n"
					}
					, "UseDefaultUpstreamBranch?"_o=
					{
						"Names"_o= _o["--use-default-upstream-branch", "-b"]
						, "Default"_o= false
						, "Description"_o= "Compare to the default upstream branch instead of the current branch.\n"
					}
					, "NeedActionOnPush?"_o=
					{
						"Names"_o= _o["--need-action-on-push", "-p"]
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPush", "false") == "true"
						, "Description"_o= "Consider repositories that needs to be pushed to the current branch as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnPushDefault?"_o=
					{
						"Names"_o= _o["--need-action-on-push-default", "-P"]
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPushDefault", "false") == "true"
						, "Description"_o= "Consider repositories that has commits not in the default branch as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnPull?"_o=
					{
						"Names"_o= _o["--need-action-on-pull"]
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnPull", "false") == "true"
						, "Description"_o= "Consider repositories that needs to be pulled as requiring action when --open-editor is specified.\n"
					}
					, "NeedActionOnLocalChanes?"_o=
					{
						"Names"_o= _o["--need-action-on-local-changes"]
						, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_NeedActionOnLocalChanges", "true") == "true"
						, "Description"_o= "Consider repositories that has local changes as requiring action when --open-editor is specified.\n"
					}
					, "NonDefaultToAll?"_o=
					{
						"Names"_o= _o["--non-default-to-all", "-d"]
						, "Default"_o= false
						, "Description"_o= "When on a non-default branch, show push state to all remotes, not just origin.\n"
					}
					, "HideBranches?"_o=
					{
						"Names"_o= _o["--hide-branches", "-H"]
						, "Type"_o= _o[""]
						, "Default"_o= _o[]
						, "Description"_o= "Hide these branchs. Supports wildcards.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fFilter_IncludePull(false)
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
				if (_Params["NeedActionOnPushDefault"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NeedActionOnPushDefault;
				if (_Params["NeedActionOnPull"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NeedActionOnPull;
				if (_Params["NeedActionOnLocalChanes"].f_Boolean())
					Flags |= CBuildSystem::ERepoStatusFlag_NeedActionOnLocalChanges;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				auto HideBranches = _Params["HideBranches"].f_StringArray();

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_Status(GenerateOptions, RepoFilter, Flags, HideBranches, _pCommandLine);
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
					"Names"_o= _o["git"]
					, "Description"_o= "Run git for all repositories.\n"
					, "Category"_o= "Repository management"
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "Options"_o=
					{
						"Synchronous?"_o=
						{
							"Names"_o= _o["--sync", "-s"]
							, "Default"_o= false
							, "Description"_o= "Run all git commands synchronously. By default all git invocations will bu run in parallel.\n"
						}
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fFilter_IncludePull(false)
						, fs_CachedEnvironmentOption(true)
					}
					, "Parameters"_o=
					{
						"GitParameters...?"_o=
						{
							"Type"_o= _o[""]
							, "Default"_o= _o[]
							, "Description"_o= "The parameters to send to git.\n"
						}
					}
				}
				, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
							[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
							{
								co_return co_await _pBuildSystem->f_Action_Repository_ForEachRepo(GenerateOptions, RepoFilter, bParallel, GitParameters);
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
				"Names"_o= _o["repo-commit"]
				, "Description"_o= "Create 'Update repositories' commits for config files with descriptive messages generated from child-repo commits.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"SkipCi?"_o=
					{
						"Names"_o= _o["--skip-ci"]
						, "Default"_o= false
						, "Description"_o= "Add [skip ci] to commit message.\n"
					}
					, "MaxCommitsPerSection?"_o=
					{
						"Names"_o= _o["--max-commits-per-section"]
						, "Default"_o= 20
						, "Description"_o= "Maximum number of commits to show per section in the commit message.\n"
					}
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::ERepoCommitFlag Flags = CBuildSystem::ERepoCommitFlag_None;
				if (_Params["SkipCi"].f_Boolean())
					Flags |= CBuildSystem::ERepoCommitFlag_SkipCi;

				uint32 MaxCommitsPerSection = _Params["MaxCommitsPerSection"].f_Integer();

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_CommitRepos(GenerateOptions, Flags, MaxCommitsPerSection);
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
				"Names"_o= _o["format"]
				, "Description"_o=
					"Format the sources of every repository that sets Repository.Format, as MTool Format does for one: "
					"the files that opt in with malterlib_format = malterlib, malterlib-c or malterlib-buildsystem in .editorconfig, found under the repository root.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Check?"_o=
					{
						"Names"_o= _o["--check"]
						, "Default"_o= false
						, "Description"_o= "Report formatting violations without writing any file.\n"
					}
					, "Diff?"_o=
					{
						"Names"_o= _o["--diff"]
						, "Default"_o= false
						, "Description"_o= "Print the proposed patches without writing any file.\n"
					}
					, "Jobs?"_o=
					{
						"Names"_o= _o["--jobs", "-j"]
						, "Default"_o= 0
						, "Description"_o= "Maximum number of files formatted concurrently in a repository. Zero selects a bounded host-capacity default.\n"
					}
					, "Slowest?"_o=
					{
						"Names"_o= _o["--slowest"]
						, "Default"_o= 0
						, "Description"_o= "After the run, list this many of the files the formatting engine took the longest on, with its times.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				// The time reported covers finding the repositories as well as formatting them.
				CStopwatch Stopwatch{true};
				bool bCheck = _Params["Check"].f_Boolean();
				bool bDiff = _Params["Diff"].f_Boolean();
				if (bCheck && bDiff)
				{
					*_pCommandLine %= "--check and --diff cannot be combined\n";

					co_return 2;
				}

				NMib::NTool::NFormat::CFormatOptions Options;
				if (bCheck)
					Options.m_Mode = NMib::NTool::NFormat::EFormatMode::mc_Check;
				else if (bDiff)
					Options.m_Mode = NMib::NTool::NFormat::EFormatMode::mc_Diff;

				if (_Params["Jobs"].f_Integer() < 0 || _Params["Slowest"].f_Integer() < 0)
				{
					*_pCommandLine %= "--jobs and --slowest cannot be negative\n";

					co_return 2;
				}

				Options.m_nJobs = umint(_Params["Jobs"].f_Integer());
				if (!Options.m_nJobs)
					Options.m_nJobs = NMib::NTool::NFormat::fg_GetDefaultFormatJobs();

				Options.m_nSlowest = umint(_Params["Slowest"].f_Integer());

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);
				RepoFilter.m_bFormat = true;

				// One run formats every repository at once, so the walks and the formatting
				// spread over the cores together; a violation is not an error of the build
				// system, so the exit code comes back beside its result.
				NStorage::TCSharedPointer<NMib::NTool::CFormatOutcome> pOutcome = fg_Construct();
				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				auto Code = co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							TCVector<CStr> Locations;
							if (auto Retry = co_await _pBuildSystem->f_Action_Repository_GetLocations(GenerateOptions, RepoFilter, Locations); Retry != CBuildSystem::ERetry_None)
								co_return Retry;

							if (Locations.f_IsEmpty())
							{
								*_pCommandLine %= "No repository sets Repository.Format\n";

								co_return CBuildSystem::ERetry_None;
							}

							TCVector<NMib::NTool::NFormat::CFormatSelection> Selections;
							for (auto &Location : Locations)
							{
								auto &Selection = Selections.f_Insert();
								Selection.m_WorkingDirectory = fg_Move(Location);
								Selection.m_Patterns = {"*"};
								Selection.m_bRecursive = true;
								Selection.m_bOwnRepository = true;
							}

							NMib::NTool::NFormat::CFormatSink Sink;
							Sink.m_fReport = [_pCommandLine](CStr const &_Text)
								{
									*_pCommandLine %= _Text;
								}
							;
							// A patch is written as bytes: a NUL in a changed line would end text written to a console.
							Sink.m_fPatch = [_pCommandLine](CStr const &_Text)
								{
									NContainer::CByteVector Bytes;
									Bytes.f_Insert((uint8 const *)_Text.f_GetStr(), _Text.f_GetLen());
									*_pCommandLine += Bytes;
								}
							;
							// The repositories' patches go together, each named from the workspace.
							auto RunOptions = Options;
							RunOptions.m_pCancelled = _pBuildSystem->f_GetCancelledPointer();
							RunOptions.m_PatchRoot = _pBuildSystem->f_GetBaseDir();
							auto Ran = co_await NMib::NTool::NFormat::fg_RunFormat(fg_Move(Selections), fg_Move(RunOptions), fg_Move(Sink)).f_Wrap();
							co_await _pBuildSystem->f_CheckCancelled();

							// A selection that fails, as a directory that cannot be listed does, is an operational failure too.
							if (!Ran)
							{
								*_pCommandLine %= "{}\n"_f << Ran.f_GetExceptionStr();
								pOutcome->m_ExitCode = 2;

								co_return CBuildSystem::ERetry_None;
							}

							auto &Run = *Ran;
							*_pCommandLine %= NMib::NTool::NFormat::fg_DescribeFormatSummary(Run.m_Summary, Options.m_Mode, Stopwatch.f_GetTime());
							if (Run.m_Summary.m_nFailed)
							{
								// A file that could not be formatted is an operational failure, told apart from a violation as MTool
								// Format tells it.
								*_pCommandLine %= "{} file(s) could not be formatted\n"_f << Run.m_Summary.m_nFailed;
								pOutcome->m_ExitCode = 2;

								co_return CBuildSystem::ERetry_None;
							}

							pOutcome->m_ExitCode = Run.m_ExitCode;

							co_return CBuildSystem::ERetry_None;
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;

				co_return Code ? Code : pOutcome->m_ExitCode;
			}
		)
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["validate"]
				, "Description"_o=
					"Validate the text files of every repository that sets Repository.Format, as MTool Validate does for one, "
					"in one run with one summary.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Staged?"_o=
					{
						"Names"_o= _o["--staged"]
						, "Default"_o= false
						, "Description"_o= "Only validate added or modified lines in each index, using staged .editorconfig files.\n"
					}
					, "Base?"_o=
					{
						"Names"_o= _o["--base"]
						, "Type"_o= ""
						, "Description"_o= "Validate committed changes from the merge base with this reference to HEAD, using HEAD's .editorconfig files.\n"
					}
					, "Summary?"_o=
					{
						"Names"_o= _o["--summary"]
						, "Default"_o= !NMib::NTool::NValidate::fg_IsRunningUnderGitHook()
						, "Description"_o= "End with the summary line. Defaults to off when run by a git hook.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CStopwatch Stopwatch{true};
				bool bStaged = _Params["Staged"].f_Boolean();
				bool bSummary = _Params["Summary"].f_Boolean();
				CStr Base;
				if (auto pBase = _Params.f_GetMember("Base"))
				{
					if (bStaged || pBase->f_String().f_IsEmpty())
					{
						*_pCommandLine %= bStaged ? "--base and --staged cannot be combined\n" : "--base requires a nonempty commit reference\n";

						co_return 2;
					}

					Base = pBase->f_String();
				}

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);
				RepoFilter.m_bFormat = true;

				// The repositories are validated at once on one pool of workers. Each one's
				// diagnostics are kept apart and written in repository order once all are
				// done, so a parallel run reads the same from one run to the next.
				NStorage::TCSharedPointer<NMib::NTool::CFormatOutcome> pOutcome = fg_Construct();
				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				auto Code = co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							TCVector<CStr> Locations;
							if (auto Retry = co_await _pBuildSystem->f_Action_Repository_GetLocations(GenerateOptions, RepoFilter, Locations); Retry != CBuildSystem::ERetry_None)
								co_return Retry;

							if (Locations.f_IsEmpty())
							{
								*_pCommandLine %= "No repository sets Repository.Format\n";

								co_return CBuildSystem::ERetry_None;
							}

							// An audit needs no git and is one Format run over every repository; the
							// changed-line modes ask git per repository, at once on one pool, and their
							// diagnostics are written in repository order once all are done.
							CStr Kind = Base ? "committed" : bStaged ? "staged" : "text";
							NMib::NTool::NValidate::CValidationCounts Counts;
							umint nFailed = 0;
							if (!bStaged && !Base)
							{
								NMib::NTool::NFormat::CFormatSink Sink;
								Sink.m_fReport = [_pCommandLine](CStr const &_Text)
									{
										*_pCommandLine %= _Text;
									}
								;
								auto Audited = co_await NMib::NTool::NValidate::fg_ValidateRepositories(Locations, fg_Move(Sink), _pBuildSystem->f_GetCancelledPointer()).f_Wrap();
								co_await _pBuildSystem->f_CheckCancelled();

								if (!Audited)
								{
									*_pCommandLine %= "{}\n"_f << Audited.f_GetExceptionStr();
									pOutcome->m_ExitCode = 2;

									co_return CBuildSystem::ERetry_None;
								}

								Counts = Audited->m_Counts;
							}
							else
							{
								auto Workers = NMib::NTool::NFormat::fg_ConstructFormatWorkerPool(NMib::NTool::NFormat::fg_GetDefaultFormatJobs());
								auto DestroyWorkers = co_await fg_AsyncDestroy(Workers);
								TCVector<CStr> Reports;
								Reports.f_SetLen(Locations.f_GetLen());
								TCFutureVector<NMib::NTool::NValidate::CValidationResult> Pending;
								for (umint i = 0; i < Locations.f_GetLen(); ++i)
								{
									NMib::NTool::NFormat::CFormatSink Sink;
									Sink.m_fReport = [pReport = &Reports[i]](CStr const &_Text)
										{
											*pReport += _Text;
										}
									;
									NMib::NTool::NValidate::fg_ValidateChanges(Locations[i], Base, Workers, fg_Move(Sink)) > Pending;
								}

								auto Outcomes = co_await fg_AllDoneWrapped(Pending);
								for (umint i = 0; i < Locations.f_GetLen(); ++i)
								{
									*_pCommandLine %= Reports[i];
									if (Outcomes[i])
										Counts += Outcomes[i]->m_Counts;
									else
									{
										*_pCommandLine %= "{}: {}\n"_f << Locations[i] << Outcomes[i].f_GetExceptionStr();
										++nFailed;
									}
								}
							}

							co_await _pBuildSystem->f_CheckCancelled();

							*_pCommandLine %= NMib::NTool::NValidate::fg_DescribeValidationFailure(Kind, Counts);
							if (bSummary)
								*_pCommandLine %= NMib::NTool::NValidate::fg_DescribeValidationSummary(Kind, Counts, Stopwatch.f_GetTime());
							if (nFailed)
							{
								*_pCommandLine %= "{} {} could not be validated\n"_f << nFailed << (nFailed == 1 ? "repository" : "repositories");
								pOutcome->m_ExitCode = 2;

								co_return CBuildSystem::ERetry_None;
							}

							pOutcome->m_ExitCode = Counts.f_GetErrors() || Counts.m_nFormatFailed ? 1 : 0;

							co_return CBuildSystem::ERetry_None;
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;

				co_return Code ? Code : pOutcome->m_ExitCode;
			}
		)
	;

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["tidy"]
				, "Description"_o=
					"Run clang-tidy over the sources of every repository that sets Repository.Tidy.Enabled, with the compile commands the build system "
					"generated for their targets. The .clang-tidy files of the sources choose the checks. A file is checked when the deepest repository "
					"it is in sets Repository.Tidy.Enabled, and its path relative to that repository matches Repository.Tidy.IncludeFiles, if there are "
					"any, and not Repository.Tidy.ExcludeFiles. A source's compile reports the source and the checked headers of every repository, as "
					"the checks plugin does in a build: a problem in a template shows where it is instantiated.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Mode?"_o=
					{
						"Names"_o= _o["--mode"]
						, "Default"_o= "file"
						, "Description"_o=
							"Which compile commands are checked:\n"
							"@Indent=11\r"
							"   file:    Each source once, with the command of the best target and configuration: the targets of --target first, then "
							"those of --workspace, then any other; the configuration by name first, then by base name, then any.\r"
							"   target:  Every source of the targets of --target, each with its database in --workspace, or else in its highest "
							"priority workspace.\r"
							"   all:     Every source of every target, once per distinct command, so each source is checked with every set of defines it "
							"is built with.\r"
					}
					, "Target?"_o=
					{
						"Names"_o= _o["--target"]
						, "Type"_o= _o[""]
						, "Default"_o= _o[]
						, "Description"_o= "The targets, as a comma-separated list of wildcards: those checked by --mode target, and those preferred by --mode file.\n"
					}
					, "Workspace?"_o=
					{
						"Names"_o= _o["--workspace"]
						, "Default"_o= ""
						, "Description"_o= "The workspace, as a wildcard: the only one checked by --mode target and all, and the one preferred by --mode file.\n"
					}
					, "Config?"_o=
					{
						"Names"_o= _o["--config"]
						, "Type"_o= _o[""]
						, "Default"_o= _o[]
						, "Description"_o=
							"The configurations, as a comma-separated list of \"<Platform> <Architecture> <Configuration>\" or names of configuration files, "
							"each checked in turn for more coverage. Without one, each target is checked with its highest priority configuration.\n"
					}
					, "Checks?"_o=
					{
						"Names"_o= _o["--checks"]
						, "Default"_o= ""
						, "Description"_o= "Checks added to those the .clang-tidy files enable, like '-*,malterlib-function-naming'.\n"
					}
					, "Staged?"_o=
					{
						"Names"_o= _o["--staged"]
						, "Default"_o= false
						, "Description"_o= "Only check the files changed in each index, and the sources that include them.\n"
					}
					, "Base?"_o=
					{
						"Names"_o= _o["--base"]
						, "Type"_o= ""
						, "Description"_o= "Only check the files changed from the merge base with this reference to HEAD, and the sources that include them.\n"
					}
					, "Fix?"_o=
					{
						"Names"_o= _o["--fix"]
						, "Default"_o= false
						, "Description"_o= "Apply the fixes the checks offer. The Malterlib checks do not rename, so no fix renames a declaration.\n"
					}
					, "Jobs?"_o=
					{
						"Names"_o= _o["--jobs", "-j"]
						, "Default"_o= 0
						, "Description"_o= "Maximum number of clang-tidy processes run at once. Zero selects the number of cores.\n"
					}
					, "Sorted?"_o=
					{
						"Names"_o= _o["--sorted"]
						, "Default"_o= false
						, "Description"_o= "Report the diagnostics in path order once all are checked, instead of as each source is, so runs compare.\n"
					}
					, "ClangTidy?"_o=
					{
						"Names"_o= _o["--clang-tidy"]
						, "Default"_o= ""
						, "Description"_o= "The clang-tidy executable. Defaults to the one of the MalterlibLLVM binaries of the workspace.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CStopwatch Stopwatch{true};
				NMib::NTool::NTidy::CTidyOptions Options;

				CStr Mode = _Params["Mode"].f_String();
				if (Mode == "file")
					Options.m_Mode = NMib::NTool::NTidy::ETidyMode::mc_File;
				else if (Mode == "target")
					Options.m_Mode = NMib::NTool::NTidy::ETidyMode::mc_Target;
				else if (Mode == "all")
					Options.m_Mode = NMib::NTool::NTidy::ETidyMode::mc_AllTargets;
				else
				{
					*_pCommandLine %= "Unknown --mode '{}': use file, target or all\n"_f << Mode;

					co_return 2;
				}

				for (auto &Target : _Params["Target"].f_Array())
					Options.m_Targets.f_Insert(Target.f_String());
				for (auto &Configuration : _Params["Config"].f_Array())
					Options.m_Configurations.f_Insert(Configuration.f_String());
				if (Options.m_Mode == NMib::NTool::NTidy::ETidyMode::mc_Target && Options.m_Targets.f_IsEmpty())
				{
					*_pCommandLine %= "--mode target requires --target\n";

					co_return 2;
				}

				Options.m_Workspace = _Params["Workspace"].f_String();
				Options.m_Checks = _Params["Checks"].f_String();
				Options.m_bStaged = _Params["Staged"].f_Boolean();
				Options.m_bFix = _Params["Fix"].f_Boolean();
				Options.m_bSorted = _Params["Sorted"].f_Boolean();
				if (auto pBase = _Params.f_GetMember("Base"))
				{
					if (Options.m_bStaged || pBase->f_String().f_IsEmpty())
					{
						*_pCommandLine %= Options.m_bStaged ? "--base and --staged cannot be combined\n" : "--base requires a nonempty commit reference\n";

						co_return 2;
					}

					Options.m_Base = pBase->f_String();
				}

				if (_Params["Jobs"].f_Integer() < 0)
				{
					*_pCommandLine %= "--jobs cannot be negative\n";

					co_return 2;
				}

				Options.m_nJobs = umint(_Params["Jobs"].f_Integer());
				if (!Options.m_nJobs)
					Options.m_nJobs = fg_Max(umint(NSys::fg_Thread_GetVirtualCores()), umint(1));

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);
				RepoFilter.m_bTidy = true;

				// A finding is not an error of the build system, so the exit code comes back beside its result.
				NStorage::TCSharedPointer<NMib::NTool::CFormatOutcome> pOutcome = fg_Construct();
				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				CStr ClangTidy = _Params["ClangTidy"].f_String();
				auto Code = co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							auto RunOptions = Options;
							TCMap<CStr, CBuildSystem::CRepoTidyFiles> TidyFiles;
							if (auto Retry = co_await _pBuildSystem->f_Action_Repository_GetLocations
									(
										GenerateOptions
										, RepoFilter
										, RunOptions.m_CheckedRepositories
										, &RunOptions.m_AllRepositories
										, &TidyFiles
									)
								; Retry != CBuildSystem::ERetry_None
							)
							{
								co_return Retry;
							}

							for (auto iFiles = TidyFiles.f_GetIterator(); iFiles; ++iFiles)
							{
								if (iFiles->m_IncludeFiles.f_IsEmpty() && iFiles->m_ExcludeFiles.f_IsEmpty())
									continue;
								auto &Files = RunOptions.m_RepositoryFiles[iFiles.f_GetKey()];
								Files.m_IncludeFiles = fg_Move(iFiles->m_IncludeFiles);
								Files.m_ExcludeFiles = fg_Move(iFiles->m_ExcludeFiles);
							}

							if (RunOptions.m_CheckedRepositories.f_IsEmpty())
							{
								*_pCommandLine %= "No repository sets Repository.Tidy.Enabled\n";

								co_return CBuildSystem::ERetry_None;
							}

							CStr BinDirectory = _pBuildSystem->f_GetBaseDir() / "Binaries/MalterlibLLVM/HostPlatformArchitecture/bin";
							if (ClangTidy)
								RunOptions.m_ClangTidy = CFile::fs_GetFullPath(ClangTidy, _Params["CurrentDirectory"].f_String());
							else
								RunOptions.m_ClangTidy = BinDirectory / ("clang-tidy" + CFile::mc_ExecutableExtension);
							RunOptions.m_ApplyReplacements = CFile::fs_GetPath(RunOptions.m_ClangTidy) / ("clang-apply-replacements" + CFile::mc_ExecutableExtension);
							RunOptions.m_ConfigStore = GenerateOptions.m_Settings.m_OutputDir / "ConfigStore";
							RunOptions.m_pCancelled = _pBuildSystem->f_GetCancelledPointer();
							RunOptions.m_bShowProgress = _pBuildSystem->f_ShowProgress();
							RunOptions.m_bAnsiProgress = (_pBuildSystem->f_AnsiFlags() & NCommandLine::EAnsiEncodingFlag_Color) != NCommandLine::EAnsiEncodingFlag_None;

							NMib::NTool::NFormat::CFormatSink Sink;
							Sink.m_fReport = [_pCommandLine](CStr const &_Text)
								{
									*_pCommandLine %= _Text;
								}
							;
							auto Run = co_await NMib::NTool::NTidy::fg_RunTidy(fg_Move(RunOptions), fg_Move(Sink));
							*_pCommandLine %= NMib::NTool::NTidy::fg_DescribeTidySummary(Run.m_Summary, Stopwatch.f_GetTime());
							pOutcome->m_ExitCode = Run.m_ExitCode;

							co_return CBuildSystem::ERetry_None;
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;

				co_return Code ? Code : pOutcome->m_ExitCode;
			}
		)
	;

	{
		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["repo-run"]
					, "Description"_o= "Run commands with git directory as current path and mib's tool directory prepended to PATH.\n"
					, "Category"_o= "Repository management"
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "Options"_o=
					{
						"Synchronous?"_o=
						{
							"Names"_o= _o["--sync", "-s"]
							, "Default"_o= false
							, "Description"_o= "Run all commands synchronously. By default all invocations will bu run in parallel.\n"
						}
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fFilter_IncludePull(false)
						, fs_CachedEnvironmentOption(true)
						, "Shell?"_o=
						{
							"Names"_o= _o["--shell"]
							, "Default"_o= fg_GetSys()->f_GetEnvironmentVariable("SHELL", "")
							, "Description"_o= "Run commands in this shell.\n"
						}
						, "ShellOptions?"_o=
						{
							"Names"_o= _o["--shell-options"]
							, "Default"_o= _o["-c"]
							, "Description"_o= "The options to send to the shell.\n"
						}
						, "ShellParamsInString?"_o=
						{
							"Names"_o= _o["--shell-params-in-string"]
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
							"Type"_o= _o[""]
							, "Default"_o= _o[]
							, "Description"_o= "The parameters to send to the command.\n"
						}
					}
				}
				, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
							[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
							{
								co_return co_await _pBuildSystem->f_Action_Repository_ForEachRepoDir(GenerateOptions, RepoFilter, Options);
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
				"Names"_o= _o["--pretend", "-p"]
				, "Default"_o= false
				, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
			}
		;

		auto Option_Force = "Force?"_o=
			{
				"Names"_o= _o["--force", "-f"]
				, "Default"_o= false
				, "Description"_o= "Overwrite any destination branches that already exists.\n"
			}
		;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["branch"]
					, "Description"_o= "Check out branch for root repository and update all repositories.\n"
					"Use --force to check out the branch for matching repositories using git checkout -B,"
					" keeping the current HEAD and any untracked files.\n"
					"Repository filters are only available with --force.\n"
					, "Category"_o= "Repository management"
					, "Options"_o=
					{
						Option_Pretend
						, Option_Force
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fFilter_IncludePull(false)
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
				, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
							[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
							{
								co_return co_await _pBuildSystem->f_Action_Repository_Branch(GenerateOptions, RepoFilter, Branch, Flags);
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
					"Names"_o= _o["unbranch"]
					, "Description"_o= "Check out the default branch for root repository and update all repositories.\n"
					"Use --force to check out the default branch for matching repositories using git checkout -B,"
					" keeping the current HEAD and any untracked files.\n"
					"Repository filters are only available with --force.\n"
					, "Category"_o= "Repository management"
					, "Options"_o=
					{
						Option_Pretend
						, Option_Force
						, Filter_Name
						, fFilter_Type("")
						, Filter_Tags
						, Filter_Branch
						, fFilter_OnlyChanged(false)
						, fFilter_IncludePull(false)
						, fs_CachedEnvironmentOption(true)
					}
				}
				, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
							[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
							{
								co_return co_await _pBuildSystem->f_Action_Repository_Unbranch(GenerateOptions, RepoFilter, Flags);
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
				"Names"_o= _o["cleanup-branches"]
				, "Description"_o= "Clean up branches that have been pushed.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Pretend?"_o=
					{
						"Names"_o= _o["--pretend", "-p"]
						, "Default"_o= false
						, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "AllRemotes?"_o=
					{
						"Names"_o= _o["--all-remotes", "-a"]
						, "Default"_o= false
						, "Description"_o= "Also delete branches on remotes specified as writable.\n"
					}
					, "UpdateRemotes?"_o=
					{
						"Names"_o= _o["--update-remotes", "-r"]
						, "Default"_o= false
						, "Description"_o= "Fetch all remotes before determining what to delete.\n"
					}
					, "Verbose?"_o=
					{
						"Names"_o= _o["--verbose", "-v"]
						, "Default"_o= false
						, "Description"_o= "List branches that are not deleted and why.\n"
					}
					, "Force?"_o=
					{
						"Names"_o= _o["--force", "-f"]
						, "Default"_o= false
						, "Description"_o= "Delete branches even though they are not merged to $(remote)/$(default_branch).\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fFilter_IncludePull(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"Branches...?"_o=
					{
						"Type"_o= _o[""]
						, "Default"_o= _o[]
						, "Description"_o= "The branches to clean up. Uses wildcards. Leave empty to clean up all branches.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CBuildSystem::ERepoCleanupBranchesFlag Flags = CBuildSystem::ERepoCleanupBranchesFlag_None;

				TCVector<CStr> Branches;
				for (auto &BranchJson : _Params["Branches"].f_Array())
					Branches.f_Insert(BranchJson.f_String());

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
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_CleanupBranches(GenerateOptions, RepoFilter, Flags, Branches);
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
				"Names"_o= _o["cleanup-tags"]
				, "Description"_o= "Clean up tags that are ancestors of the default branch.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Pretend?"_o=
					{
						"Names"_o= _o["--pretend", "-p"]
						, "Default"_o= false
						, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "AllRemotes?"_o=
					{
						"Names"_o= _o["--all-remotes", "-a"]
						, "Default"_o= false
						, "Description"_o= "Also delete tags on remotes specified as writable.\n"
					}
					, "UpdateRemotes?"_o=
					{
						"Names"_o= _o["--update-remotes", "-r"]
						, "Default"_o= false
						, "Description"_o= "Fetch all remotes before determining what to delete.\n"
					}
					, "Verbose?"_o=
					{
						"Names"_o= _o["--verbose", "-v"]
						, "Default"_o= false
						, "Description"_o= "List tags that are not deleted and why.\n"
					}
					, "Force?"_o=
					{
						"Names"_o= _o["--force", "-f"]
						, "Default"_o= false
						, "Description"_o= "Delete tags even though they are not ancestors of $(remote)/$(default_branch).\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fFilter_IncludePull(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"Tags...?"_o=
					{
						"Type"_o= _o[""]
						, "Default"_o= _o[]
						, "Description"_o= "The tags to clean up. Leave empty to clean up all tags. Uses wildcards.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CBuildSystem::ERepoCleanupTagsFlag Flags = CBuildSystem::ERepoCleanupTagsFlag_None;

				TCVector<CStr> Tags;
				for (auto &TagJson : _Params["Tags"].f_Array())
					Tags.f_Insert(TagJson.f_String());

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
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_CleanupTags(GenerateOptions, RepoFilter, Flags, Tags);
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
				"Names"_o= _o["push"]
				, "Description"_o= "Push all repositories that need pushing.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Pretend?"_o=
					{
						"Names"_o= _o["--pretend", "-p"]
						, "Default"_o= false
						, "Description"_o= "Only pretend to run the command, only report the actions that would be taken.\n"
					}
					, "FollowTags?"_o=
					{
						"Names"_o= _o["--follow-tags"]
						, "Default"_o= true
						, "Description"_o= "Also push tags that are reachable from the refs pushed.\n"
					}
					, "NonDefaultToAll?"_o=
					{
						"Names"_o= _o["--non-default-to-all", "-d"]
						, "Default"_o= false
						, "Description"_o= "When on a non-default branch, push to all remotes, not just origin.\n"
					}
					, "Force?"_o=
					{
						"Names"_o= _o["--force", "-f"]
						, "Default"_o= false
						, "Description"_o= "Force push to remotes.\n"
					}
					, "PushPulls?"_o=
					{
						"Names"_o= _o["--push-pulls"]
						, "Default"_o= false
						, "Description"_o= "Also push branches whose same-named remote branch would show as needing pull in status, but only when the local branch tip is reachable from the remote default branch.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fFilter_IncludePull(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"Remotes...?"_o=
					{
						"Type"_o= _o[""]
						, "Default"_o= _o[]
						, "Description"_o= "The remotes to push to. By default pushes to all remotes.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
				if (_Params["PushPulls"].f_Boolean())
					Flags |= CBuildSystem::ERepoPushFlag_PushPulls;

				TCVector<CStr> Remotes;
				for (auto &Param : _Params["Remotes"].f_Array())
					Remotes.f_Insert(Param.f_String());

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_Push(GenerateOptions, RepoFilter, Remotes, Flags);
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
				"Names"_o= _o["list-commits"]
				, "Description"_o= "List commits in all repositories between two commits in main repository.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Local?"_o=
					{
						"Names"_o= _o["--local", "-l"]
						, "Default"_o= false
						, "Description"_o= "Don't fetch all remotes before listing commits.\n"
					}
					, "Compact?"_o=
					{
						"Names"_o= _o["--compact"]
						, "Default"_o= false
						, "Description"_o= "If possible, make size columns to fit content.\n"
					}
					, "ChangeLog?"_o=
					{
						"Names"_o= _o["--changelog"]
						, "Default"_o= false
						, "Description"_o= "List all commits sorted by date.\n"
					}
					, "Columns?"_o=
					{
						"Names"_o= _o["--columns"]
						, "Default"_o= ""
						, "Description"_o= "Add columns that extract data from .\n"
						"Format: [Name:Wildcard[;Name:Wildcard]...]\n"
					}
					, "Prefix?"_o=
					{
						"Names"_o= _o["--prefix"]
						, "Default"_o= ""
						, "Description"_o= "Prefix to put in front of every output line.\n"
					}
					, "MaxCommits?"_o=
					{
						"Names"_o= _o["--max-commits"]
						, "Default"_o= 50
						, "Description"_o= "Max commits to display for non-main repositories.\n"
					}
					, "MaxCommitsMain?"_o=
					{
						"Names"_o= _o["--max-commits-main"]
						, "Default"_o= 500
						, "Description"_o= "Max commits to display for main repository.\n"
					}
					, "MaxMessageWidth?"_o=
					{
						"Names"_o= _o["--max-message-width"]
						, "Default"_o= 60
						, "Description"_o= "Max width of the message column.\n"
					}
					, Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fFilter_IncludePull(false)
					, fs_CachedEnvironmentOption(true)
				}
				, "Parameters"_o=
				{
					"FromReference?"_o=
					{
						"Default"_o= "origin/master"
						, "Description"_o= "The commit to start showing commits from.\n"
						"For Perforce roots, use @changelist (e.g. @12345). Default auto-detects previous changelist.\n"
					}
					, "ToReference?"_o=
					{
						"Default"_o= "HEAD"
						, "Description"_o= "The commit to end showing commits to.\n"
						"For Perforce roots, use @changelist (e.g. @12345). Default auto-detects latest changelist.\n"
					}
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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
						[&](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_ListCommits
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
				"Names"_o= _o["release-package"]
				, "Description"_o= "Release package to repository hosting provider.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					Filter_Name
					, fFilter_Type("")
					, Filter_Tags
					, Filter_Branch
					, fFilter_OnlyChanged(false)
					, fFilter_IncludePull(false)
					, fs_CachedEnvironmentOption(true)
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[&](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_ReleasePackage
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
