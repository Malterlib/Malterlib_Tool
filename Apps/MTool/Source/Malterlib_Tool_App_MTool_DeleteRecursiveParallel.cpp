// Copyright © 2023 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Process/ProcessLaunchActor>

namespace NDeleteRecursive
{
	constinit NAtomic::TCAtomic<mint> g_nBlockingActors{0};

	template <typename tf_CResult, typename tf_CContainer, typename tf_CFunctor>
	auto DMibWorkaroundUBSanSectionErrorsDisable fg_ParallelForEachBlocking(tf_CContainer &&_Container, tf_CFunctor &&_fFunctor, mint _MaxConcurrency)
		-> TCUnsafeFuture<TCConditional<NTraits::cIsVoid<tf_CResult>, tf_CResult, TCVector<tf_CResult>>>
	{
		co_await ECoroutineFlag_CaptureMalterlibExceptions;

		if (_Container.f_IsEmpty())
			co_return {};

		mint nSplit = 1;
		mint nValues = _Container.f_GetLen();
		while (nSplit < nValues)
		{
			auto nActors = g_nBlockingActors.f_Load();
			if (nActors >= (_MaxConcurrency - 1))
				break;

			auto nWanted = fg_Min(nValues - nSplit, (_MaxConcurrency - 1) - nActors);

			if (g_nBlockingActors.f_CompareExchangeStrong(nActors, nActors + nWanted))
			{
				nSplit += nWanted;
				break;
			}
		}

		TCFutureVector<tf_CResult> Results;

		if (nSplit > 1)
		{
			auto Cleanup = g_OnScopeExit / [&]
				{
					g_nBlockingActors -= nSplit - 1;
				}
			;

			TCVector<CBlockingActorCheckout> Checkouts;
			Checkouts.f_Reserve(nSplit - 1);
			for (mint i = 1; i < nSplit; ++i)
				Checkouts.f_Insert(fg_BlockingActor());

			mint iSplit = 0;

			for (auto &Value : _Container)
			{
				if (iSplit == 0)
				{
					++iSplit;
					continue;
				}

				auto iCheckout = iSplit;
				if (++iSplit == nSplit)
					iSplit = 0;

				g_Dispatch(Checkouts[iCheckout - 1]) / [&, pValue = &Value]()
					{
						return _fFunctor(*pValue);
					}
					> Results
				;
			}

			for
				(
					auto pStartArray = _Container.f_GetArray()
					, pEndArray = pStartArray + _Container.f_GetLen()
					, pValue = pStartArray
					; pValue < pEndArray && pValue >= pStartArray
					; pValue += nSplit
				)
			{
				_fFunctor(*pValue) > Results;
			}

			co_return co_await fg_AllDone(Results);
		}
		else
		{
			for (auto &Value : _Container)
				_fFunctor(Value) > Results;

			co_return co_await fg_AllDone(Results);
		}
	}

	struct CFileToDelete
	{
		template <typename tf_CStr>
		void f_Format(tf_CStr &o_Str) const
		{
			o_Str += m_Path;
		}

		CStr m_Path;
		bool m_bReadOnly = false;
	};

	struct CFoundFiles
	{
		CFoundFiles &operator += (CFoundFiles &&_Other)
		{
			m_FilesOrLinks.f_Insert(fg_Move(_Other.m_FilesOrLinks));
			m_Directories.f_Insert(fg_Move(_Other.m_Directories));

			return *this;
		}

		TCVector<CFileToDelete> m_FilesOrLinks;
		TCVector<CStr> m_Directories;
	};

	TCUnsafeFuture<CFoundFiles> fg_FindFilesRecursiveParallel(CStr const &_Path, mint _MaxConcurrency)
	{
		CFoundFiles Return;

		auto Files = CFile::fs_FindFilesEx(CFile::fs_AppendPath(_Path, "*"), EFileAttrib_File | EFileAttrib_Directory, false);
		// First recurse down
		for (auto &File : Files)
		{
			if ((File.m_Attribs & NFile::EFileAttrib_File) || (File.m_Attribs & NFile::EFileAttrib_Link))
			{
				if (File.m_Attribs & EFileAttrib_ReadOnly)
					Return.m_FilesOrLinks.f_Insert(CFileToDelete{.m_Path = fg_Move(File.m_Path)}).m_bReadOnly = true;
				else
					Return.m_FilesOrLinks.f_Insert(CFileToDelete{.m_Path = fg_Move(File.m_Path)});
			}
			else if (File.m_Attribs & EFileAttrib_Directory)
				Return.m_Directories.f_Insert(fg_Move(File.m_Path));
		}

		if (g_nBlockingActors.f_Load() >= _MaxConcurrency - 1)
		{
			CFoundFiles Results;
			for (auto &Directory : Return.m_Directories)
				Results += co_await fg_FindFilesRecursiveParallel(Directory, _MaxConcurrency);

			Return += fg_Move(Results);
		}
		else
		{
			if (!Return.m_Directories.f_IsEmpty())
			{
				auto Results = co_await fg_ParallelForEachBlocking<CFoundFiles>
					(
						Return.m_Directories
						, [_MaxConcurrency](CStr const &_Directory) -> TCFuture<CFoundFiles>
						{
							return fg_FindFilesRecursiveParallel(_Directory, _MaxConcurrency);
						}
						, _MaxConcurrency
					)
				;

				for (auto &Result : Results)
					Return += fg_Move(Result);
			}

		}

		co_return fg_Move(Return);
	}
}

