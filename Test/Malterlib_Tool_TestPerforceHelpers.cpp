// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestPerforceHelpers.h"

namespace NMib::NTool::NTestHelpers
{
	// Sets the server's trigger table, "Triggers:\n" and its lines, which "Triggers:\n" alone empties.
	TCFuture<void> fg_SetPerforceTriggers(NPerforce::CPerforceClient::CConnectionInfo _Connection, CStr _Triggers)
	{
		co_await
			(
				g_Dispatch(fg_BlockingActor())
				/ [_Connection, _Triggers]()
				{
					NPerforce::CPerforceClientThrow Client(_Connection);
					Client.f_Login(CStr());
					Client.f_SetTriggers(_Triggers);
				}
				% "Failed to set the triggers"
			)
		;

		co_return {};
	}

	// The trigger line of a command trigger that refuses the p4 command where its arguments hold _Text, or always for an empty
	// _Text, saying "Refused <Command>". Its script is written in the fixture's root, and the fixture's shell runs it, which Windows
	// can't launch by itself.
	CStr fg_RefusingTrigger(CRepositoryFixture const &_Repo, CStr const &_Command, CStr const &_Text)
	{
		CStr Script = _Repo.m_Root / ("Refuse-{}.sh"_f << _Command);
		CStr Refuse = "echo \"Refused {}\"; exit 1"_f << _Command;
		CStr Contents = _Text ? CStr("#!/bin/sh\ncase \"$*\" in\n*{}*) {};;\nesac\nexit 0\n"_f << _Text << Refuse) : CStr("#!/bin/sh\n{}\n"_f << Refuse);
		CFile::fs_WriteStringToFile(Script, Contents, false);
		CFile::fs_SetAttributes(Script, EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute);

		return "\tRefuse{} command pre-user-{} \"%quote%{}%quote% %quote%{}%quote% %args%\"\n"_f << _Command << _Command << _Repo.m_Shell << Script;
	}
}
