// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystem>
#include <Mib/Encoding/EJson>
#include <Mib/CommandLine/AnsiEncoding>

class CTool_AnalyzeXCBuildLog : public CDistributedTool, public CAllowUnsafeThis
{
public:
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		) override
	;
};
