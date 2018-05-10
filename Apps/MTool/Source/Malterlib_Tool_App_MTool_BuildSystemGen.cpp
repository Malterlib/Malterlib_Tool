// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystem>
#include <Mib/Encoding/EJSON>

class CTool_BuildSystemGen : public CTool
{
public:
	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		using namespace NBuildSystem;
		DMibScopeTraceTimer("CTool_BuildSystemGen");
		// The first argument is the path to the .gen file
		CGenerateSettings GeneratorSettings;
		GeneratorSettings.m_SourceFile = _Params.f_GetValue("0", "");

		for (mint i = 1; true; ++i)
		{
			CStr Value = _Params.f_GetValue(CStr::fs_ToStr(i), "");
			if (Value.f_IsEmpty())
				break;
			CStr Key = fg_GetStrSep(Value, "=");

			if (Key == "Workspace")
				GeneratorSettings.m_Workspace = Value;
			else if (Key == "OutDir")
				GeneratorSettings.m_OutputDir = Value;
			else if (Key == "Generator")
				GeneratorSettings.m_Generator = Value;
			else if (Key == "Action")
				GeneratorSettings.m_Action = Value;
			else if (Key == "ActionParams")
			{
				auto const Json = CEJSON::fs_FromString(Value);
				for (auto &Param : Json.f_Array())
					GeneratorSettings.m_ActionParams.f_Insert(Param.f_String());
			}
			else if (Key == "AbsFilePaths")
				GeneratorSettings.m_GenerationFlags |= Value.f_ToInt(1) ? NBuildSystem::EGenerationFlag_AbsoluteFilePaths : NBuildSystem::EGenerationFlag_None;
			else if (Key == "SingleThreaded")
				GeneratorSettings.m_GenerationFlags |= Value.f_ToInt(1) ? NBuildSystem::EGenerationFlag_SingleThreaded : NBuildSystem::EGenerationFlag_None;
			else if (Key == "UseCachedEnvironment")
				GeneratorSettings.m_GenerationFlags |= (Value == "true") ? NBuildSystem::EGenerationFlag_UseCachedEnvironment : NBuildSystem::EGenerationFlag_None;
			else if (Key == "DisableUserSettings")
				GeneratorSettings.m_GenerationFlags |= (Value == "true") ? NBuildSystem::EGenerationFlag_DisableUserSettings : NBuildSystem::EGenerationFlag_None;
			else
				DError(CStr(CStr::CFormat("Unknown generate setting: {}") << Key));
		}

		CBuildSystem::ERetry Retry = CBuildSystem::ERetry_Again;
		bool bChanged = false;
		while (Retry != CBuildSystem::ERetry_None)
		{
			NBuildSystem::CBuildSystem BuildSystem;
			if (BuildSystem.f_Generate(GeneratorSettings, Retry))
				bChanged = true;
			if (Retry == CBuildSystem::ERetry_Relaunch)
				return 3;
		}
		return bChanged ? 2 : 0;
	}
};

DMibRuntimeClass(CTool, CTool_BuildSystemGen);

class CTool_Malterlib : public CTool_BuildSystemGen
{
public:
	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		CStr BuildSystem = _Params.f_GetValue("0", "");

		if (BuildSystem.f_IsEmpty())
			DMibError("You need to specify the build system");

		mint iParam = 1;

		TCVector<CStr> BuildSystemParams;

		for (; true; ++iParam)
		{
			auto pChild = _Params.f_GetChild(CStr::fs_ToStr(iParam));
			if (!pChild)
				break;
			auto &Value = pChild->f_GetThisValue();
			if (Value.f_Find("=") < 0)
				break;

			BuildSystemParams.f_Insert(Value);
		}

		CStr Action = _Params.f_GetValue(CStr::fs_ToStr(iParam++), "");
		if (Action.f_IsEmpty())
			DMibError("You need to specify an action");

		NRegistry::CRegistry_CStr Params;

		NEncoding::CEJSON ActionParams;
		auto &ActionParamsArray = ActionParams.f_Array();

		for (; true; ++iParam)
		{
			auto pChild = _Params.f_GetChild(CStr::fs_ToStr(iParam));
			if (!pChild)
				break;
			ActionParamsArray.f_Insert(pChild->f_GetThisValue());
		}

		mint iOutParam = 0;
		Params.f_SetValue(CStr::fs_ToStr(iOutParam++), BuildSystem);
		for (auto &Param : BuildSystemParams)
			Params.f_SetValue(CStr::fs_ToStr(iOutParam++), Param);

		Params.f_SetValue(CStr::fs_ToStr(iOutParam++), "Action={}"_f << Action);
		Params.f_SetValue(CStr::fs_ToStr(iOutParam++), "UseCachedEnvironment=true");
		Params.f_SetValue(CStr::fs_ToStr(iOutParam++), "ActionParams={}"_f << ActionParams.f_ToString());

		return CTool_BuildSystemGen::f_Run(Params);
	}

};

DMibRuntimeClass(CTool, CTool_Malterlib);

#ifdef DMToolEmbedCMake

int do_cmake(int ac, char const* const* av);
int do_command(int ac, char const* const* av);
int do_build(int ac, char const* const* av);
int do_open(int ac, char const* const* av);
void prepare_cmake(char const *exe_path);

class CTool_CMake : public CTool
{
public:
	aint f_Run(NRegistry::CRegistry_CStr &_Params)
	{
		TCVector<CStr> Commands;
		TCVector<const ch8 *> ArgV;
		ArgV.f_Insert(Commands.f_Insert("cmake").f_GetStr());
		for (mint i = 0; true; ++i)
		{
			auto *pValue = _Params.f_GetChild(CStr::fs_ToStr(i));
			if (!pValue)
				break;
			ArgV.f_Insert(Commands.f_Insert(pValue->f_GetThisValue()).f_GetStr());
		}
		prepare_cmake(CFile::fs_GetProgramPath());

		if (Commands[1] == "--build")
			return do_build(ArgV.f_GetLen(), ArgV.f_GetArray());
		else if (Commands[1] == "--open")
			return do_open(ArgV.f_GetLen(), ArgV.f_GetArray());
		else if (Commands[1] == "-E")
			return do_command(ArgV.f_GetLen(), ArgV.f_GetArray());
		else
			return do_cmake(ArgV.f_GetLen(), ArgV.f_GetArray());
	}
};

DMibRuntimeClass(CTool, CTool_CMake);

#endif
