// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/File/Patch>

struct CTool_BinaryPatch : public CTool
{
	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Original = _Params.f_GetValue("0", "");
		CStr Patch = _Params.f_GetValue("1", "");
		CStr Output = _Params.f_GetValue("2", "");

		if (Original.f_IsEmpty())
			DMibError("You need to specify original file: MTool BinaryPatch Original Patch Output");
		if (Patch.f_IsEmpty())
			DMibError("You need to specify patch file: MTool BinaryPatch Original Patch Output");
		if (Output.f_IsEmpty())
			DMibError("You need to specify output file: MTool BinaryPatch Original Patch Output");

		CByteVector PatchData = NFile::fg_MalterlibPatchDecode(NFile::CFile::fs_ReadFile(Original), NFile::CFile::fs_ReadFile(Patch));
		if (Output == "-")
			NSys::fg_ConsoleOutputBinary(PatchData.f_ToSecure());
		else
			CFile::fs_WriteFile(PatchData, Output);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_BinaryPatch);

struct CTool_BinaryDiff : public CTool
{
	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Original = _Params.f_GetValue("0", "");
		CStr Changed = _Params.f_GetValue("1", "");
		CStr Output = _Params.f_GetValue("2", "");

		if (Original.f_IsEmpty())
			DMibError("You need to specify original file: MTool BinaryDiff Original Changed Output");
		if (Changed.f_IsEmpty())
			DMibError("You need to specify changed file: MTool BinaryDiff Original Changed Output");
		if (Output.f_IsEmpty())
			DMibError("You need to specify output file: MTool BinaryDiff Original Changed Output");

		CByteVector NewData = NFile::fg_MalterlibPatchEncode(NFile::CFile::fs_ReadFile(Original), NFile::CFile::fs_ReadFile(Changed));
		if (Output == "-")
			NSys::fg_ConsoleOutputBinary(NewData.f_ToSecure());
		else
			CFile::fs_WriteFile(NewData, Output);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_BinaryDiff);

