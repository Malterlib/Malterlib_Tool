// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

#include <Mib/BuildSystem/BuildSystem>

void CTool_Malterlib::f_Register_CheckLicense(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
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
					, "Description"_o= "Only run command on repositories of specified type.\n"
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

	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["check-license"]
				, "Description"_o=
				"Check and optionally fix SPDX license headers in repository files.\n"
				"\n"
				"Verifies that tracked files have the correct SPDX license header, checks that\n"
				"license files are in place, and validates REUSE.toml content. Repositories must\n"
				"have Repository.CheckLicense enabled and Repository.License configured.\n"
				"\n"
				"In check mode (default), reports issues without modifying files.\n"
				"In fix mode (--fix), automatically updates headers, copies license files,\n"
				"and generates REUSE.toml files.\n"
				, "Category"_o= "Repository management"
				, "Options"_o=
				{
					"Fix?"_o=
					{
						"Names"_o= _o["--fix", "-f"]
						, "Default"_o= false
						, "Description"_o= "Fix license headers, copy license files, and generate REUSE.toml.\n"
						"Without this flag the command only reports issues.\n"
					}
					, "ShowAll?"_o=
					{
						"Names"_o= _o["--show-all", "-a"]
						, "Default"_o= false
						, "Description"_o= "Show all repositories, including those with no issues.\n"
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

				CBuildSystem::CRepoFilter RepoFilter = CBuildSystem::CRepoFilter::fs_ParseParams(_Params);

				CBuildSystem::ECheckLicenseFlag Flags = CBuildSystem::ECheckLicenseFlag::mc_None;
				if (_Params["Fix"].f_Boolean())
					Flags = Flags | CBuildSystem::ECheckLicenseFlag::mc_Fix;
				if (_Params["ShowAll"].f_Boolean())
					Flags = Flags | CBuildSystem::ECheckLicenseFlag::mc_ShowAll;

				auto GenerateOptions = fs_ParseSharedOptions(_Params);
				co_return co_await f_RunBuildSystem
					(
						[=](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							co_return co_await _pBuildSystem->f_Action_Repository_CheckLicense(GenerateOptions, RepoFilter, Flags, _pCommandLine);
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;
			}
		)
	;
}
