// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Process/ProcessLaunch>

class CTool_MakeFat : public CTool2
{
public:

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{
		if (!_Params.f_FindEqual("DestinationDir"))
		{
			DError("DestinationDir not specified");
		}
		
		CStr DestinationDir = _Params["DestinationDir"];
		CStr Exclude = _Params["Exclude"];
		
		TCVector<CStr> Excluded;
		while (!Exclude.f_IsEmpty())
		{
			Excluded.f_Insert(fg_GetStrSep(Exclude, ";"));
		}
		
		if (CFile::fs_FileExists(DestinationDir))
			CFile::fs_DeleteDirectoryRecursive(DestinationDir);

		TCMap<CStr, TCVector<CStr>> FilesToLipo;
		
		for (auto iFile = _Files.f_GetIterator(); iFile; ++iFile)
		{
			CStr Source = CFile::fs_GetExpandedPath(*iFile, true);
			
			TCVector<CStr> Files = NFile::CFile::fs_FindFiles(Source + "/*", EFileAttrib_File | EFileAttrib_Directory, true, false);
			
			if (Files.f_IsEmpty())
				DError(CStr::CFormat("No files found for source: {}") << Source);
			
			for (auto iFoundFile = Files.f_GetIterator(); iFoundFile; ++iFoundFile)
			{
				CStr RelativePath = CFile::fs_MakePathRelative(*iFoundFile, Source);

				bool bLipoFile = false;
				EFileAttrib Attributes = CFile::fs_GetAttributes(*iFoundFile);
				if ((Attributes & EFileAttrib_Executable) && !(Attributes & (EFileAttrib_Directory | EFileAttrib_Link)))
				{
					CFile File;
					File.f_Open(*iFoundFile, EFileOpen_Read | EFileOpen_ShareAll);
					ch8 Data[2];
					File.f_Read(Data, 2);
					if (Data[0] == '#' && Data[1] == '!')
					{
						// Shell script
					}
					else
					{
						bLipoFile = true;
					}
				}
				
				if (bLipoFile)
				{
					for (auto iExclude = Excluded.f_GetIterator(); iExclude; ++iExclude)
					{
						if (RelativePath.f_Find(*iExclude) >= 0)
						{
							bLipoFile = false;
							break;
						}
					}
				}
				
				if (bLipoFile)
					FilesToLipo[RelativePath].f_Insert(*iFoundFile);
				else
				{
					if (Attributes & EFileAttrib_Directory && !(Attributes & EFileAttrib_Link))
						CFile::fs_CreateDirectory(CFile::fs_AppendPath(DestinationDir, RelativePath));
					else
					{
						CStr OutFile = CFile::fs_AppendPath(DestinationDir, RelativePath);
						CFile::fs_CreateDirectory(CFile::fs_GetPath(OutFile));
						CFile::fs_DiffCopyFileOrDirectory(*iFoundFile, OutFile, fg_Default());
					}
				}
			}
		}
		
		for (auto iLipo = FilesToLipo.f_GetIterator(); iLipo; ++iLipo)
		{
			CStr RelativePath = iLipo.f_GetKey();
			TCVector<CStr> Params;
			for (auto iFile = iLipo->f_GetIterator(); iFile; ++iFile)
			{
				Params.f_Insert(*iFile);
			}
			
			CStr Output = CFile::fs_AppendPath(DestinationDir, RelativePath);
			CFile::fs_CreateDirectory(CFile::fs_GetPath(Output));
			Params.f_Insert("-create");
			Params.f_Insert("-output");
			Params.f_Insert(Output);
			
			int Ret = 0;
			
			CProcessLaunchParams LaunchParams = CProcessLaunchParams::fs_LaunchExecutable
				(
					"lipo"
					, Params
					, CFile::fs_GetCurrentDirectory()
					, [&](CProcessLaunchStateChangeVariant const &_StateChange, fp64 _TimeSinceLaunch)
					{
						if (_StateChange.f_GetTypeID() == EProcessLaunchState_LaunchFailed)
						{
							DConErrOut("{}", _StateChange.f_Get<EProcessLaunchState_LaunchFailed>());
							Ret = 255;
						}
						else if (_StateChange.f_GetTypeID() == EProcessLaunchState_Exited)
						{
							Ret = _StateChange.f_Get<EProcessLaunchState_Exited>();
						}
					}
				)
			;
			
			LaunchParams.m_bAllowExecutableLocate = true;
			
			LaunchParams.m_fOnOutput 
				= [&](EProcessLaunchOutputType _OutputType, CStr const &_Output)
				{
					switch (_OutputType)
					{
					case EProcessLaunchOutputType_GeneralError:
					case EProcessLaunchOutputType_StdErr:
					case EProcessLaunchOutputType_TerminateMessage:
						DConErrOutRaw(_Output);
						break;
					case EProcessLaunchOutputType_StdOut:
						DConOutRaw(_Output);
						break;
					}
				}
			;
			{
				CProcessLaunch Launch(LaunchParams, EProcessLaunchCloseFlag_BlockOnExit);
			}

			if (Ret)
				DError("lipo launch failed");
				
			
		}
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_MakeFat);

