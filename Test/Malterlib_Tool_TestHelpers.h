// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Test/Test>
#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NTool::NTestHelpers
{
	using namespace NStr;
	using namespace NFile;
	using namespace NContainer;
	using namespace NConcurrency;
	using namespace NProcess;
	using namespace NTest;
	using namespace NEncoding;

	CStr fg_Repeat(CStr const &_Text, umint _Count);

	// Each fixture outlives all its awaited operations within the owning test category.
	struct CRepositoryFixture : CAllowUnsafeThis
	{
		CRepositoryFixture();

		TCFuture<void> f_Init();
		void f_Write(CStr const &_Name, CStr const &_Contents) const;
		void f_WriteBinary(CStr const &_Name) const;
		TCFuture<void> f_Stage();
		TCFuture<void> f_Commit();

		TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_Run(CStr _Executable, TCVector<CStr> _Params, CStr _Directory, bool _bMib = false);
		TCFuture<CStr> f_Git(TCVector<CStr> _Params);
		TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_Tool(TCVector<CStr> _Params, bool _bMib = false);
		TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_ToolWithStdIn(TCVector<CStr> _Params, CStr _Input);
		TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_ToolWatchingOutput(TCVector<CStr> _Params, bool _bMib, CStr _Text, TCPromise<void> _TextSeen);

		CStr f_ShellScriptTool(CStr const &_Script) const;
		bool f_HasTemporaryAttributes() const;
		TCFuture<CStr> f_Validate(CStr _Case, uint32 _Expected = 0, bool _bStaged = true, CStr _Base = {});

		CStr m_Root;
		CStr m_Path;
		CStr m_ToolDirectory;
		CStr m_Shell; // The POSIX shell, Git for Windows' on Windows, that runs the shell scripts of the tests
		CSystemEnvironment m_Environment;
	};

	// A checkout whose root and sub-repositories, in Repos/<Name>, push to bare repositories of their own in remotes/<Name>.git.
	// The sub-repositories are pinned in Repos.MRepo.
	struct CCheckoutFixture : CAllowUnsafeThis
	{
		CCheckoutFixture(CRepositoryFixture &_Repo);

		TCFuture<void> f_Init(TCVector<CStr> _Repositories, CStr _ExtraConfiguration = {}, TCMap<CStr, CStr> _RepositoryProperties = {});
		TCFuture<CStr> f_CreateRemote(CStr _Name);
		CStr f_Remote(CStr const &_Name) const;
		CStr f_Repository(CStr const &_Name) const;
		CStr f_Configuration(TCVector<CStr> const &_Repositories, CStr const &_Extra, TCMap<CStr, CStr> const &_RepositoryProperties) const;

		TCFuture<CProcessLaunchActor::CSimpleLaunchResult> f_Mib(TCVector<CStr> _Params);
		TCFuture<CStr> f_MibSucceeds(TCVector<CStr> _Params);
		TCFuture<CStr> f_GitIn(CStr _Directory, TCVector<CStr> _Params);
		TCFuture<CStr> f_CommitIn(CStr _Directory, CStr _Name);

		CRepositoryFixture &m_Repo;
		CStr m_Remotes;
		bool m_bUseUserSettings = false; // Only with the global user settings file left out, which tests can't move from the real home
	};
}
