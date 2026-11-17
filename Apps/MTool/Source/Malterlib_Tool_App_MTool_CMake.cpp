// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Encoding/EJson>

#ifdef DMToolEmbedCMake

// NOLINTNEXTLINE(malterlib-function-naming): the entry point of the embedded CMake, which External/cmake defines
int cmake_main(int _nArgs, char const * const *_pArgs);
// NOLINTNEXTLINE(malterlib-function-naming): the entry point of the embedded CTest, which External/cmake defines
int ctest_main(int _nArgs, char const * const *_pArgs);

namespace
{
	using FEmbeddedMain = int (int _nArgs, char const * const *_pArgs);

	void fg_RegisterEmbeddedMain
		(
			CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_Name
			, FEmbeddedMain *_fMain
		)
	{
		auto Command = o_ToolsSection.f_RegisterDirectCommand
			(
				{
					"Names"_o= _o[_Name]
					, "Description"_o= "Runs {} command line.\n"_f << _Name.f_LowerCase()
					, "Parameters"_o=
					{
						"Params...?"_o=
						{
							"Type"_o= _o[""]
							, "Description"_o= "The {} params."_f << _Name.f_LowerCase()
						}
					}
					, "GreedyDefaultCommand"_o= true
					, "RawParameters"_o= true
				}
				, [_fMain](NEncoding::CEJsonSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					TCVector<CStr> Commands;
					TCVector<ch8 const *> ArgV;
					ArgV.f_Insert(Commands.f_Insert(CFile::fs_GetProgramPath()).f_GetStr());
					if (auto pParams = _Params.f_GetMember("Params"))
					{
						for (auto &Param : pParams->f_Array())
							ArgV.f_Insert(Commands.f_Insert(Param.f_String()).f_GetStr());
					}

					return _fMain(ArgV.f_GetLen(), ArgV.f_GetArray());
				}
			)
		;

		o_CommandLine.f_SetDefaultCommand(Command);
	}
}

struct CTool_CMake : public CDistributedTool
{
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		)
	;
};

void CTool_CMake::f_Register
	(
		TCActor<CDistributedToolAppActor> const &_ToolActor
		, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
		, CDistributedAppCommandLineSpecification &o_CommandLine
		, NStr::CStr const &_ClassName
	)
{
	if (fg_IsCMake())
		fg_RegisterEmbeddedMain(o_ToolsSection, o_CommandLine, "CMake", &cmake_main);
	else if (fg_IsCTest())
		fg_RegisterEmbeddedMain(o_ToolsSection, o_CommandLine, "CTest", &ctest_main);
}

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_CMake);

#endif
