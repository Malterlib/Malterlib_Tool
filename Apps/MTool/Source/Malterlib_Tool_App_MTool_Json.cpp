// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Encoding/EJson>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Web/Curl>

class CTool_ReadJson : public CTool2
{
public:

	aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params)
	{
		TCVector<CStr> Params = _Files;
		if (Params.f_IsEmpty())
			DError("You need to specify source file/URL");

		CStr SourcePath = Params[0];
		Params.f_Remove(0);

		auto WorkActor = fg_ConcurrentActor();
		CCurrentActorScope ActorScope{fg_ConcurrencyThreadLocal(), WorkActor};

		TCPromiseFuturePair<CStr> Contents;
		TCActor<CCurlActor> CurlActor;

		if (SourcePath.f_StartsWith("https://") || SourcePath.f_StartsWith("http://"))
		{
			TCMap<CStr, CStr> Cookies;
			if (auto pCookies = _Params.f_FindEqual("Cookies"))
			{
				for (auto &Cookie : pCookies->f_Split("&"))
				{
					auto ToSet = Cookie.f_Split("=");
					if (ToSet.f_GetLen() != 2)
						DMibError("Expected format: x=y not '{}'"_f << Cookie);

					Cookies[ToSet[0]] = ToSet[1];
				}
			}

			CurlActor = fg_Construct(fg_Construct(), "Curl Reader");

			TCMap<CStr, CStr> Headers;

			CurlActor(&CCurlActor::f_Request, CCurlActor::EMethod_GET, SourcePath, Headers, CByteVector{}, Cookies) > fg_Move(Contents.m_Promise) / [ContentsPromise = Contents.m_Promise]
				(CCurlActor::CResult &&_Result)
				{
					if (_Result.m_StatusCode >= 300)
						ContentsPromise.f_SetException(DMibErrorInstance("Error status: {}: {}"_f << _Result.m_StatusCode << _Result.m_Body));
					else
						ContentsPromise.f_SetResult(_Result.m_Body);
				}
			;
		}
		else
			Contents.m_Promise.f_SetResult(CFile::fs_ReadStringFromFile(CFile::fs_GetExpandedPath(SourcePath, true)));

		CEJsonSorted Json = CEJsonSorted::fs_FromString(fg_Move(Contents.m_Future).f_CallSync(60.0));

		if (Params.f_IsEmpty())
		{
			DConOut("{}\n", Json.f_ToString());
			return 0;
		}

		for (auto &Param : Params)
		{
			CEJsonSorted const *pValue = &Json;
			CStr CurrentPath;
			for (auto &Path : Param.f_Split<true>("."))
			{
				pValue = pValue->f_GetMember(Path);
				fg_AddStrSep(CurrentPath, ".", Path);
				if (!pValue)
					DError("Could not find path in JSON: {}\n"_f << CurrentPath);
			}
			DConOut("{}\n", pValue->f_AsString());
		}

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_ReadJson);