using namespace NDeleteRecursive;

struct CTool_DeleteRecursiveParallel : public CDistributedTool, public CAllowUnsafeThis
{
public:
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		)
	{
		if (fg_IsMalterlib())
			return;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["DeleteRecursiveParallel"]
					, "Description"_o= "Delete files or directories recursively.\n"
					, "Category"_o= "Legacy"
					, "Options"_o=
					{
						"Verbose?"_o=
						{
							"Names"_o= _o["--verbose", "-v"]
							, "Default"_o= false
							, "Description"_o= "Show which files are deleted.\n"
						}
						, "ShowProgress?"_o=
						{
							"Names"_o= _o["--show-progress"]
							, "Default"_o= false
							, "Description"_o= "Log progress of deleted items every second.\n"
						}
						, "ShowSummary?"_o=
						{
							"Names"_o= _o["--show-summary"]
							, "Default"_o= true
							, "Description"_o= "Log summary of what is going to be deleted and time to finish.\n"
						}
						, "MaxConcurrency?"_o=
						{
							"Names"_o= _o["--max-concurrency"]
							, "Default"_o= 64
							, "Description"_o= "The maximum number of simultaneous operations that should be done in parallel.\n"
						}
						, "ItemChunkSize?"_o=
						{
							"Names"_o= _o["--item-chunk-size"]
							, "Default"_o= 100
							, "Description"_o= "The smallest number of items to schedule in one concurrent operation.\n"
							"Decreasing will improve load balancing, while increasing processing overhead.\n"
						}
					}
					, "Parameters"_o=
					{
						"Destinations..."_o=
						{
							"Type"_o= _o[""]
							, "Description"_o= "The destinations to delete.\n"
						}
					}
				}
				, [=](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					auto Destinations = _Params["Destinations"].f_StringArray();

					bool bVerbose = _Params["Verbose"].f_Boolean();
					bool bShowProgress = _Params["ShowProgress"].f_Boolean();
					bool bShowSummary = _Params["ShowSummary"].f_Boolean();
					mint MaxConcurrency = _Params["MaxConcurrency"].f_Integer();
					mint ItemChunkSize = _Params["ItemChunkSize"].f_Integer();

					CClock TotalClock{true};

					if (bShowSummary)
						*_pCommandLine %= "Deleting with concurrency {}: {vs}\n"_f << MaxConcurrency << Destinations;

					NAtomic::TCAtomic<mint> nItemsDeleted = 0;
					NAtomic::TCAtomic<int64> ClockStartTime = TotalClock.m_StartTime;
					mint nTotalItems = 0;

					auto fLogProgress = [&](bool _bForce = false)
						{
							if (!_bForce)
							{
								CClock Clock;
								Clock.m_StartTime = ClockStartTime.f_Load();

								auto StartTime = Clock.m_StartTime;

								if (Clock.f_GetTime() <= 1.0)
									return;

								Clock.f_AddOffset(1.0);

								if (!ClockStartTime.f_CompareExchangeStrong(StartTime, Clock.m_StartTime))
									return;
							}

							auto CurrentItems = nItemsDeleted.f_Load();

							auto EllapsedTime = fg_SecondsDurationToHumanReadable(TotalClock.f_GetTime());
							auto PercentDone = (fp64(CurrentItems) / fp64(nTotalItems)) * 100.0;

							CUStr ToOutput = CStr("  {} items deleted. {fe0}% done in {}"_f << CurrentItems << PercentDone << EllapsedTime);
							*_pCommandLine %= "{}\x1B[{}D"_f << ToOutput << ToOutput.f_GetLen();
						}
					;

					bool bSuccess = true;
					{
						auto BlockingActorCheckout = fg_BlockingActor();
						bSuccess = co_await
							(
								g_Dispatch(BlockingActorCheckout) /
								[
									Destinations
									, MaxConcurrency
									, ItemChunkSize
									, pCommandLine = _pCommandLine
									, bShowProgress
									, bShowSummary
									, bVerbose
									, TotalClock
									, &nTotalItems
									, &nItemsDeleted
									, &fLogProgress
								]
								() -> TCFuture<bool>
								{
									co_await ECoroutineFlag_CaptureExceptions;

									CFoundFiles Files;

									TCVector<CStr> RootDirectories;

									for (auto &Destination : Destinations)
									{
										if (!CFile::fs_FileExists(Destination))
											continue;

										auto Attribs = CFile::fs_GetAttributes(Destination);

										if (!((Attribs & EFileAttrib_Directory) && !(Attribs & EFileAttrib_Link)))
											Files.m_FilesOrLinks.f_Insert(CFileToDelete{.m_Path = Destination}).m_bReadOnly = !!(Attribs & EFileAttrib_ReadOnly);
										else
											RootDirectories.f_Insert(Destination);
									}

									if (RootDirectories.f_IsEmpty() && Files.m_FilesOrLinks.f_IsEmpty())
										co_return true;

									TCAtomic<bool> bSuccess = true;
									{
										auto AllFoundFiles = co_await fg_ParallelForEachBlocking<CFoundFiles>
											(
												RootDirectories
												, [&](CStr const &_Destination) -> TCUnsafeFuture<CFoundFiles>
												{
													co_return co_await fg_FindFilesRecursiveParallel(_Destination, MaxConcurrency);
												}
												, MaxConcurrency
											)
										;

										for (auto &FoundFiles : AllFoundFiles)
											Files += fg_Move(FoundFiles);
									}

									auto FindTime = TotalClock.f_GetTime();

									nTotalItems = Files.m_FilesOrLinks.f_GetLen() + Files.m_Directories.f_GetLen();

									if (bVerbose)
										*pCommandLine %= "Deleting files: \n{}\n Deleting directories:\n{}\n"_f << Files.m_FilesOrLinks << Files.m_Directories;

									if (bShowSummary)
									{
										*pCommandLine %= "Finding what to delete took {}\n"_f << fg_SecondsDurationToHumanReadable(FindTime);
										*pCommandLine %= "Deleting {ns } files and {ns } directories\n"_f << Files.m_FilesOrLinks.f_GetLen() << Files.m_Directories.f_GetLen();
									}

									Files.m_FilesOrLinks.f_Sort
										(
											[](CFileToDelete const &_Left, CFileToDelete const &_Right)
											{
												return _Left.m_Path <=> _Right.m_Path;
											}
										)
									;

									TCVector<TCVector<CFileToDelete>> FilesChunks;

									for
										(
											auto pFiles = Files.m_FilesOrLinks.f_GetArray()
											, pFilesEnd = Files.m_FilesOrLinks.f_GetArray() + Files.m_FilesOrLinks.f_GetLen()
											; pFiles < pFilesEnd
											; pFiles += ItemChunkSize
										)
									{
										FilesChunks.f_Insert().f_InsertMove(pFiles, fg_Min(ItemChunkSize, mint(pFilesEnd - pFiles)));
									}

									co_await fg_ParallelForEachBlocking<void>
										(
											FilesChunks
											, [&](TCVector<CFileToDelete> const &_Files) -> TCUnsafeFuture<void>
											{
												for (auto &File : _Files)
												{
													try
													{
														if (File.m_bReadOnly)
															CFile::fs_MakeFileWritable(File.m_Path);
														CFile::fs_DeleteFile(File.m_Path);

														if (bShowProgress && ((++nItemsDeleted) % 1000) == 0)
															fLogProgress();
													}
													catch (CExceptionFile const &_Exception)
													{
														bSuccess.f_Store(false);
														*pCommandLine %= "{}\n"_f << _Exception.f_GetErrorStr();
													}
												}

												co_return {};
											}
											, MaxConcurrency
										)
									;

									TCMap<mint, TCVector<TCVector<CStr>>> DirectoriesDepth;

									for (auto &Directory : Files.m_Directories)
									{
										auto pParse = Directory.f_GetStr();
										mint Depth = 0;
										while (*pParse)
										{
											if (*pParse == '/')
												++Depth;
											++pParse;
										}

										auto &Vectors = DirectoriesDepth[Depth];
										if (Vectors.f_IsEmpty() || Vectors.f_GetLast().f_GetLen() >= ItemChunkSize)
											Vectors.f_Insert().f_Reserve(ItemChunkSize);

										Vectors.f_GetLast().f_Insert(fg_Move(Directory));
									}

									for (auto &Directories : DirectoriesDepth.f_GetIteratorReverse())
									{
										co_await fg_ParallelForEachBlocking<void>
											(
												Directories
												, [&](TCVector<CStr> const &_Directories) -> TCUnsafeFuture<void>
												{
													for (auto &Directory : _Directories)
													{
														try
														{
															CFile::fs_DeleteDirectory(Directory);

															if (bShowProgress && ((++nItemsDeleted) % 1000) == 0)
																fLogProgress();
														}
														catch (CExceptionFile const &_Exception)
														{
															bSuccess.f_Store(false);
															*pCommandLine %= "{}\n"_f << _Exception.f_GetErrorStr();
														}
													}

													co_return {};
												}
												, MaxConcurrency
											)
										;
									}

									for (auto &RootDirectory : RootDirectories)
									{
										try
										{
											CFile::fs_DeleteDirectory(RootDirectory);
										}
										catch (CExceptionFile const &_Exception)
										{
											*pCommandLine %= "{}\n"_f << _Exception.f_GetErrorStr();
											co_return false;
										}
									}

									co_return bSuccess.f_Load();
								}
							)
						;
					}

					if (bShowProgress)
					{
						fLogProgress(true);
						*_pCommandLine %= "\n";
					}

					if (bShowSummary)
						*_pCommandLine %= "Finished in {}\n"_f << fg_SecondsDurationToHumanReadable(TotalClock.f_GetTime());

					co_return bSuccess ? 0 : 1;
				}
			)
		;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["CreateDirectoryTree"]
					, "Description"_o= "Create directory tree.\n"
					, "Category"_o= "Legacy"
					, "Options"_o=
					{
						"NumFiles?"_o=
						{
							"Names"_o= _o["--files"]
							, "Default"_o= 1'000'000
							, "Description"_o= "The number of files to create.\n"
						}
						, "NumDirectories?"_o=
						{
							"Names"_o= _o["--directories"]
							, "Default"_o= 100'000
							, "Description"_o= "The number of directories to create.\n"
						}
						, "FanOut?"_o=
						{
							"Names"_o= _o["--fan-out"]
							, "Default"_o= 8
							, "Description"_o= "How many directories that should be created in each directory.\n"
						}
						, "FileSize?"_o=
						{
							"Names"_o= _o["--file-size"]
							, "Default"_o= 1024
							, "Description"_o= "How big each file should be.\n"
						}
					}
					, "Parameters"_o=
					{
						"Destination"_o=
						{
							"Type"_o= ""
							, "Description"_o= "The destination path where the directory tree should be created.\n"
						}
					}
				}
				, [=](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					auto Destination = _Params["Destination"].f_String();
					mint NumFiles = _Params["NumFiles"].f_Integer();
					mint NumDirectories = _Params["NumDirectories"].f_Integer();
					mint FanOut = _Params["FanOut"].f_Integer();
					mint FileSize = _Params["FileSize"].f_Integer();

					CStr FileContents;
					CStr FileLine = gc_Str<"0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789\n">;
					while (mint(FileContents.f_GetLen()) < FileSize)
						FileContents += FileLine.f_Left(fg_Min(mint(FileLine.f_GetLen()), FileSize - FileContents.f_GetLen()));

					{
						auto BlockingActorCheckout = fg_BlockingActor();
						co_await
							(
								g_Dispatch(BlockingActorCheckout) / [=]() -> TCFuture<void>
								{
									co_await ECoroutineFlag_CaptureExceptions;

									auto nLeftToCreate = NumDirectories;

									auto fCreateDirectories = [&](CStr const &_Path)
										{
											TCVector<CStr> Directories;
											for (mint iDirectory = 0; iDirectory < FanOut; ++iDirectory)
											{
												if (nLeftToCreate == 0)
													return Directories;

												--nLeftToCreate;
												CStr DirName = _Path / ("Dir{}"_f << iDirectory);
 												CFile::fs_CreateDirectory(DirName);
												Directories.f_Insert(fg_Move(DirName));
											}

											return Directories;
										}
									;

									auto fCreateDirectoriesRecurse = [&](this auto &_fThis, CStr const &_Path)
										{
											auto LeafDirectories = fCreateDirectories(_Path);

											TCVector<CStr> AllDirectories;

											while (nLeftToCreate)
											{
												TCVector<CStr> NewLeafDirectories;
												for (auto &LeafDirectory : LeafDirectories)
													NewLeafDirectories.f_Insert(fCreateDirectories(LeafDirectory));

												AllDirectories.f_Insert(fg_Move(LeafDirectories));

												LeafDirectories = fg_Move(NewLeafDirectories);
											}

											AllDirectories.f_Insert(fg_Move(LeafDirectories));

											return AllDirectories;
										}
									;

									TCVector<CStr> Directories = fCreateDirectoriesRecurse(Destination);

									mint iDirectory = 0;
									for (mint iFile = 0; iFile < NumFiles; ++iFile)
									{
										CStr Path = Directories[iDirectory] / ("File{}.txt"_f << iFile);
										CFile::fs_WriteStringToFile(Path, FileContents, false);

										++iDirectory;
										if (iDirectory == NumDirectories)
											iDirectory = 0;
									}

									co_return {};
								}
							)
						;
					}

					co_return {};
				}
			)
		;
	}
};

DMibRuntimeClass(CTool, CTool_DeleteRecursiveParallel);
