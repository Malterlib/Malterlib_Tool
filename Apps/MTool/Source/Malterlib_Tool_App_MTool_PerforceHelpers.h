// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Process/StdIn>

bool fg_Confirm(CStr const &_Text);
CStr fg_AskUser(CBlockingStdInReader &_StdIn, CStr const &_Text);
