// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"
#include <Mib/Encoding/Base64>

class CTool_Base64ToGUID : public CTool
{
public:

	aint f_Run(NContainer::CRegistry &_Params)
	{
		CStr Base64 = _Params.f_GetValue("0", "");

		CByteVector Data;
		fg_Base64Decode(Base64, Data);

		uint32 First = 0;
		uint16 Second = 0;
		uint16 Third = 0;
		uint16 Forth = 0;
		uint64 Fifth = 0;

		CBinaryStreamMemoryPtr<> Stream;
		Stream.f_OpenRead(Data);

		Stream >> First;
		Stream >> Second;
		Stream >> Third;
		uint64 Last;
		Stream >> Last;

		Forth = (fg_ByteSwap(Last) << 48) & uint64(0xFFFF);
		Fifth = fg_ByteSwap(Last) & uint64(0xFFFFFFFFFFFFull);

		CStr Format = CStr::CFormat("{{{nfh}-{nfh}-{nfh}-{nfh}-{nfh}}") << First << Second << Third << Forth << Fifth;

		DConOut("{}\n", Format);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_Base64ToGUID);
