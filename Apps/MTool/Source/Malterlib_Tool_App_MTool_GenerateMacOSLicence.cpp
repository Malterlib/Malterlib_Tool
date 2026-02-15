// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"


class CTool_GenerateMacOSLicence : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr SourcePath = _Params.f_GetValue("0", "");
		CStr ReplacementTextPath = _Params.f_GetValue("1", "");
		CAnsiStr SearchString = _Params.f_GetValue("2", "").f_GetStr();
		CStr DestinationPath = _Params.f_GetValue("3", "");

		if (!NFile::CFile::fs_FileExists(SourcePath, EFileAttrib_File))
			DError("Source file does not exist");

		if (!NFile::CFile::fs_FileExists(ReplacementTextPath, EFileAttrib_File))
			DError("Replacement text file does not exist");

		if (SearchString.f_IsEmpty())
			DError("Search string is empty");

		if (DestinationPath.f_IsEmpty())
			DError("Destination path is empty");

		try
		{
			CAnsiStr SourceFileContents;
			{
				NContainer::CByteVector FileContents = NFile::CFile::fs_ReadFile(SourcePath);
				SourceFileContents.f_AddStr((ch8 const *)FileContents.f_GetArray(), FileContents.f_GetLen());
			}
			CAnsiStr ReplacementTextAnsi;
			{
				CStr ReplacementText = NFile::CFile::fs_ReadStringFromFile((CStr)ReplacementTextPath);
				NStr::NPlatform::fg_SystemEncodeCodePageStr(ReplacementText, ReplacementTextAnsi, 10000, '?'); // Code page 10000 = Mac Roman
			}

			aint iSearchPos = SourceFileContents.f_Find(SearchString);
			if (iSearchPos == -1)
				DError("Failed to find search string in source file");

			CAnsiStr FinalText = SourceFileContents.f_Extract(0, iSearchPos) + SourceFileContents.f_Extract(iSearchPos + SearchString.f_GetLen(), SourceFileContents.f_GetLen() - iSearchPos - SearchString.f_GetLen());

			CAnsiStr FormattedReplacementText;
			{

				mint nLength = ReplacementTextAnsi.f_GetLen();

				for (mint iChar = 0; iChar < nLength; iChar += 16)
				{
					mint nChars = (iChar + 16 <= nLength) ? 16 : nLength - iChar;

					if (FormattedReplacementText.f_IsEmpty())
						FormattedReplacementText += "$\"";
					else
						FormattedReplacementText += "\t$\"";

					for (mint j = 0; j < nChars; ++j)
					{
						if (j > 0 && (j & 1) == 0)
							FormattedReplacementText += " ";

						FormattedReplacementText += CAnsiStr::CFormat("{nfh,sj2,sf0,nc}") << (uint8)ReplacementTextAnsi.f_GetAt(iChar + j);
					}

					FormattedReplacementText += "\"\n";
				}
			}

			FinalText = FinalText.f_Insert(iSearchPos, FormattedReplacementText);

			NFile::CFile::fs_CreateDirectory(NFile::CFile::fs_GetPath(DestinationPath));

			CBinaryStreamMemory<> Stream;
			Stream.f_FeedBytes(FinalText.f_GetStr(), FinalText.f_GetLen());

			NFile::CFile::fs_CopyFileDiff(Stream.f_GetVector(), (CStr)DestinationPath, NTime::CTime::fs_NowUTC());
		}
		catch (NFile::CExceptionFile const& _Exception)
		{
			DError(_Exception.f_GetErrorStr());
		}

		return 0;
	}

};

DMibRuntimeClass(CTool, CTool_GenerateMacOSLicence);

