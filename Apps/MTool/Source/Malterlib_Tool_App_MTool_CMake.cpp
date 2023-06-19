// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Encoding/EJSON>

#ifdef DMToolEmbedCMake

int cmake_main(int ac, char const* const* av);

class CTool_CMake : public CDistributedTool
{
public:
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		)
	{
		if (!fg_IsCMake())
			return;

		auto CmakeCommand = o_ToolsSection.f_RegisterDirectCommand
			(
				{
					"Names"_o= {"CMake"}
					, "Description"_o= "Runs cmake command line.\n"
					, "Parameters"_o=
					{
						"Params...?"_o=
						{
							"Type"_o= {""}
							, "Description"_o= "The cmake params."
						}
					}
					, "ErrorOnCommandAsParameter"_o= false
					, "ErrorOnOptionAsParameter"_o= false
					, "GreedyDefaultCommand"_o= true
				}
				, [](NEncoding::CEJSONSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					TCVector<CStr> Commands;
					TCVector<const ch8 *> ArgV;
					ArgV.f_Insert(Commands.f_Insert(CFile::fs_GetProgramPath()).f_GetStr());
					if (auto pParams = _Params.f_GetMember("Params"))
					{
						for (auto &Param : pParams->f_Array())
							ArgV.f_Insert(Commands.f_Insert(Param.f_String()).f_GetStr());
					}

					return cmake_main(ArgV.f_GetLen(), ArgV.f_GetArray());
				}
			)
		;

		o_CommandLine.f_SetDefaultCommand(CmakeCommand);
	}
};

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_CMake);

#endif
