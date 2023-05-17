// Copyright © 2023 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Process/ProcessLaunchActor>

struct CTool_ResignXcode : public CDistributedTool, public CAllowUnsafeThis
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
					"Names"_= {"ResignXcode"}
					, "Description"_= "Resign Xcode.\n"
					//, "Category"_= "Development Tools"
					, "Options"_=
					{
						"Source"_=
						{
							"Names"_= {"--source"}
							, "Default"_= "/Users/erik/Downloads/Xcode.app"
							, "Description"_= "Xcode source location.\n"
						}
						, "Destination"_=
						{
							"Names"_= {"--destination"}
							, "Default"_= "/Applications/XcodePatched.app"
							, "Description"_= "Xcode destination location.\n"
						}
					}
				}
				, [=](NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					auto Source = _Params["Source"].f_String();
					auto Destination = _Params["Destination"].f_String();

					*_pCommandLine %= "Source      : {}\n"_f << Source;
					*_pCommandLine %= "Destination : {}\n"_f << Destination;

					TCActor<CSeparateThreadActor> FileActor{fg_Construct(), "File Actor"};

					CClock Clock{true};

					CStr StateFile = Destination / "Contents/SignState.json";

					CEJSON SignState = EJSONType_Object;

					if (CFile::fs_FileExists(StateFile))
						SignState = CEJSON::fs_FromString(CFile::fs_ReadStringFromFile(StateFile, true), StateFile);

					mint nFiles = 0;
					mint nFilesSkipped = 0;

					bool bCopyDone = false;
					if (auto *pValue = SignState.f_GetMember("CopyDone"))
						bCopyDone = pValue->f_Boolean();


					TCSet<CStr> ToSignExecutables;

					TCSet<CStr> ToSign;
					if (auto *pValue = SignState.f_GetMember("ToSign"))
						ToSign = TCSet<CStr>::fs_FromContainer(pValue->f_StringArray());

					if (auto *pValue = SignState.f_GetMember("ToSignExecutables"))
						ToSignExecutables = TCSet<CStr>::fs_FromContainer(pValue->f_StringArray());

					auto Cleaup = g_OnScopeExit / [&]
						{
							try
							{
								if (!CFile::fs_FileExists(CFile::fs_GetPath(StateFile)))
									return;

								SignState["ToSign"] = TCVector<CStr>::fs_FromContainer(ToSign);
								SignState["ToSignExecutables"] = TCVector<CStr>::fs_FromContainer(ToSignExecutables);

								CFile::fs_WriteStringToFile(StateFile, SignState.f_ToString(), false);
							}
							catch (...)
							{
							}
						}
					;

					if (!bCopyDone)
					{
						co_await
							(
								g_Dispatch(FileActor) / [&]
								{
									CFile::fs_DiffCopyFileOrDirectory
										(
											Source
											, Destination
											, [&](CFile::EDiffCopyChange _Change, CStr const &_Source, CStr const &_Destination, CStr const &_Link)
											{
												if (_Destination == StateFile)
													return CFile::EDiffCopyChangeAction_Skip;

												if (_Source.f_Find("/CoreSimulator/") >= 0 || _Source.f_Find("/LLDB.framework") >= 0 || _Source.f_EndsWith("/lldb"))
												{
													++nFiles;
													return CFile::EDiffCopyChangeAction_Perform;
												}


												if (_Source.f_EndsWith("/_CodeSignature"))
												{
													CStr ToSignPath = CFile::fs_GetPath(_Destination);
													ToSignPath = ToSignPath.f_RemoveSuffix("/Versions/B");
													ToSignPath = ToSignPath.f_RemoveSuffix("/Versions/A");
													ToSignPath = ToSignPath.f_RemoveSuffix("/Contents");

													ToSign[ToSignPath];
													++nFilesSkipped;
													return CFile::EDiffCopyChangeAction_Skip;
												}
												else if (_Source.f_Find("/_CodeSignature/") >= 0)
												{
													++nFilesSkipped;
													return CFile::EDiffCopyChangeAction_Skip;
												}

												auto Attribs = CFile::fs_GetAttributesOnLink(_Source);
												if (!(Attribs & EFileAttrib_Link) && !(Attribs & EFileAttrib_Directory) && (Attribs & EFileAttrib_UserExecute))
												{
													if (CFile::fs_GetExtension(_Destination) == "")
														ToSignExecutables[_Destination];
												}

												if (Clock.f_GetTime() > 1.0)
												{
													CUStr ToOutput = CStr("  {} files done"_f << nFiles);

													*_pCommandLine %= "{}\x1B[{}D"_f << ToOutput << ToOutput.f_GetLen();
													Clock.f_AddOffset(1.0);
												}

												++nFiles;

												return CFile::EDiffCopyChangeAction_Perform;
											}
											, {} // ExcludePatterns
											, 0.0
										 )
									;
								}
							)
						;

						SignState["CopyDone"] = true;
						*_pCommandLine %= "\n";
					}

					auto fSortByLength = [](CStr const &_Left, CStr const &_Right)
						{
							if (auto Compare = _Right.f_GetLen() <=> _Left.f_GetLen(); Compare != 0)
								return Compare;

							return _Left <=> _Right;
						}
					;

					auto ToSignVector = TCVector<CStr>::fs_FromContainer(ToSign);
					ToSignVector.f_Sort(fSortByLength);

					auto ToSignExecutablesVector = TCVector<CStr>::fs_FromContainer(ToSignExecutables);
					ToSignExecutablesVector.f_Sort(fSortByLength);

					CStr MainExecutable = Destination / "Contents/MacOS/Xcode";

					mint nExecutableSign = 0;

					for (auto &ToSign : ToSignExecutablesVector)
					{
						if (ToSign == MainExecutable)
							continue;

						TCActor<CProcessLaunchActor> Launch = fg_Construct();
						TCVector<CStr> Params;

						Params = {"-f", "-s", "-", ToSign};

						CProcessLaunchActor::CSimpleLaunch SimpleLaunch("codesign", Params, "", CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode);
						auto Result = co_await Launch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(SimpleLaunch));

						++nExecutableSign;
						CUStr ToOutput = CStr("  {}/{} executables signed"_f << nExecutableSign << (ToSignExecutablesVector.f_GetLen() - 1));
						*_pCommandLine %= "{}\x1B[{}D"_f << ToOutput << ToOutput.f_GetLen();
					}
					*_pCommandLine %= "\n";

					mint nSign = 0;

					for (auto &ToSign : ToSignVector)
					{
						TCActor<CProcessLaunchActor> Launch = fg_Construct();
						TCVector<CStr> Params;

						Params = {"-f", "-s", "-", ToSign};

						CProcessLaunchActor::CSimpleLaunch SimpleLaunch("codesign", Params, "", CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode);
						auto Result = co_await Launch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(SimpleLaunch));

						++nSign;
						CUStr ToOutput = CStr("  {}/{} bundles signed"_f << nSign << ToSignExecutablesVector.f_GetLen());
						*_pCommandLine %= "{}\x1B[{}D"_f << ToOutput << ToOutput.f_GetLen();
					}
					*_pCommandLine %= "\n";

					*_pCommandLine %= "{} files copied. {} files skipped. {} total files.\n"_f << nFiles << nFilesSkipped << (nFiles + nFilesSkipped);
					*_pCommandLine %= "{} executables signed. {} bundles signed. {} total sign.\n"_f << nExecutableSign << nSign << (nSign + nExecutableSign);

					co_return {};
				}
			)
		;

	}
};

DMibRuntimeClass(CTool, CTool_ResignXcode);
