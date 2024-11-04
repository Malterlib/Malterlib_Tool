// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

#include <Mib/Git/LfsReleaseStore>


void CTool_Malterlib::f_Register_LfsReleaseStore(CDistributedAppCommandLineSpecification &o_CommandLine)
{
	auto Section = o_CommandLine.f_AddSection("LFS Release Store", "LFS release store utilities.", "Default");

	auto Option_WorkingDirectory = "WorkingDirectory?"_o=
		{
			"Names"_o= {"--working-directory", "-C"}
			, "Default"_o= CFile::fs_GetCurrentDirectory()
			, "Description"_o= "The directory of the git repository.\n"
		}
	;

	Section.f_RegisterCommand
		(
			{
				"Names"_o= {"lfs-release-store"}
				, "Description"_o= "Command for running lfs release store.\n"
				, "Options"_o=
				{
					Option_WorkingDirectory
				}
			}
			, [](NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				TCActor<NGit::CLfsReleaseStoreService> LfsService = fg_Construct(_pCommandLine, _Params["WorkingDirectory"].f_String());

				auto Destroy = co_await fg_AsyncDestroy(LfsService);

				co_await LfsService(&NGit::CLfsReleaseStoreService::f_InitService);

				co_await LfsService(&NGit::CLfsReleaseStoreService::f_WaitForExit);

				co_return 0;
			}
		)
	;

	Section.f_RegisterCommand
		(
			{
				"Names"_o= {"lfs-release-update-index"}
				, "Description"_o= "Update LFS release store index.\n"
				, "Options"_o=
				{
					Option_WorkingDirectory
					, "Remote?"_o=
					{
						"Names"_o= {"--remote", "-r"}
						, "Default"_o= "origin"
						, "Description"_o= "The remote to update the index on.\n"
					}
					, "Pretend?"_o=
					{
						"Names"_o= {"--pretend"}
						, "Default"_o= true
						, "Description"_o= "Only log the operations that would have been done, don't do them.\n"
					}
					, "PruneOrphanedAssets?"_o=
					{
						"Names"_o= {"--prune-orphaned-assets"}
						, "Default"_o= false
						, "Description"_o= "Remove LFS assets that are not used anywhere in the repository.\n"
					}
				}
			}
			, [](NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				TCActor<NGit::CLfsReleaseStoreService> LfsService = fg_Construct(_pCommandLine, _Params["WorkingDirectory"].f_String());

				auto Destroy = co_await fg_AsyncDestroy(LfsService);

				NGit::CLfsReleaseStoreService::EUpdateReleaseIndexOption Options = NGit::CLfsReleaseStoreService::EUpdateReleaseIndexOption::mc_None;

				if (_Params["Pretend"].f_Boolean())
					Options |= NGit::CLfsReleaseStoreService::EUpdateReleaseIndexOption::mc_Pretend;

				if (_Params["PruneOrphanedAssets"].f_Boolean())
					Options |= NGit::CLfsReleaseStoreService::EUpdateReleaseIndexOption::mc_PruneOrphanedAssets;

				co_await LfsService
					(
						&NGit::CLfsReleaseStoreService::f_UpdateReleaseIndex
						, _Params["Remote"].f_String()
						, Options
						, [_pCommandLine](NStr::CStr const &_Output)
						{
							*_pCommandLine %= _Output;
						}
					)
				;

				co_return 0;
			}
		)
	;
}
