// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

#include "Malterlib_Tool_App_MTool_WindowsSymbols.h"

#include <Mib/Process/ProcessLaunch>
#include <Mib/Cryptography/RandomID>

class CTool_Symbol : public CTool
{
public:

	void fs_CompressOrCopy(CStr const &_Source, CStr const &_Destination)
	{
		try
		{
			CProcessLaunchParams LaunchParams;
			LaunchParams.m_bShowLaunched = false;
			CStr Extension = CFile::fs_GetExtension(_Destination);
			if (Extension.f_IsEmpty())
				DError(fg_Format("Needs extension to compress: {}", _Destination));
			Extension[Extension.f_GetLen() - 1] = '_';
			CStr DestinationCompressed = CFile::fs_AppendPath(CFile::fs_GetPath(_Destination), CFile::fs_GetFileNoExt(_Destination) + "." + Extension);
#ifdef DPlatform_Windows
			auto Params = fg_CreateVector(_Source.f_ReplaceChar('/', '\\'), DestinationCompressed.f_ReplaceChar('/', '\\'));
			CProcessLaunch::fs_LaunchTool("compress.exe", Params, LaunchParams);
#else
			CStr TempDir = CFile::fs_GetTemporaryDirectory();
			CFile::fs_CreateDirectory(TempDir);
			CStr TempFile = CFile::fs_AppendPath(TempDir, fg_FastRandomID()) + "." + CFile::fs_GetExtension(_Destination);
			CStr TempFileCompressed = TempFile + "_";

			CFile::fs_CopyFile(_Source, TempFile);

			CProcessLaunch::fs_LaunchTool("mscompress", fg_CreateVector(TempFile), LaunchParams);

			CFile::fs_CopyFile(TempFileCompressed, DestinationCompressed);
			CFile::fs_DeleteFile(TempFileCompressed);
			CFile::fs_DeleteFile(TempFile);
#endif
		}
		catch (CException const &_Exception)
		{
			DConErrOut("WARNING: Failed to compress: {}\n", _Exception);
			NFile::CFile::fs_CopyFile(_Source, _Destination);
		}
	}

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr SymbolDir = _Params.f_GetValue("0", "NotExist:/");
		CStr FileName = _Params.f_GetValue("1", "NotExist.file");
		if (!NFile::CFile::fs_FileExists(SymbolDir, EFileAttrib_Directory))
		{
			DError(CStr(CStr::CFormat("Directory does not exist ({})") << SymbolDir));
		}
		if (!NFile::CFile::fs_FileExists(FileName, EFileAttrib_File))
		{
			DError(CStr(CStr::CFormat("File does not exist ({})") << FileName));
		}

		DConOut("Deploying: {}" DNewLine, FileName);

		auto ExecutableInfo = NTool::fg_GetWindowsExecutableInfo(FileName);

		{
			CStr SymServName = NFile::CFile::fs_GetFile(FileName);
			CStr Ident = CStr::CFormat("{nfh,sj8,sf0}{nfh}") << ExecutableInfo.m_TimestampRaw << ExecutableInfo.m_SizeOfImage;
			CStr OutPath = SymbolDir + "/" + SymServName + "/" + Ident;
			NFile::CFile::fs_CreateDirectory(OutPath);
			fs_CompressOrCopy(FileName, OutPath + "/" + SymServName);
		}

		{
			CStr SymServName = NFile::CFile::fs_GetFile(ExecutableInfo.m_PDBFile);
			CStr OutPath = SymbolDir + "/" + SymServName + "/" + ExecutableInfo.m_PDBGuid;
			NFile::CFile::fs_CreateDirectory(OutPath);

			if (!CFile::fs_FileExists(ExecutableInfo.m_PDBFile))
			{
				CStr AlternatePdbFile = CFile::fs_AppendPath(CFile::fs_GetPath(FileName), CFile::fs_GetFileNoExt(FileName) + ".pdb");
				if (CFile::fs_FileExists(AlternatePdbFile))
					ExecutableInfo.m_PDBFile = AlternatePdbFile;
			}

			fs_CompressOrCopy(ExecutableInfo.m_PDBFile, OutPath + "/" + SymServName);
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_Symbol);
