// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include "Malterlib_Tool_App_MTool_Malterlib.h"

void CTool_Malterlib::f_Register_Core(CDistributedAppCommandLineSpecification::CSection &o_ToolsSection)
{
	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["generate"]
				, "Description"_o= "Generate build system.\n"
				, "Category"_o= "Core"
				, "Options"_o=
				{
					"Action?"_o=
					{
						"Names"_o= _o["--action"]
						, "Type"_o= COneOf{"Build", "Clean", "ReBuild"}
						, "Description"_o= "Action from build system when generating as part of build.\n"
						"One of: Build, Clean or ReBuild\n"
					}
					, fs_CachedEnvironmentOption(false)
					, "SignalChanged?"_o=
					{
						"Names"_o= _o["--signal-changed"]
						, "Default"_o= true
						, "Description"_o= "Return exit code 2 if the build system was changed."
					}
				}
				, "Parameters"_o=
				{
					"Workspace?"_o=
					{
						"Default"_o= fg_GetSys()->f_GetEnvironmentVariable("Malterlib_Workspace", "")
						, "Description"_o= "Generate only this specific workspace."
					}
				}
			}
			, [=, this](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				co_await ECoroutineFlag_CaptureExceptions;

				CGenerateOptions GenerateOptions = fs_ParseSharedOptions(_Params);

				bool bSignalChanged = _Params["SignalChanged"].f_Boolean();

				GenerateOptions.m_Settings.m_Workspace = _Params["Workspace"].f_String();
				if (auto pValue = _Params.f_GetMember("Action"))
					GenerateOptions.m_Settings.m_Action = pValue->f_String();

				#if defined(DPlatformFamily_macOS) && !defined(DMibSanitizerEnabled)
					int Signals[] =
						{
							SIGHUP,
							SIGINT,
							SIGQUIT,
							SIGILL,
							SIGABRT,
							SIGEMT,
							SIGFPE,
							SIGKILL,
							SIGSYS,
							SIGPIPE,
							SIGALRM,
							SIGTERM,
							SIGURG,
							SIGSTOP,
							SIGTSTP,
							SIGCONT
						}
					;

					auto fSignalHandler = [](int _Signal)
						{
							DConOut("Ignored signal: {}\n", _Signal);
						}
					;

					TCMap<int, void (*)(int)> OldSignals;

					bool bRunningFromXcode = fg_GetSys()->f_GetEnvironmentVariable("XCODE_APP_SUPPORT_DIR") && fg_GetSys()->f_GetEnvironmentVariable("ACTION");
					if (bRunningFromXcode)
					{
						for (auto &Signal : Signals)
							OldSignals[Signal] = signal(Signal, fSignalHandler);
					}

					auto Cleanup
						= g_OnScopeExit / [&]
						{
							for (auto iOldSignal = OldSignals.f_GetIterator(); iOldSignal; ++iOldSignal)
								signal(iOldSignal.f_GetKey(), *iOldSignal);
						}
					;
				#endif

				bool bChanged = false;

				auto AnsiEncoding = _pCommandLine->f_AnsiEncoding();

				auto ExitValue = co_await f_RunBuildSystem
					(
						[&](NBuildSystem::CBuildSystem *_pBuildSystem) -> TCUnsafeFuture<CBuildSystem::ERetry>
						{
							CBuildSystem::ERetry Retry = CBuildSystem::ERetry_None;

							if (co_await _pBuildSystem->f_Action_Generate(GenerateOptions, Retry))
								bChanged = true;

							co_return Retry;
						}
						, _pCommandLine
						, &GenerateOptions
					)
				;

				if (ExitValue)
					co_return ExitValue;

				co_return (bChanged && bSignalChanged) ? 2 : 0;
			}
		)
	;

#if 0
	o_ToolsSection.f_RegisterDirectCommand
		(
			{
				"Names"_o= _o["create"]
				, "Description"_o= "Create a new Malterlib build system.\n"
				, "Category"_o= "Core"
				, "Options"_o=
				{
					fs_CachedEnvironmentOption(true)
				}
			}
			, [=](NEncoding::CEJsonSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
			{
				return f_RunBuildSystem
					(
						[GenerateOptions = fs_ParseSharedOptions(_Params)](NBuildSystem::CBuildSystem *_pBuildSystem)
						{
							return _pBuildSystem->f_Action_Create(GenerateOptions);
						}
						, _CommandLineClient.f_AnsiEncodingFlags()
					)
				;
			}
		)
	;
#endif
	o_ToolsSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["install-binaries"]
				, "Description"_o= "Install binaries used for bootstrap and LFS storage.\n"
				, "Category"_o= "Core"
				, "Options"_o=
				{
					fs_CachedEnvironmentOption(true)
				}
			}
			, [=](NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				TCSharedPointer<TCAtomic<bool>> pCancelled = fg_Construct();
				CBuildSystem BuildSystem
					(
						_pCommandLine->f_AnsiEncoding().f_Flags()
						, _pCommandLine->m_CommandLineWidth
						, [_pCommandLine](NStr::CStr const &_Output, bool _bError)
						{
							if (_bError)
								*_pCommandLine %= _Output;
							else
								*_pCommandLine += _Output;
						}
						, pCancelled
					)
				;

				co_await BuildSystem.f_SetupGlobalMTool();
				co_await BuildSystem.f_SetupBootstrapMTool();

				co_return 0;
			}
		)
	;
}
