// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

/*

Usage:
	MTool GenerateTweaks [-o OutputCPPFile.cpp] InputTwkFile
		Generates a OutputCPPFile.cpp and OutputCPPFile.h.
		OutputCPPFile, if not specified is set to InputTwkFile.cpp

	Or

	MTool GenerateTweaks -m [-o OutputTwkFile] InputTwkFile
		Merges InputTwkFile into OutputTwkFile.
		Creates OutputTwkFile if it does not exist.

*/

class CTool_GenerateTweaks : public CTool
{
	enum EType
	{
		EType_Unknown	= 0,
		EType_Int		= DBit(0),
		EType_Float		= DBit(1),
		EType_Color		= DBit(2),
		EType_Font		= DBit(3),
		EType_PMetricInt = DBit(4),
		EType_PMetricFloat = DBit(5),
	};

public:
	aint f_Run(NContainer::CRegistry &_Params)
	{

		bint bRet = true;

		CStr DestFilePath;

		int iArg = 0;
		CStr ArgStr = CStr(CStr(CStr::CFormat("{}") << iArg));
		CStr ArgValue;
		bint bMergeMode = false;
		bool bOnlyWriteIfChanged = true;

		while (_Params.f_GetValueIfExists(ArgStr, ArgValue))
		{
			if (ArgValue.f_GetAt(0) != '-')
				break;

			if (ArgValue.f_CmpNoCase("-o") == 0)
			{ // Output cpp file specified.
				++iArg;
				ArgStr = CStr(CStr(CStr::CFormat("{}") << iArg));

				if (!_Params.f_GetValueIfExists(ArgStr, DestFilePath))
				{
					DConOut("Invalid command line args" DNewLine, 0);
					return false;
				}
			}
			else if (ArgValue.f_CmpNoCase("-m") == 0)
			{ // Merge mode
				// Merge source options into output file.
				bMergeMode = true;
			}
			else if (ArgValue.f_CmpNoCase("-c") == 0)
			{
				bOnlyWriteIfChanged = false;
			}

			++iArg;
			ArgStr = CStr(CStr(CStr::CFormat("{}") << iArg));
		}

		CStr SourceFilePath;

		if (!_Params.f_GetValueIfExists(ArgStr, SourceFilePath))
		{
			DConOut("Invalid command line args" DNewLine, 0);
			return false;
		}

		if (DestFilePath.f_IsEmpty())
		{
			DestFilePath = NFile::CFile::fs_GetPath(SourceFilePath);
			if (!DestFilePath.f_IsEmpty())
				DestFilePath += "/";
			DestFilePath += NFile::CFile::fs_GetFileNoExt(SourceFilePath);
			DestFilePath += ".cpp";
		}

		CRegistry Source;
		{
			// Read source file.
			CStr Contents;
			Contents = NFile::CFile::fs_ReadStringFromFile(SourceFilePath);

			try
			{
				Source.f_ParseStr(Contents, SourceFilePath);
			}
			catch(NMib::NException::CException const &_Exception)
			{
				DConErrOut("Error: Failed to parse source file: {}:" DNewLine, SourceFilePath);
				DConErrOut("{}" DNewLine, _Exception.f_GetErrorStr());

				return -1;
			}
		}


		if (bMergeMode)
		{
			DConOut("Merging {} into {}" DNewLine, DestFilePath << SourceFilePath);

			bRet = fp_MergeFile(Source, DestFilePath, bOnlyWriteIfChanged);
		}
		else
		{
			DConOut("Generating code for {}" DNewLine, SourceFilePath);

			bRet = fp_GenerateFile(Source, DestFilePath, true, bOnlyWriteIfChanged);
			bRet = bRet && fp_GenerateFile(Source, DestFilePath, false, bOnlyWriteIfChanged);

			if (bRet)
			{
				// We need to output at least one file with newer date than source, othewise Visual Studio automatic file tracking fails

				CStr DestFilename = NFile::CFile::fs_GetFile(DestFilePath);
				CStr OutDir = NFile::CFile::fs_GetPath(DestFilePath);
				if (!OutDir.f_IsEmpty())
					OutDir += "/";

				DestFilename += ".h";
				DestFilePath = OutDir + DestFilename;

				CFile File;
				File.f_Open(DestFilePath, EFileOpen_Write);

			}

		}

		return bRet ? 0 : -1;
	}

private:

