// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>

namespace NMib::NTool::NTestHelpers
{
	CStr fg_Repeat(CStr const &_Text, umint _Count)
	{
		CStr Result;
		for (umint i = 0; i < _Count; ++i)
			Result += _Text;

		return Result;
	}

	CRepositoryFixture::CRepositoryFixture()
		: m_Root(CFile::fs_GetProgramDirectory() / "MToolTests" / fg_TestGetCurrentPath().f_RemovePrefix("Malterlib/Tool/"))
		, m_Path(m_Root / "repo")
		, m_Environment(fg_GetSys()->f_Environment())
	{
		DMibRequire(fg_TestGetCurrentPath().f_StartsWith("Malterlib/Tool/"));
		if (CFile::fs_FileExists(m_Root))
			CFile::fs_DeleteDirectoryRecursive(m_Root);
		fg_TestAddCleanupPath(m_Root);

		CFile::fs_CreateDirectory(m_Path);
		for (auto Name : {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX", "GIT_ATTR_SOURCE"})
			m_Environment.f_Remove(Name);

		m_Environment["MToolIsMalterlib"] = "false";
		m_Environment["MToolRootDirectory"] = m_Root / "mtool";
		CFile::fs_CreateDirectory(m_Root / "tmp");
		for (auto Name : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"})
			m_Environment[Name] = m_Root / "tmp";

		m_Environment["GIT_CONFIG_GLOBAL"] = m_Root / "gitconfig";
		CFile::fs_WriteStringToFile
			(
				m_Root / "gitconfig"
				, "[user]\n\tname = MTool Test\n\temail = mtool-test@example.invalid\n"
				"[commit]\n\tgpgsign = false\n[core]\n\tautocrlf = false\n\thooksPath = no-hooks\n"
				, false
			)
		;

		m_ToolDirectory = CFile::fs_GetProgramDirectory() / "TestApps/MTool";
	}

	TCFuture<void> CRepositoryFixture::f_Init()
	{
		auto Capture = co_await (g_CaptureExceptions % "Initializing test repository");
		if (!CFile::fs_FileExists(m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension)))
			co_return DMibErrorInstance("MTool test executable is missing from '{}'; build and deploy Com_Test_Malterlib_Tool"_f << m_ToolDirectory);

		co_await f_Git({"init", "-q", "-b", "main"});
		f_Write(".editorconfig", "root = true\n\n[*]\nmax_line_length = 190\ntab_width = 4\n");

		co_return {};
	}

	void CRepositoryFixture::f_Write(CStr const &_Name, CStr const &_Contents) const
	{
		CStr Path = m_Path / _Name;
		CFile::fs_CreateDirectoryForFile(Path);
		CFile::fs_WriteStringToFile(Path, _Contents, false);
	}

	void CRepositoryFixture::f_WriteBinary(CStr const &_Name) const
	{
		CByteVector Data;
		Data.f_SetLen(501);
		for (auto &Byte : Data)
			Byte = 'X';

		Data[0] = 0;
		CFile::fs_WriteFile(m_Path / _Name, Data);
	}

	TCFuture<void> CRepositoryFixture::f_Stage()
	{
		co_await f_Git({"add", "--all"});

		co_return {};
	}

	TCFuture<void> CRepositoryFixture::f_Commit()
	{
		co_await f_Stage();
		co_await f_Git({"-c", "core.hooksPath=" + (m_Root / "no-hooks"), "commit", "-qm", "fixture"});

		co_return {};
	}

	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CRepositoryFixture::f_Run(CStr _Executable, TCVector<CStr> _Params, CStr _Directory, bool _bMib)
	{
		auto Capture = co_await (g_CaptureExceptions % ("Launching test command '{}'"_f << _Executable));
		CProcessLaunchActor::CSimpleLaunch Params{_Executable, _Params, _Directory, CProcessLaunchActor::ESimpleLaunchFlag_None};
		Params.m_Params.m_Environment = m_Environment;
		Params.m_Params.m_bMergeEnvironment = false;
		if (_bMib)
			Params.m_Params.m_Environment["MToolIsMalterlib"] = "true";

		TCActor<CProcessLaunchActor> Process = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(Process);

		co_return co_await Process(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Params));
	}

	TCFuture<CStr> CRepositoryFixture::f_Git(TCVector<CStr> _Params)
	{
		auto Capture = co_await (g_CaptureExceptions % "Running Git in test repository");
		auto Result = co_await f_Run("git", fg_Move(_Params), m_Path);
		if (Result.m_ExitCode)
			co_return DMibErrorInstance("Git failed: {}"_f << Result.f_GetCombinedOut());

		co_return Result.f_GetStdOut();
	}

	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CRepositoryFixture::f_Tool(TCVector<CStr> _Params, bool _bMib)
	{
		co_return co_await f_Run(m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension), fg_Move(_Params), m_Root, _bMib);
	}

