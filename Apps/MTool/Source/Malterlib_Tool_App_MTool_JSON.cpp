// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Encoding/EJSON>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Web/Curl>

class CTool_ReadJSON : public CTool2
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
		CCurrentActorScope ActorScope{WorkActor};

		TCContinuation<CStr> Contents;
		TCActor<CCurlActor> CurlActor;

		if (SourcePath.f_StartsWith("https://") || SourcePath.f_StartsWith("http://"))
		{
			CurlActor = fg_Construct(fg_Construct(), "Curl Reader");

			TCMap<CStr, CStr> Headers;

			CurlActor(&CCurlActor::f_Request, CCurlActor::EMethod_GET, SourcePath, Headers, CByteVector{}) > Contents / [Contents](CCurlActor::CResult &&_Result)
				{
					if (_Result.m_StatusCode >= 300)
						Contents.f_SetException(DMibErrorInstance("Error status: {}: {}"_f << _Result.m_StatusCode << _Result.m_Body));
					Contents.f_SetResult(_Result.m_Body);
				}
			;
		}
		else
			Contents.f_SetResult(CFile::fs_ReadStringFromFile(CFile::fs_GetExpandedPath(SourcePath, true)));

		CEJSON JSON = CEJSON::fs_FromString(Contents.f_CallSync(60.0));

		if (Params.f_IsEmpty())
		{
			DConOut("{}\n", JSON.f_ToString());
			return 0;
		}

		for (auto &Param : Params)
		{
			CEJSON const *pValue = &JSON;
			CStr CurrentPath;
			for (auto &Path : Param.f_Split("."))
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

DMibRuntimeClass(CTool, CTool_ReadJSON);