	bint fp_MergeFile(CRegistry & _Source, CStr const& _DestFilePath, bool _bOnlyWriteIfChanged)
	{
		CRegistry Dest;
		if (NFile::CFile::fs_FileExists(_DestFilePath))
		{
			// Read source file.
			CStr Contents;
			Contents = NFile::CFile::fs_ReadStringFromFile(CStr(_DestFilePath));

			try
			{
				Dest.f_ParseStr(Contents);
			}
			catch(NMib::NException::CException)
			{
				DConOut("Error: Failed to parse dest file: {}" DNewLine, _DestFilePath);
				return false;
			}
		}

		bint bSourcePure, bDestPure;

		EType SourceTypesUsed = fp_GetUsedTypes(_Source, bSourcePure);
		EType DestTypesUsed = fp_GetUsedTypes(Dest, bDestPure);

		if (bDestPure && DestTypesUsed != SourceTypesUsed)
		{
			// Convert dest to have per value types.

			CStr DestType = fp_TypeToName(DestTypesUsed);
			CStr Value;

			Dest.f_DeleteChild("__Type");
			for (auto RIter = Dest.f_GetChildIterator(); RIter; ++RIter)
			{
				Value = RIter->f_GetThisValue();
				RIter->f_SetThisValue(CStr(CStr::CFormat("{}:{}") << DestType << Value));
			}

			bDestPure = false;
		}

		if (bSourcePure && !bDestPure)
		{
			CStr SourceType = fp_TypeToName(SourceTypesUsed);
			CStr Value;

			_Source.f_DeleteChild("__Type");
			for (auto RIter = _Source.f_GetChildIterator(); RIter; ++RIter)
			{
				Value = RIter->f_GetThisValue();
				RIter->f_SetThisValue(CStr(CStr::CFormat("{}:{}") << SourceType << Value));
			}
		}

		for (auto RIter = _Source.f_GetChildIterator(); RIter; ++RIter)
		{
			Dest.f_SetValue(RIter->f_GetName(), RIter->f_GetThisValue());
		}

		fp_ConditionTweaks(Dest, bDestPure ? DestTypesUsed : EType_Unknown);

		CStr ResultStr = Dest.f_GenerateStr();

		if (_bOnlyWriteIfChanged)
		{
			CByteVector FileData;
			CFile::fs_WriteStringToVector(FileData, CStr(ResultStr));
			CFile::fs_CopyFileDiff(FileData, CStr(_DestFilePath), CTime::fs_NowUTC());
		}
		else
			NFile::CFile::fs_WriteStringToFile(_DestFilePath, ResultStr);

		return true;
	}

	inline_always uint8 fp_ParseHexChar(char _Ch)
	{
		if (_Ch >= '0' && _Ch <= '9')
			return uint8(_Ch) - uint8('0');
		else if (_Ch >= 'A' && _Ch <= 'Z')
			return uint8(_Ch) - uint8('A') + 10;
		else if (_Ch >= 'a' && _Ch <= 'z')
			return uint8(_Ch) - uint8('a') + 10;
		else
			return 0;
	}

