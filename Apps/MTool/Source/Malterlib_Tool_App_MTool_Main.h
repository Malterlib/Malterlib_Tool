// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/RuntimeType>

class CTool
{
public:
	virtual ~CTool() {}
	virtual aint f_Run(NRegistry::CRegistry_CStr &_Params) = 0;
	virtual void f_RequestStop() {};
};

class CTool2 : public CTool
{
public:
	virtual ~CTool2() {}
	virtual aint f_Run(NRegistry::CRegistry_CStr &_Params) override;
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) = 0;
	
	CStr f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option, CStr const &_Default) const;
	CStr f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option) const;
};
