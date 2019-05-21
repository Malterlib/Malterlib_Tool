// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/RuntimeType>
#include <Mib/Concurrency/DistributedTool>
#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Container/Registry>

bool fg_IsCMake();
bool fg_IsMalterlib();

class CTool : public CDistributedTool
{
public:
	virtual ~CTool() {}

	virtual void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
		 	, NStr::CStr const &_ClassName
		) override
	;

	virtual aint f_Run(NContainer::CRegistry &_Params) = 0;
	virtual void f_RequestStop() {};
};

class CTool2 : public CTool
{
public:
	virtual ~CTool2() {}
	virtual aint f_Run(NContainer::CRegistry &_Params) override;
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) = 0;

	CStr f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option, CStr const &_Default) const;
	CStr f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option) const;
};