	CStr fp_ConvertColor(CStr const& _Color, bint _bForCode = false)
	{
		mint nChars = _Color.f_GetLen();

		if (nChars < 7)
			return "";

		char const* pStr = _Color.f_GetStr();
		++pStr;

		uint8 R,G,B,A = 255;

		R = (fp_ParseHexChar(*pStr++) << 4);
		R |= fp_ParseHexChar(*pStr++);
		G = (fp_ParseHexChar(*pStr++) << 4);
		G |= fp_ParseHexChar(*pStr++);
		B = (fp_ParseHexChar(*pStr++) << 4);
		B |= fp_ParseHexChar(*pStr++);
		if (nChars == 9)
		{
			A = (fp_ParseHexChar(*pStr++) << 4);
			A |= fp_ParseHexChar(*pStr++);
		}

		if (_bForCode)
		{
			return CStr(CStr::CFormat("qRgba({}, {}, {}, {})") << R << G << B << A);
		}
		else
		{
			if (A != 255)
				return CStr(CStr::CFormat("rgba({}, {}, {}, {})") << R << G << B << A);
			else
				return CStr(CStr::CFormat("rgb({}, {}, {})") << R << G << B);
		}

	}

	CStr fp_ConvertColourForCode(CStr const& _Value)
	{
		char const* pStr = _Value.f_GetStr();
		++pStr;

		if (*pStr == '#')
			return fp_ConvertColor(_Value, true);
		else
		{
			uint8 R,G,B,A = 255;
			aint nParsed;

			(CStr::CParse("rgba( {} , {} , {} , {} )") >> R >> G >> B >> A).f_Parse(_Value, nParsed, NStr::EParseFlag_ExtendedWhitespace | NStr::EParseFlag_NoCase);
			if (nParsed == 4)
			{
				return	CStr::CFormat("qRgba({}, {}, {}, {})") << R << G << B << A;
			}

			(CStr::CParse("rgb( {} , {} , {} )") >> R >> G >> B).f_Parse(_Value, nParsed, NStr::EParseFlag_ExtendedWhitespace | NStr::EParseFlag_NoCase);
			if (nParsed == 3)
			{
				return	CStr::CFormat("qRgb({}, {}, {})") << R << G << B;
			}

			return "";
		}
	}

	void fp_ConditionTweaks(CRegistry& _Tweaks, EType _PureType)
	{
		if (_PureType == EType_Color)
		{
			char const* pStr;
			for (auto RIter = _Tweaks.f_GetChildIterator(); RIter; ++RIter)
			{
				pStr = RIter->f_GetThisValue().f_GetStr();

				if (*pStr == '#')
					RIter->f_SetThisValue(fp_ConvertColor(pStr));
			}
		}
		else if (_PureType == EType_Unknown)
		{
			CStr Value;
			CStr Type;
			for (auto RIter = _Tweaks.f_GetChildIterator(); RIter; ++RIter)
			{
				Value = RIter->f_GetThisValue().f_GetStr();
				Type = fg_GetStrSep(Value, ":");

				if (Type.f_CmpNoCase("QColor") == 0)
					RIter->f_SetThisValue(Type + ":" + fp_ConvertColor(Value));
			}
		}
	}


	bint fp_GenerateFile(CRegistry const& _Source, CStr const& _DestFilePath, bint _bHeader, bool _bOnlyWriteIfChanged)
	{
		CStr DestFilename = NFile::CFile::fs_GetFileNoExt(_DestFilePath);
		CStr OutDir = NFile::CFile::fs_GetPath(_DestFilePath);
		if (!OutDir.f_IsEmpty())
			OutDir += "/";

		if (_bHeader)
			DestFilename += ".h";
		else
			DestFilename += ".cpp";
		CStr DestFilePath = OutDir + DestFilename;

		bint bPure;
		EType UsedTypes = fp_GetUsedTypes(_Source, bPure);

		CStr Out;

		DConOut("Writing: {}" DNewLine, DestFilePath);

		bint bRet = fp_GenerateHeader(Out, UsedTypes, DestFilename, _bHeader);;
		bRet = bRet && fp_GenerateCode(Out, bPure ? UsedTypes : EType_Unknown, _Source, DestFilename, _bHeader);
		bRet = bRet && fp_GenerateFooter(Out, UsedTypes, DestFilename, _bHeader);

		if (!bRet)
		{
			DConOut("Generation failed for: {}" DNewLine, DestFilePath);
		}

		CStr DestPath = NFile::CFile::fs_GetPath(DestFilePath);
		NFile::CFile::fs_CreateDirectory(DestPath);
		if (_bOnlyWriteIfChanged)
		{
			CByteVector FileData;
			CFile::fs_WriteStringToVector(FileData, CStr(Out));
			CFile::fs_CopyFileDiff(FileData, CStr(DestFilePath), CTime::fs_NowUTC());
		}
		else
			CFile::fs_WriteStringToFile(CStr(DestFilePath), CStr(Out));


		return bRet;
	}

