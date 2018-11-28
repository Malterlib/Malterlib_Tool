// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Encoding/EJSON>

#ifdef DMToolEmbedCMake

int do_cmake(int ac, char const* const* av);
int do_command(int ac, char const* const* av);
int do_build(int ac, char const* const* av);
int do_open(int ac, char const* const* av);
void prepare_cmake(char const *exe_path);

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
					"Names"_= {"CMake"}
					, "Description"_= "Runs cmake command line.\n"
					, "Parameters"_=
					{
						"Params...?"_=
						{
							"Type"_= {""}
							, "Description"_= "The cmake params."
						}
					}
					, "ErrorOnCommandAsParameter"_= false
					, "ErrorOnOptionAsParameter"_= false
					, "GreedyDefaultCommand"_= true
				}
				, [](NEncoding::CEJSON const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					TCVector<CStr> Commands;
					TCVector<const ch8 *> ArgV;
					ArgV.f_Insert(Commands.f_Insert("cmake").f_GetStr());
					if (auto pParams = _Params.f_GetMember("Params"))
					{
						for (auto &Param : pParams->f_Array())
							ArgV.f_Insert(Commands.f_Insert(Param.f_String()).f_GetStr());
					}

					prepare_cmake(CFile::fs_GetProgramPath());

					if (Commands.f_GetLen() >= 2)
					{
						if (Commands[1] == "--build")
							return do_build(ArgV.f_GetLen(), ArgV.f_GetArray());
						else if (Commands[1] == "--open")
							return do_open(ArgV.f_GetLen(), ArgV.f_GetArray());
						else if (Commands[1] == "-E")
							return do_command(ArgV.f_GetLen(), ArgV.f_GetArray());
					}
					return do_cmake(ArgV.f_GetLen(), ArgV.f_GetArray());
				}
			)
		;

		o_CommandLine.f_SetDefaultCommand(CmakeCommand);
	}
};

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_CMake);

#endif
