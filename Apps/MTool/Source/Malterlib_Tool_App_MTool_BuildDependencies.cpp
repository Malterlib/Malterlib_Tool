#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/BuildSystem/BuildSystemDependency>

class CTool_BuildDependencies : public CTool2
{
public:
	
	~CTool_BuildDependencies()
	{
	}

	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		if (_Files.f_IsEmpty())
			DError("You need to specify at least ONE command");

		CStr const *pOutputFile = _Params.f_FindEqual("OutputFile");
		if (!pOutputFile)
			DError("No OutputFile specified");

		CMalterlibDependencyTracker DependencyTracker;

		for (auto CommandContents : _Files)
		{
			CStr Command = fg_GetStrSep(CommandContents, ":");

			if (Command == "Input")
				DependencyTracker.f_AddInputFile(CommandContents);
			else if (Command == "Output")
				DependencyTracker.f_AddOutputFile(CommandContents);
			else if (Command == "Find")
			{
				// Find:/Test/Test.*;RF;33;*/Test/*;*Test2.???

				CStr SearchPattern = fg_GetStrSep(CommandContents, ";");
				CStr Flags = fg_GetStrSep(CommandContents, ";");
				EFileAttrib Attribs = (EFileAttrib)fg_GetStrSep(CommandContents, ";").f_ToInt(uint32(0));

				bool bRecurse = false;
				bool bFollowLinks = false;
				bool bUseAsInput = false;
				for (ch8 const *pFlag = Flags.f_GetStr(); *pFlag; ++pFlag)
				{
					switch (*pFlag)
					{
					case 'R':
					case 'r':
						bRecurse = true;
						break;
					case 'F':
					case 'f':
						bFollowLinks = true;
						break;
					case 'I':
					case 'i':
						bUseAsInput = true;
						break;
					default:
						{
							ch8 Flag[2] = {*pFlag, 0};
							DError(fg_Format("Unknown flag: {}",  Flag));
						}
						break;
					}
				}

				TCVector<CStr> Excluded;

				while (!CommandContents.f_IsEmpty())
					Excluded.f_Insert(fg_GetStrSep(CommandContents, ";"));

				CFile::CFindFilesOptions FindOptions(SearchPattern, bRecurse);
				FindOptions.m_bFollowLinks = bFollowLinks;
				FindOptions.m_ExcludePatterns = Excluded;
				FindOptions.m_AttribMask = Attribs;

				auto FoundFiles = CFile::fs_FindFiles(FindOptions);

				TCVector<CStr> Results;

				for (auto const &File : FoundFiles)
				{
					Results.f_Insert(File.m_Path);
					if (bUseAsInput)
						DependencyTracker.f_AddInputFile(File.m_Path);
				}

				DependencyTracker.f_AddFind(SearchPattern, bRecurse, bFollowLinks, Attribs, Results, Excluded);
			}
		}

		DependencyTracker.f_WriteDependencyFile(*pOutputFile);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_BuildDependencies);
