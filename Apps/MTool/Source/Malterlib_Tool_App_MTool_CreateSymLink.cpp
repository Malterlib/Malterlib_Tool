// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Process/ProcessLaunch>

class CTool_CreateSymLink : public CTool2
{
public:

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{

		if (_Files.f_GetLen() != 2)
			DError("Wrong number of parameters");

		CStr Source = _Files[0].f_ReplaceChar('\\', '/');
		CStr Target = _Files[1].f_ReplaceChar('\\', '/');

		ESymbolicLinkFlag Flags;

		{
			auto DPIter = _Params.f_FindEqual("UseDevicePath");
			if (DPIter)
			{
				if ((*DPIter).f_ToInt(1))
					Flags |= ESymbolicLinkFlag_ConvertToDevicePath;
			}
		}

		{
			auto DPIter = _Params.f_FindEqual("Relative");
			if (DPIter)
			{
				if ((*DPIter).f_ToInt(1))
					Flags |= ESymbolicLinkFlag_Relative;
			}
		}
		
		bool bElevate = false;
		{
			auto DPIter = _Params.f_FindEqual("Elevate");
			if (DPIter)
			{
				if ((*DPIter).f_ToInt(1))
					bElevate = true;
			}
		}

		if (!(Flags & ESymbolicLinkFlag_Relative) && !NFile::CFile::fs_IsPathAbsolute(Source))
			Source = NFile::CFile::fs_GetExpandedPath(Source, true);

		if (!NFile::CFile::fs_IsPathAbsolute(Target))
			Target = NFile::CFile::fs_GetExpandedPath(Target, true);

		if (!NFile::CFile::fs_FileExists(Source))
			DError("Source file does not exist.");

		EFileAttrib SourceAttribs = NFile::CFile::fs_GetAttributes(Source);

		try
		{
			NFile::CFile::fs_CreateSymbolicLink(Source, Target, SourceAttribs, Flags);
		}
		catch (CExceptionFile const &)
		{
			if (bElevate && CProcessLaunch::fs_GetElevation() == EProcessElevation_IsNotElevated)
			{
				TCVector<CStr> Params;
				Params.f_Insert("CreateSymLink");
				for (auto iParam = _Params.f_GetIterator(); iParam; ++iParam)
					Params.f_Insert(fg_Format("{}={}", iParam.f_GetKey(), *iParam));
				Params.f_Insert(_Files);
				CStr StdOut;
				CStr StdErr;
				uint32 ExitCode;
				CProcessLaunchParams LaunchParams;
				LaunchParams.m_Elevation = EProcessLaunchElevation_Elevate;
				LaunchParams.m_Prompt = "Create a symbolic link";
				if (!CProcessLaunch::fs_LaunchBlock(CFile::fs_GetProgramPath(), Params, StdOut, StdErr, ExitCode, LaunchParams))
					DError(fg_Format("Error launching executable for elevation: {}", StdErr));
			}
			else
				throw;
		}

		return 0;
	}

};

DMibRuntimeClass(CTool, CTool_CreateSymLink);