	// The input is sent once the tool has launched, and stdin is closed behind it.
	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CRepositoryFixture::f_ToolWithStdIn(TCVector<CStr> _Params, CStr _Input)
	{
		auto Capture = co_await (g_CaptureExceptions % "Launching MTool with stdin");

		struct CState
		{
			CProcessLaunchActor::CSimpleLaunchResult m_Result;
			TCPromise<void> m_Launched;
			TCPromise<void> m_Exited;
		};

		NStorage::TCSharedPointer<CState> pState = fg_Construct();
		CProcessLaunchActor::CSimpleLaunch Launch
			{
				m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension)
				, fg_Move(_Params)
				, m_Root
				, CProcessLaunchActor::ESimpleLaunchFlag_None
			}
		;
		Launch.m_Params.m_Environment = m_Environment;
		Launch.m_Params.m_bMergeEnvironment = false;
		Launch.m_bWholeLineOutput = false;
		Launch.m_Params.m_fOnOutput = [pState](EProcessLaunchOutputType _Type, CStr const &_Output)
			{
				auto &Output = pState->m_Result.m_Output.f_Insert();
				Output.m_Type = _Type;
				Output.m_Output = _Output;
			}
		;
		Launch.m_Params.m_fOnStateChange = [pState](CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
			{
				switch (_State.f_GetTypeID())
				{
					case EProcessLaunchState_Launched:
						pState->m_Launched.f_SetResult();

						break;
					case EProcessLaunchState_LaunchFailed:
						pState->m_Launched.f_SetException(DMibErrorInstance(_State.f_Get<EProcessLaunchState_LaunchFailed>()));

						break;
					case EProcessLaunchState_Exited:
						pState->m_Result.m_ExitCode = _State.f_Get<EProcessLaunchState_Exited>();
						pState->m_Exited.f_SetResult();

						break;
				}
			}
		;

		TCActor<CProcessLaunchActor> Process = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(Process);
		auto Launched = pState->m_Launched.f_Future();
		auto Exited = pState->m_Exited.f_Future();
		auto Subscription = co_await Process(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_CurrentActor());
		co_await fg_Move(Launched);
		co_await Process(&CProcessLaunchActor::f_SendStdIn, _Input);
		co_await Process(&CProcessLaunchActor::f_CloseStdIn);
		co_await fg_Move(Exited);

		co_return pState->m_Result;
	}

	// _TextSeen is resolved when the output of the tool contains _Text, and fails when the tool exits without it.
	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CRepositoryFixture::f_ToolWatchingOutput
		(
			TCVector<CStr> _Params
			, bool _bMib
			, CStr _Text
			, TCPromise<void> _TextSeen
		)
	{
		auto Capture = co_await (g_CaptureExceptions % "Launching MTool watching its output");

		struct CState
		{
			CProcessLaunchActor::CSimpleLaunchResult m_Result;
			CStr m_Output;
			CStr m_Text;
			TCPromise<void> m_TextSeen;
			TCPromise<void> m_Exited;
			bool m_bTextSeen = false;
		};

		NStorage::TCSharedPointer<CState> pState = fg_Construct();
		pState->m_Text = _Text;
		pState->m_TextSeen = _TextSeen;

		CProcessLaunchActor::CSimpleLaunch Launch
			{
				m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension)
				, fg_Move(_Params)
				, m_Root
				, CProcessLaunchActor::ESimpleLaunchFlag_None
			}
		;
		Launch.m_Params.m_Environment = m_Environment;
		Launch.m_Params.m_bMergeEnvironment = false;
		if (_bMib)
			Launch.m_Params.m_Environment["MToolIsMalterlib"] = "true";
		Launch.m_bWholeLineOutput = false;
		Launch.m_Params.m_fOnOutput = [pState](EProcessLaunchOutputType _Type, CStr const &_Output)
			{
				auto &Output = pState->m_Result.m_Output.f_Insert();
				Output.m_Type = _Type;
				Output.m_Output = _Output;

				pState->m_Output += _Output;
				if (!pState->m_bTextSeen && pState->m_Output.f_Find(pState->m_Text) >= 0)
				{
					pState->m_bTextSeen = true;
					pState->m_TextSeen.f_SetResult();
				}
			}
		;
		Launch.m_Params.m_fOnStateChange = [pState](CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
			{
				switch (_State.f_GetTypeID())
				{
					case EProcessLaunchState_Launched:
						break;
					case EProcessLaunchState_LaunchFailed:
						pState->m_Exited.f_SetException(DMibErrorInstance(_State.f_Get<EProcessLaunchState_LaunchFailed>()));

						break;
					case EProcessLaunchState_Exited:
						pState->m_Result.m_ExitCode = _State.f_Get<EProcessLaunchState_Exited>();
						pState->m_Exited.f_SetResult();

						break;
				}
			}
		;

		TCActor<CProcessLaunchActor> Process = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(Process);
		auto Exited = pState->m_Exited.f_Future();
		auto Subscription = co_await Process(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_CurrentActor());
		auto ExitResult = co_await fg_Move(Exited).f_Wrap();

		if (!pState->m_bTextSeen)
		{
			pState->m_bTextSeen = true;
			pState->m_TextSeen.f_SetException(DMibErrorInstance("MTool exited without writing '{}': {}"_f << _Text << pState->m_Output));
		}

		if (!ExitResult)
			co_return ExitResult.f_GetException();

		co_return pState->m_Result;
	}