	bint fp_GenerateHeader(CStr& _Out, EType _UsedTypes, CStr const& _DestFilename, bint _bHeader)
	{
		if (_bHeader)
		{
			CStr Guard = _DestFilename.f_ReplaceChar('.', '_');

			_Out += "#pragma once" DNewLine;
			_Out += DNewLine;

			// Malterlib needs to be included first to get correct new/delete specs on OSX
			if (_UsedTypes & EType_PMetricFloat || _UsedTypes & EType_PMetricInt)
			{
				_Out += "#include <Mib/Core/Core>" DNewLine;
				_Out += "#include <AOQT/Interop/AOQT_TweakableValues.h>" DNewLine;
			}
			if (_UsedTypes & EType_Color)
				_Out += "#include <QtGui/QColor>" DNewLine;
			if (_UsedTypes & EType_Font)
			{
				_Out += "#include <QtGui/QFont>" DNewLine;
				_Out += "#include <Mib/Storage/LazyInit>" DNewLine;
			}
			_Out += DNewLine;
		}
		else
		{
			CStr Filename = NFile::CFile::fs_GetFileNoExt(_DestFilename);
			_Out += CStr::CFormat("#include \"{}.h\"" DNewLine) << Filename;
			_Out += "#include <QtCore/QFile>" DNewLine;
			_Out += "#include <QtCore/QTextStream>" DNewLine;
			_Out += "#include <Mib/Core/Core>" DNewLine;
			_Out += "#include <AOQT/Interop/AOQT_Core.h>" DNewLine;
			_Out += "#include <AOQT/Interop/AOQT_UIValues.h>" DNewLine;
		}

		_Out += "namespace NAOQT" DNewLine;
		_Out += "{" DNewLine;
		_Out += "\tnamespace NUIValues" DNewLine;
		_Out += "\t{" DNewLine;

		return true;
	}

	bint fp_GenerateFooter(CStr& _Out, EType _UsedTypes, CStr const& _DestFilename, bint _bHeader)
	{
		_Out += DNewLine;

		_Out += "\t} // Namespace NUIValues" DNewLine;
		_Out += "} // Namespace NAOQT" DNewLine;

		return true;
	}

