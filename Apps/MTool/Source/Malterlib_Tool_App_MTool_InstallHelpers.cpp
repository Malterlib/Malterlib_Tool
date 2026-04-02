// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

class CTool_AddPortSource : public CTool2
{
public:
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr Source = f_GetOption(_Params, "Source").f_Trim();

		CStr ConfigFile("/opt/local/etc/macports/sources.conf");

		CStr FileContents = CFile::fs_ReadStringFromFile(ConfigFile);

		auto pParse = FileContents.f_GetStr();

		TCVector<CStr> Sources;

		CStr NewContents;

		while (*pParse)
		{
			auto pStart = pParse;
			fg_ParseToEndOfLine(pParse);
			CStr Line(pStart, pParse - pStart);
			fg_ParseEndOfLine(pParse);

			if (Line.f_StartsWith("#") || Line.f_Trim().f_IsEmpty())
			{
				// Comment
				NewContents += Line;
				NewContents += DMibNewLine;
			}
			else
				Sources.f_Insert(Line.f_Trim());
		}

		if (Sources.f_Contains(Source) < 0)
			Sources.f_InsertFirst(Source);

		for (auto iSource = Sources.f_GetIterator(); iSource; ++iSource)
		{
			NewContents += *iSource;
			NewContents += DMibNewLine;
		}

		CFile::fs_WriteStringToFile(ConfigFile, NewContents, false);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_AddPortSource);

