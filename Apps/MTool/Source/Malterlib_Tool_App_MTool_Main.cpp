// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Core/Application>

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool);

bool fg_IsCMake()
{
	static bool bIsCmake = CFile::fs_GetFileNoExt(CFile::fs_GetProgramPath()) == "MToolCMake";
	return bIsCmake;
}

bool fg_IsMalterlib()
{
	static bool bIsMalterlb = fg_GetSys()->f_GetProtectedEnvironmentVariable("MToolIsMalterlib", "false") == "true" || CFile::fs_GetFileNoExt(CFile::fs_GetProgramPath()) == "mib";
	return bIsMalterlb;
}

void CTool::f_Register
	(
		TCActor<CDistributedToolAppActor> const &_ToolActor
		, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
		, CDistributedAppCommandLineSpecification &o_CommandLine
		, NStr::CStr const &_ClassName
	)
{
	if (fg_IsCMake() || fg_IsMalterlib())
		return;

	CStr ClassName = _ClassName;
	if (ClassName.f_StartsWith("CTool_"))
		ClassName = ClassName.f_Extract(6);

	o_ToolsSection.f_RegisterDirectCommand
		(
			{
				"Names"_= {ClassName}
				, "Description"_= "Runs {} command.\n"_f << ClassName
				, "Parameters"_=
				{
					"Params...?"_=
					{
						"Type"_= {""}
						, "Description"_= "The params the command take."
					}
				}
				, "ErrorOnCommandAsParameter"_= false
				, "ErrorOnOptionAsParameter"_= false
			}
			, [this](NEncoding::CEJSON const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
			{
				NContainer::CRegistry Params;
				mint iOut = 0;

				if (auto pParams = _Params.f_GetMember("Params"))
				{
					for (auto &Param : pParams->f_Array())
					{
						Params.f_SetValue(CStr::fs_ToStr(iOut), Param.f_String());
						++iOut;
					}
				}

				return f_Run(Params);
			}
		)
	;
}

CStr CTool2::f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option, CStr const &_Default) const
{
	auto pOption = _Params.f_FindEqual(_Option);
	if (pOption)
		return *pOption;
	return _Default;
}

CStr CTool2::f_GetOption(TCMap<CStr, CStr> const &_Params, CStr const &_Option) const
{
	auto pOption = _Params.f_FindEqual(_Option);
	if (pOption)
		return *pOption;

	DError(CStr::CFormat("You have to specify '{}'") << _Option);
}

aint CTool2::f_Run(NContainer::CRegistry &_Params)
{
	TCMap<CStr, CStr> Params;

	TCVector<CStr> Files;

	for (mint i = 0; true; ++i)
	{
		CStr Value = _Params.f_GetValue(CStr::fs_ToStr(i), "");
		if (Value.f_IsEmpty())
			break;
		if (Value.f_FindChar('=') >= 0)
		{
			CStr Key = fg_GetStrSep(Value, "=");
			Params[Key] = Value;
		}
		else
			Files.f_Insert(Value);
	}

	return f_Run(Files, Params);
}

class CToolApp : public NMib::CApplication
{
public:
	aint f_Main()
	{
		fg_SetConcurrencyManagerDefaultExecutionPriority(EPriority_Low, EExecutionPriority_Normal);
		
		return fg_RunApp
			(
				[]
				{
					auto Settings = CDistributedToolSettings("MTool")
						.f_RootDirectory(NFile::CFile::fs_GetUserHomeDirectory() / ".Malterlib/MTool")
					;

					if (fg_IsCMake())
						fg_Move(Settings).f_DefaultCommandLineFunctionalies(EDefaultCommandLineFunctionality_None);
					else
						fg_Move(Settings).f_DefaultCommandLineFunctionalies(EDefaultCommandLineFunctionality_AllNoDistributedComputing);

					return fg_ConstructActor<CDistributedToolAppActor>(fg_Move(Settings));
				}
			)
		;
	}
};

DMibAppImplement(CToolApp);