	bint fp_GenerateCode(CStr& _Out, EType _MasterType, CRegistry const& _Reg, CStr const& _DestFileName, bint _bHeader)
	{
		EType Type;
		CStr ValueStr, TypeStr;

		bint bFirst = true;

		CStr Filename = NFile::CFile::fs_GetFileNoExt(_DestFileName);

		for (auto RIter = _Reg.f_GetChildIterator(); RIter; ++RIter)
		{
			if (RIter->f_GetName() == "__Type")
				continue;

			ValueStr = RIter->f_GetThisValue();

			if (_MasterType != EType_Unknown)
				Type = _MasterType;
			else
			{
				TypeStr = fg_GetStrSep(ValueStr, ":");
				Type = fp_NameToType(TypeStr);
			}

			switch(Type)
			{
			case EType_Int:
				if (_bHeader)
					_Out += CStr::CFormat("\t\textern int m_Int_{};" DNewLine) << RIter->f_GetName();
				else
					_Out += CStr::CFormat("\t\tint m_Int_{};" DNewLine) << RIter->f_GetName();
				break;
			case EType_Float:
				if (_bHeader)
					_Out += CStr::CFormat("\t\textern float m_Float_{};" DNewLine) << RIter->f_GetName();
				else
					_Out += CStr::CFormat("\t\tfloat m_Float_{};" DNewLine) << RIter->f_GetName();
				break;
			case EType_Color:
				if (_bHeader)
					_Out += CStr::CFormat("\t\textern QColor m_Color_{};" DNewLine) << RIter->f_GetName();
				else
					_Out += CStr::CFormat("\t\tQColor m_Color_{};" DNewLine) << RIter->f_GetName();
				break;
			case EType_Font:
				if (_bHeader)
					_Out += CStr::CFormat("\t\textern NMib::NStorage::TCLazyInit<QFont, NMib::NThread::CSpinLock> m_Font_{};" DNewLine) << RIter->f_GetName();
				else
					_Out += CStr::CFormat("\t\tNMib::NStorage::TCLazyInit<QFont, NMib::NThread::CSpinLock> m_Font_{};" DNewLine) << RIter->f_GetName();
				break;
			case EType_PMetricInt:
				if (_bHeader)
					_Out += CStr::CFormat("\t\textern CPMetricInt m_PMetricInt_{};" DNewLine) << RIter->f_GetName();
				else
					_Out += CStr::CFormat("\t\tCPMetricInt m_PMetricInt_{};" DNewLine) << RIter->f_GetName();
				break;

			case EType_PMetricFloat:
				if (_bHeader)
					_Out += CStr::CFormat("\t\textern CPMetricFloat m_PMetricFloat_{};" DNewLine) << RIter->f_GetName();
				else
					_Out += CStr::CFormat("\t\tCPMetricFloat m_PMetricFloat_{};" DNewLine) << RIter->f_GetName();
				break;
			}

			bFirst = false;

		}

		if (_bHeader)
		{
			_Out += CStr::CFormat("\t\tbool fg_Load_{}(char const* _Path, char const* _Platform, char const* _PlatformVersion );" DNewLine) << Filename;
		}
		else
		{
			_Out += DNewLine DNewLine;
			_Out += CStr::CFormat("\t\tbool fg_Load_{}(char const* _Path, char const* _Platform, char const* _PlatformVersion)" DNewLine) << Filename;
			_Out += "\t\t{" DNewLine;

			// Open Registry.
			_Out += "\t\t\tCReadValueParams Params;" DNewLine;
			_Out += "\t\t\tParams.m_Platform = _Platform;" DNewLine;
			_Out += "\t\t\tParams.m_PlatformVersion = _PlatformVersion;" DNewLine;
			_Out += "\t\t\t{" DNewLine;
			_Out += "\t\t\t\tCStr Contents;" DNewLine;

			_Out += "\t\t\t\tQFile File(_Path);" DNewLine;
			_Out += "\t\t\t\tif (File.open(QIODevice::ReadOnly))" DNewLine;
			_Out += "\t\t\t\t{" DNewLine;
			_Out += "\t\t\t\t\tQTextStream StreamIn(&File);" DNewLine;
			_Out += "\t\t\t\t\tQString QtContents = StreamIn.readAll();" DNewLine;
			_Out += "\t\t\t\t\tContents = NAOQT::fg_StrFromQT(QtContents);" DNewLine;
			_Out += "\t\t\t\t}" DNewLine;
			_Out += "\t\t\t\telse" DNewLine;
			_Out += "\t\t\t\t\treturn false;" DNewLine;

			_Out += "\t\t\t\ttry" DNewLine;
			_Out += "\t\t\t\t{" DNewLine;
            _Out += "\t\t\t\t\tCStr Path = CStr(_Path);" DNewLine;
			_Out += "\t\t\t\t\tParams.m_Registry.f_ParseStr(Contents, Path);" DNewLine;
			_Out += "\t\t\t\t}" DNewLine;
			_Out += "\t\t\t\tcatch(NMib::NException::CException const &)" DNewLine;
			_Out += "\t\t\t\t{" DNewLine;
			_Out += "\t\t\t\t\treturn false;" DNewLine;
			_Out += "\t\t\t\t}" DNewLine;
			_Out += "\t\t\t}" DNewLine;

			_Out += "\t\t\tbool bFailed = false;" DNewLine;

			// Load in each value.
			for (auto RIter = _Reg.f_GetChildIterator(); RIter; ++RIter)
			{
				if (RIter->f_GetName() == "__Type")
					continue;

				ValueStr = RIter->f_GetThisValue();

				if (_MasterType != EType_Unknown)
					Type = _MasterType;
				else
				{
					TypeStr = fg_GetStrSep(ValueStr, ":");
					Type = fp_NameToType(TypeStr);
				}

				const ch8 *pType = "Error";
				switch(Type)
				{
				case EType_Int:
					pType = "Int";
					break;
				case EType_Float:
					pType = "Float";
					break;
				case EType_Color:
					pType = "Color";
					break;
				case EType_Font:
					pType = "Font";
					break;
				case EType_PMetricInt:
					pType = "PMetricInt";
					break;
				case EType_PMetricFloat:
					pType = "PMetricFloat";
					break;
				}

				ch8 const *pLazyLoad = (Type == EType_Font) ? "*" : "";

				_Out
					+= CStr::CFormat("\t\t\tNAOQT::fg_ReadValue({}m_{}_{}, \"{}\", Params, {}, bFailed);" DNewLine)
					<< pLazyLoad
					<< pType
					<< RIter->f_GetName()
					<< RIter->f_GetName()
					<< (_MasterType != EType_Unknown ? "true" : "false")
				;
			}

			_Out += "\t\t\treturn !bFailed;" DNewLine;
			_Out += "\t\t}" DNewLine;
			_Out += DNewLine DNewLine;
		}

		return true;
	}


