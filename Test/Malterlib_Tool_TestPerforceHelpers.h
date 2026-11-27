// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Tool_TestHelpers.h"

// The Perforce headers use Malterlib's types without their namespaces.
namespace NMib::NPerforce
{
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NFunction;
	using namespace NMib::NConcurrency;
	using namespace NMib::NEncoding;
}

#include <Mib/Perforce/Wrapper>

namespace NMib::NTool::NTestHelpers
{
	TCFuture<void> fg_SetPerforceTriggers(NPerforce::CPerforceClient::CConnectionInfo _Connection, CStr _Triggers);
	CStr fg_RefusingTrigger(CRepositoryFixture const &_Repo, CStr const &_Command, CStr const &_Text);
}