	CCheckoutFixture::CCheckoutFixture(CRepositoryFixture &_Repo)
		: m_Repo(_Repo)
		, m_Remotes(_Repo.m_Root / "remotes")
	{
		// CI sets it, and it resets repositories instead of keeping their branches and changes
		m_Repo.m_Environment.f_Remove("MalterlibRepositoryHardReset");
	}

	CStr CCheckoutFixture::f_Remote(CStr const &_Name) const
	{
		return m_Remotes / (_Name + ".git");
	}

	CStr CCheckoutFixture::f_Repository(CStr const &_Name) const
	{
		return m_Repo.m_Path / "Repos" / _Name;
	}

	CStr CCheckoutFixture::f_Configuration(TCVector<CStr> const &_Repositories, CStr const &_Extra, TCMap<CStr, CStr> const &_RepositoryProperties) const
	{
		CStr Configuration = "%Repository \"Root\"\n{\n\tRepository\n\t{\n\t\tType \"Root\"\n\t\tDefaultBranch \"main\"\n";
		Configuration += "\t\tURL " + CEJsonSorted(f_Remote("Root")).f_ToString() + "\n";
		Configuration += "\t\tLocation \".\"->MakeAbsolute()\n\t}\n}\n";
		for (auto &Name : _Repositories)
		{
			Configuration += "%Repository \"" + Name + "\"\n{\n\tRepository\n\t{\n\t\tDefaultBranch \"main\"\n";
			Configuration += "\t\tURL " + CEJsonSorted(f_Remote(Name)).f_ToString() + "\n";
			Configuration += "\t\tLocation \"Repos/" + Name + "\"->MakeAbsolute()\n";
			Configuration += "\t\tConfigFile \"Repos.MRepo\"->MakeAbsolute()\n";
			Configuration += "\t\tStateFile \"Repos.MRepoState\"->MakeAbsolute()\n";
			if (auto *pProperties = _RepositoryProperties.f_FindEqual(Name))
				Configuration += *pProperties;
			Configuration += "\t}\n}\n";
		}

		return Configuration + _Extra;
	}

	// Each sub-repository starts with one commit on main, and the root with the build system, both pushed. _RepositoryProperties
	// adds lines to the Repository block of each sub-repository named, and _ExtraConfiguration to the build system file.
	TCFuture<void> CCheckoutFixture::f_Init(TCVector<CStr> _Repositories, CStr _ExtraConfiguration, TCMap<CStr, CStr> _RepositoryProperties)
	{
		auto Capture = co_await (g_CaptureExceptions % "Initializing test checkout");

		CFile::fs_CreateDirectory(f_Remote("Root"));
		co_await m_Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, f_Remote("Root"));

