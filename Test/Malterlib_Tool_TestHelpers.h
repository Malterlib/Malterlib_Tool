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

		bool f_HasTemporaryAttributes() const;
		TCFuture<CStr> f_Validate(CStr _Case, uint32 _Expected = 0, bool _bStaged = true, CStr _Base = {});

		CStr m_Root;
		CStr m_Path;
		CStr m_ToolDirectory;
		CSystemEnvironment m_Environment;
	};
}