	EType fp_GetUsedTypes(CRegistry const& _Reg, bint& _bPure)
	{

		CRegistry const* pKey = _Reg.f_GetChild("__Type");
		if (pKey)
		{
			_bPure = true;
			return fp_NameToType(pKey->f_GetThisValue());
		}

		_bPure = false;

		EType UsedTypes = EType_Unknown;
		CStr TypeStr;
		CStr ValueStr;
		for (auto RIter = _Reg.f_GetChildIterator(); RIter; ++RIter)
		{
			ValueStr = RIter->f_GetThisValue();
			TypeStr = fg_GetStrSep(ValueStr, ":");
			UsedTypes |= fp_NameToType(TypeStr);
		}

		return UsedTypes;
	}

	EType fp_NameToType(CStr const& _Name)
	{
		if (_Name.f_CmpNoCase("int") == 0)
			return EType_Int;
		else if (_Name.f_CmpNoCase("double") == 0)
			return EType_Float;
		else if (_Name.f_CmpNoCase("QColor") == 0)
			return EType_Color;
		else if (_Name.f_CmpNoCase("QFont") == 0)
			return EType_Font;
		else if (_Name.f_CmpNoCase("CPMetricInt") == 0)
			return EType_PMetricInt;
		else if (_Name.f_CmpNoCase("CPMetricFloat") == 0)
			return EType_PMetricFloat;
		else
			return EType_Unknown;
	}

	CStr fp_TypeToName(EType _Type)
	{
		switch(_Type)
		{
		case EType_Int:
			return "int";
		case EType_Float:
			return "double";
		case EType_Color:
			return "QColor";
		case EType_Font:
			return "QFont";
		case EType_PMetricInt:
			return "CPMetricInt";
		case EType_PMetricFloat:
			return "CPMetricFloat";
		default:
			return CStr();
		}
	}


};

DMibRuntimeClass(CTool, CTool_GenerateTweaks);