		for (auto &Name : _Repositories)
		{
			CStr Remote = f_Remote(Name);
			CStr Work = m_Repo.m_Root / "work" / Name;
			CFile::fs_CreateDirectory(Remote);
			CFile::fs_CreateDirectory(Work);
			co_await m_Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, Remote);
			co_await m_Repo.f_Run("git", {"init", "-q", "-b", "main"}, Work);
			co_await f_CommitIn(Work, "Initial");
			co_await f_GitIn(Work, {"push", "-q", Remote, "main"});
		}

		m_Repo.f_Write("Test.MBuildSystem", f_Configuration(_Repositories, _ExtraConfiguration, _RepositoryProperties));
		m_Repo.f_Write(".gitignore", "/BuildSystem/\n/Repos/\n/Repos.MRepoState\n");
		co_await m_Repo.f_Commit();
		co_await m_Repo.f_Git({"remote", "add", "origin", f_Remote("Root")});
		co_await m_Repo.f_Git({"push", "-q", "-u", "origin", "main"});

		co_return {};
	}

	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CCheckoutFixture::f_Mib(TCVector<CStr> _Params)
	{
		CStr Command = _Params[0];
		_Params.f_Remove(0);

		TCVector<CStr> Params =
			{
				Command
				, "--no-color"
				, "--build-system"
				, m_Repo.m_Path / "Test.MBuildSystem"
				, "--output-directory"
				, m_Repo.m_Path / "BuildSystem/Default"
				, "--no-use-user-settings"
				, "--no-use-cached-environment"
			}
		;
		Params.f_Insert(fg_Move(_Params));

		return m_Repo.f_Run(m_Repo.m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension), fg_Move(Params), m_Repo.m_Path, true);
	}

	// The combined output of an MTool command that has to succeed
	TCFuture<CStr> CCheckoutFixture::f_MibSucceeds(TCVector<CStr> _Params)
	{
		CStr Command = CStr::fs_Join(_Params, " ");
		auto Result = co_await f_Mib(fg_Move(_Params));
		if (Result.m_ExitCode)
			co_return DMibErrorInstance("'{}' failed: {}"_f << Command << Result.f_GetCombinedOut());

		co_return Result.f_GetCombinedOut();
	}

	TCFuture<CStr> CCheckoutFixture::f_GitIn(CStr _Directory, TCVector<CStr> _Params)
	{
		auto Result = co_await m_Repo.f_Run("git", fg_Move(_Params), _Directory);
		if (Result.m_ExitCode)
			co_return DMibErrorInstance("Git failed in '{}': {}"_f << _Directory << Result.f_GetCombinedOut());

		co_return Result.f_GetStdOut().f_Trim();
	}

	// Commits a new file named after the commit, and returns the commit
	TCFuture<CStr> CCheckoutFixture::f_CommitIn(CStr _Directory, CStr _Name)
	{
		CFile::fs_WriteStringToFile(_Directory / (_Name + ".txt"), _Name + "\n", false);
		co_await f_GitIn(_Directory, {"add", "-A"});
		co_await f_GitIn(_Directory, {"-c", "core.hooksPath=" + (m_Repo.m_Root / "no-hooks"), "commit", "-qm", _Name});

		co_return co_await f_GitIn(_Directory, {"rev-parse", "HEAD"});
	}

	bool CRepositoryFixture::f_HasTemporaryAttributes() const
	{
		// The fixture points MToolRootDirectory at m_Root / "mtool".
		return !CFile::fs_FindFiles(m_Root / "mtool" / "Validate" / "*", EFileAttrib_Directory).f_IsEmpty();
	}

	TCFuture<CStr> CRepositoryFixture::f_Validate(CStr _Case, uint32 _Expected, bool _bStaged, CStr _Base)
	{
		auto Capture = co_await (g_CaptureExceptions % "Validating test repository");
		DMibTestPath(_Case);

		TCVector<CStr> Params = {"Validate", "--no-color", "-C", m_Path};
		if (_Base)
			Params.f_Insert({"--base", _Base});
		else if (_bStaged)
			Params.f_Insert("--staged");

		auto Result = co_await f_Tool(fg_Move(Params));
		auto Output = Result.f_GetCombinedOut();
		DMibExpect(Result.m_ExitCode, ==, _Expected);
		if (Result.m_ExitCode != _Expected)
			co_return DMibErrorInstance(Output);

		DMibExpect(Output.f_Find("Time: "), >=, 0);
		if (Output.f_Find("Time: ") < 0)
			co_return DMibErrorInstance("Validation exited without a summary: {}"_f << Output);

		DMibExpect(Output.f_Find(" s."), >=, 0);

		bool bAbsolutePaths = true;
		for (auto const &Line : Output.f_SplitLine())
		{
			auto iMessage = Line.f_Find(": line length ");
			if (iMessage < 0)
				continue;

			auto pStart = Line.f_GetStr();
			auto pParse = pStart + iMessage;
			while (pParse != pStart && *(pParse - 1) != ':')
				--pParse;

			if (pParse == pStart)
			{
				bAbsolutePaths = false;
				continue;
			}

			CStr File(pStart, pParse - pStart - 1);
			auto Relative = CFile::fs_MakePathRelative(File, m_Path);
			bAbsolutePaths &= CFile::fs_IsPathAbsolute(File) && !CFile::fs_IsPathAbsolute(Relative) && !Relative.f_StartsWith("..");
		}

		DMibExpect(bAbsolutePaths, ==, true);
		DMibExpect(f_HasTemporaryAttributes(), ==, false);

		co_return Output;
	}
}
