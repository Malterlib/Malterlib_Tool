// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"
#include <Mib/Cryptography/UUID>

class CTool_PerforceCreateStream : public CTool2
{
public:

	struct CCreateStreamState
	{
		CStr m_UniqueCleanName;
		TCMap<CStr, CStr> m_StreamMap;
		zbool m_bTaskStream;
		zint32 m_ImportIsolated;
		CCreateStreamState()
		{
		}
	};


	void fr_GetIsolatedPaths(CPerforceClientThrow &_Client, CStr const &_StreamName, TCVector<CStr> &_Ret)
	{
		CPerforceClient::CStream Stream = _Client.f_GetStream(_StreamName);

		if (!Stream.m_Parent.f_IsEmpty() && Stream.m_Parent != "none")
			fr_GetIsolatedPaths(_Client, Stream.m_Parent, _Ret);

		for (auto iPath = Stream.m_Paths.f_GetIterator(); iPath; ++iPath)
		{
			CStr Type;
			CStr Path;
			CStr DepotPath;
			(CStr::CParse("{} {} {}") >> Type >> Path >> DepotPath).f_Parse(*iPath);

			if (Type == "isolate")
				_Ret.f_Insert(*iPath);
		}
	}

	CStr fr_CreateStreams
		(
			CPerforceFunctions *_pFunctions
			, CPerforce_TemporaryStreamSwitcher &_StreamSwitcher
			, CBlockingStdInReader &_Reader
			, CStr const &_StreamName
			, CStr const &_StreamParent
			, bool _bTaskStream
			, CCreateStreamState &_State
			, bool _bJustUpdate
			, TCSet<CStr> const &_DisableCreate
			, bool _bUseDefaults
			, CPerforceFunctions::COwnedStreams &_OwnedStreams
			, bool _bAutoCreateOwned
			, bool _bAutoImportSDK
		)
	{
		CPerforceClient::CStream ParentStream = _pFunctions->f_GetStreamCached(_StreamParent);

		CRegistryPreserveAll Registry;

		if (_bJustUpdate)
			Registry = CPerforceFunctions::fs_GetRegistry(ParentStream);

		CPerforceClient::CStream NewStream;
		if (_bTaskStream)
		{
			_State.m_bTaskStream = true;
			if (_State.m_UniqueCleanName.f_IsEmpty())
			{
				if (_bJustUpdate)
				{
					_State.m_UniqueCleanName = Registry.f_GetValue("UniqueName", CStr());
					if (_State.m_UniqueCleanName.f_IsEmpty())
						DError(fg_Format("No unique name found in description in stream ({}) (UniqueName)", _StreamParent));
				}
				else
				{
					_State.m_UniqueCleanName = fg_GetRandomUuidString();
				}
			}
		}

		if (!_State.m_UniqueCleanName.f_IsEmpty())
			Registry.f_SetValue("UniqueName", _State.m_UniqueCleanName);

		CStr CleanName;
		if (_bJustUpdate)
			CleanName = _StreamParent;
		else if (_State.m_bTaskStream)
			CleanName = fg_Format("{}.{}", _StreamParent, _State.m_UniqueCleanName);
		else
			CleanName = fg_Format("//{}/{}", CPerforceFunctions::fs_GetDepot(_StreamParent), _StreamName.f_Replace(" (", ".").f_Replace(")", "").f_Replace(" ", "_").f_Replace("/", "."));

		if (!_bJustUpdate && _pFunctions->f_GetClient().f_StreamExists(CleanName))
		{
			_OwnedStreams += _pFunctions->f_GetStreamOwned(CleanName, true);
			return CleanName; // Already exists
		}

		DConOut("Created:\t{}{\n}", CleanName);
		DConOut("\t\t//{}/{}{\n}", CPerforceFunctions::fs_GetDepot(CleanName) << _StreamName);

		NewStream.m_Name = _StreamName;
		NewStream.m_Parent = _StreamParent;
		if (_bTaskStream)
			NewStream.m_Type = "task";
		else
			NewStream.m_Type = "development";

		NewStream.m_Options.f_Insert("allsubmit");
		NewStream.m_Options.f_Insert("fromparent");
		CRegistryPreserveAll ParentRegistry = CPerforceFunctions::fs_GetRegistry(ParentStream);
		if (ParentRegistry.f_GetValue("MergeInto", "true") == "true")
			NewStream.m_Options.f_Insert("toparent");

		CStr ParentStreamName = ParentStream.m_Name;
		CStr ParentStreamOwned;
		CStr DefaultImportPrefix;

		TCSet<CStr> DisableCreateStreams;

		if (_bJustUpdate)
		{
			NewStream = ParentStream;
			NewStream.m_Paths.f_Clear();

			if (ParentStream.m_Parent != "none")
			{
				CPerforceClient::CStream ParentParentStream = _pFunctions->f_GetClient().f_GetStream(ParentStream.m_Parent);
				ParentStreamName = ParentParentStream.m_Name;
				ParentStreamOwned = ParentStream.m_Parent;
				DisableCreateStreams = _pFunctions->f_GetDisabledCreate(ParentParentStream);
			}
			else
			{
				DisableCreateStreams = _pFunctions->f_GetDisabledCreate(ParentStream);
				ParentStreamOwned = _StreamParent;
			}
		}
		else
		{
			DisableCreateStreams = _pFunctions->f_GetDisabledCreate(ParentStream);
			ParentStreamOwned = _StreamParent;
		}

		DisableCreateStreams += _DisableCreate;

		CStr Suffix;
		CStr ParentSuffix;
		CStr Prefix = CPerforceFunctions::fs_GetCommonPath(ParentStreamName, _StreamName, ParentSuffix, Suffix);
		//DConOut("Prefix: {}{\n}", Prefix);
		//DConOut("Suffix: {}{\n}", Suffix);
		//DConOut("ParentSuffix: {}{\n}", ParentSuffix);

		CPerforceFunctions::COwnedStreams ParentOwnedStreams = _pFunctions->f_GetStreamOwned(ParentStreamOwned, true);

		CPerforceFunctions::COwnedStreams OwnedStreams;
		CPerforceFunctions::COwnedStreams OwnedStreamsRecursive;

		OwnedStreams.f_AddStream(CleanName, ParentStreamOwned);
		OwnedStreamsRecursive.f_AddStream(CleanName, ParentStreamOwned);
		if (_bJustUpdate)
		{
			CPerforceFunctions::COwnedStreams ThisStreamOwned = _pFunctions->f_GetStreamOwned(CleanName, true);
			OwnedStreams += ThisStreamOwned;
			OwnedStreamsRecursive += ThisStreamOwned;
		}

		DefaultImportPrefix = Registry.f_GetValue("DefaultImportPrefix", "");

		auto pOwned = Registry.f_GetChildNoPath("OwnedStreams");
		if (pOwned)
		{
			for (auto iOwned = pOwned->f_GetChildIterator(); iOwned; ++iOwned)
			{
				OwnedStreams.f_AddStream(iOwned->f_GetThisValue(), iOwned->f_GetName());
				OwnedStreamsRecursive.f_AddStream(iOwned->f_GetThisValue(), iOwned->f_GetName());
			}
		}

		CPerforceFunctions::COwnedStreams OwnedStreamsOriginal = OwnedStreams;

		TCMap<CStr, TCMap<CStr, CStr>> SDKImports;

		TCSet<CStr> AlreadyImported;

		bool bHasShare = false;

		CRegistryPreserveOrder StreamConfig;
		{
			TCUniquePointer<CPerforceClientThrow> pClientOwned;
			CPerforceClientThrow *pClient = &_pFunctions->f_GetClient();
			CStr StreamConfigName;
			if (_bJustUpdate)
			{
				CStr DestinationWorkspace = _StreamSwitcher.f_GetClientForStream(CleanName);
				CPerforceClient::CConnectionInfo ConnectionInfo = _pFunctions->f_GetClient().f_GetConnectionInfo();
				ConnectionInfo.m_Client = DestinationWorkspace;
				pClientOwned = fg_Construct(ConnectionInfo);
				pClientOwned->f_Login(CStr());
				pClient = pClientOwned.f_Get();
				StreamConfigName = CleanName + "/Stream.conf";
			}
			else
				StreamConfigName = _StreamParent + "/Stream.conf";

			if (pClient->f_FileExists(StreamConfigName))
			{
				auto Stats = pClient->f_FileStats(StreamConfigName);
				CStr Config;
				if (_bJustUpdate)
				{
					CStr WorkspacePath = pClient->f_GetClientPath(StreamConfigName);
					if (Stats.m_HaveRev < Stats.m_HeadRev || !CFile::fs_FileExists(WorkspacePath))
						Config = pClient->f_GetTextFileContents(StreamConfigName);
					else
						Config = CFile::fs_ReadStringFromFile(CStr(WorkspacePath), true);
				}
				else
					Config = pClient->f_GetTextFileContents(StreamConfigName);

				StreamConfig.f_ParseStr(Config, StreamConfigName);
			}
		}

		TCVector<CStr> ProtectedPaths;
		{
			CStr Protected = StreamConfig.f_GetValue("ProtectedImports", "");
			while (!Protected.f_IsEmpty())
				ProtectedPaths.f_Insert(fg_GetStrSep(Protected, ";"));
		}

		bool bAutoCreateOwned = StreamConfig.f_GetValue("AutoCreateOwned", "false") == "true" || _bAutoCreateOwned;
		bool bAutoImportSDK = StreamConfig.f_GetValue("AutoImportSDK", "false") == "true" || _bAutoImportSDK;

		auto fl_CreateImported
			= [&](CStr const& _Stream, CStr const &_Source) -> CStr
			{
				// Already owned, so we shouldn't try to import this
				if (OwnedStreams.m_StreamMap.f_FindEqual(_Stream))
					return _Stream;

				// Disabled create in settings
				if (DisableCreateStreams.f_FindEqual(_Stream))
					return _Stream;

				for (auto &Protected : ProtectedPaths)
				{
					if (fg_StrMatchWildcard(_Source.f_GetStr(), Protected.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted)
						return _Stream;
				}

				CPerforceClient::CStream ImportStream = _pFunctions->f_GetClient().f_GetStream(_Stream);

				CRegistryPreserveAll ImportRegistry = CPerforceFunctions::fs_GetRegistry(ImportStream);

				// Child creation disabled
				if (ImportRegistry.f_GetValue("CreateStreamChildren", "true") != "true")
					return _Stream;

				// Already imported into stream
				if (!_State.m_UniqueCleanName.f_IsEmpty() && ImportRegistry.f_GetValue("UniqueName", "") == _State.m_UniqueCleanName)
					return _Stream;


				CStr ImportPrefix = ImportStream.m_Name;

				if (!ParentSuffix.f_IsEmpty())
				{
					auto iFind = ImportPrefix.f_FindReverse(ParentSuffix);
					if (iFind == ImportPrefix.f_GetLen() - ParentSuffix.f_GetLen())
						ImportPrefix = ImportPrefix.f_Left(iFind);
				}

				if (ImportPrefix.f_IsEmpty() && !DefaultImportPrefix.f_IsEmpty())
					ImportPrefix = DefaultImportPrefix + "/";

				CStr NewName = ImportPrefix + Suffix;

				CStr ImportDepot = CPerforceFunctions::fs_GetDepot(_Stream);

				DConOut("Create new stream for import (from, to):{\n}\t//{}/{}{\n}\t//{}/{}{\n}", ImportDepot << ImportStream.m_Name << ImportDepot << NewName);

				bool bOwnedParent = ParentOwnedStreams.m_StreamMap.f_FindEqual(_Stream);

				CStr Ask;
				if (bOwnedParent)
					Ask = "[Y/n/name]{\n}";
				else
					Ask = "[y/N/name]{\n}";

				bool bAutoCreate = (bAutoCreateOwned && bOwnedParent) || _bUseDefaults;

				CStr Reply;
				if (!bAutoCreate)
				{
					Reply = fg_AskUser
						(
							_Reader
							, Ask
						)
					;
				}
				bool bDefault = Reply == "";
				if (Reply == "N" || Reply == "n" || (!bOwnedParent && bDefault))
				{
					if (bDefault)
						DConOut("N{\n}", 0);

					return _Stream;
				}
				else
				{

					if (Reply == "Y" || Reply == "y" || (bOwnedParent && bDefault))
					{
						if (bDefault)
							DConOut("Y{\n}", 0);
					}
					else
					{
						DConOut("Name: {}{\n}", Reply);
						NewName = Reply;
					}

					CStr ParentStream = _Stream;
					bool bJustUpdate = false;
					if (_State.m_bTaskStream)
					{
						CStr NewCleanName = fg_Format("{}.{}", _Stream, _State.m_UniqueCleanName);
						if (_pFunctions->f_GetClient().f_StreamExists(NewCleanName))
						{
							bJustUpdate = true;
							ParentStream = NewCleanName;
						}
					}

					CStr Ret = fr_CreateStreams(_pFunctions, _StreamSwitcher, _Reader, NewName, ParentStream, false, _State, bJustUpdate, DisableCreateStreams, _bUseDefaults, OwnedStreamsRecursive, bAutoCreateOwned, bAutoImportSDK);
					OwnedStreamsRecursive.f_AddStream(Ret, _Stream);
					OwnedStreams.f_AddStream(Ret, _Stream);
					return Ret;
				}
			}
		;

		{
			auto Owned = _pFunctions->f_GetStreamOwned(ParentStreamOwned, false);
			for (auto &Stream : Owned.m_Streams)
			{
				// Create owned streams that are not imported
				auto Mapped = _State.m_StreamMap(fg_Get<1>(Stream));
				if (Mapped.f_WasCreated())
					*Mapped = fl_CreateImported(fg_Get<1>(Stream), fg_Get<1>(Stream));
			}
		}

		bool bImportsDone = false;
		TCVector<CStr> ExtraImports;
		{
			TCMap<CStr, CStr> TranslatedStreamCache;

			auto fl_GetTranslatedStream
				= [&](CStr const& _StreamToTranslate) -> CStr
				{
					auto *pCached = TranslatedStreamCache.f_FindEqual(_StreamToTranslate);
					if (pCached)
						return *pCached;
					CStr TranslatedStream;
					for (auto iStream = OwnedStreamsRecursive.m_Streams.f_GetIterator(); iStream && TranslatedStream.f_IsEmpty(); ++iStream)
					{
						CStr StreamPath = fg_Get<0>(*iStream);

						while (!StreamPath.f_IsEmpty() && StreamPath != "none")
						{
							if (StreamPath == _StreamToTranslate)
							{
								TranslatedStream = fg_Get<0>(*iStream);
								break;
							}
							if (_pFunctions->f_GetClient().f_StreamExists(StreamPath))
							{
								CPerforceClient::CStream Stream = _pFunctions->f_GetStreamCached(StreamPath);
								StreamPath = Stream.m_Parent;
							}
							else
							{
								if (StreamPath == fg_Get<1>(*iStream))
									break;
								StreamPath = fg_Get<1>(*iStream);
							}
						}
					}
					TranslatedStreamCache[_StreamToTranslate] = TranslatedStream;
					return TranslatedStream;
				}
			;

			for (auto iChild = StreamConfig.f_GetChildIterator(); iChild; ++iChild)
			{
				CStr Type = iChild->f_GetName();
				CStr Path;
				CStr RawDepotPath;
				(CStr::CParse("{} {}") >> Path >> RawDepotPath).f_Parse(iChild->f_GetThisValue());

				CStr DepotPath;

				{
					CStr Stream;
					CStr Import;
					aint nParsed = 0;
					(CStr::CParse("{{{}}{}") >> Stream >> Import).f_Parse(RawDepotPath, nParsed);
					if (nParsed == 2)
					{
						CStr TranslatedStream = fl_GetTranslatedStream(Stream);
						if (TranslatedStream.f_IsEmpty())
						{
							DError(fg_Format("'{}' stream not found as a parent of any of the owned streams", Stream));
						}
						DepotPath = fg_Format("{}{}", TranslatedStream, Import);
					}
					else
						DepotPath = RawDepotPath;
				}

				if (Type == "import" || Type == "import+")
				{
					bImportsDone = true;
					CStr ImportFromStream = CPerforceFunctions::fs_GetStream(DepotPath);
					auto Mapped = _State.m_StreamMap(ImportFromStream);
					if (Mapped.f_WasCreated())
						*Mapped = fl_CreateImported(ImportFromStream, DepotPath);

					if (!AlreadyImported.f_FindEqual(Path))
					{
						CStr NewPath = fg_Format("{} {} {}", Type, Path, DepotPath.f_Replace(ImportFromStream, *Mapped));
						//DConOut("{}{\n}", NewPath);
						ExtraImports.f_Insert(NewPath);
						AlreadyImported[Path];
					}

					if (CPerforceFunctions::fs_GetDepot(ImportFromStream) == "SDK")
						SDKImports[ImportFromStream][Path] = DepotPath;
				}
			}
		}

		for (auto iPath = ParentStream.m_Paths.f_GetIterator(); iPath; ++iPath)
		{
			//DMibConOut("{}{\n}", *iPath);
			CStr Type;
			CStr Path;
			CStr DepotPath;
			(CStr::CParse("{} {} {}") >> Type >> Path >> DepotPath).f_Parse(*iPath);


			if (Type == "import" || Type == "import+")
			{
				if (!bImportsDone)
				{
					CStr ImportFromStream = CPerforceFunctions::fs_GetStream(DepotPath);
					auto Mapped = _State.m_StreamMap(ImportFromStream);
					if (Mapped.f_WasCreated())
						*Mapped = fl_CreateImported(ImportFromStream, DepotPath);

					if (!AlreadyImported.f_FindEqual(Path))
					{
						NewStream.m_Paths.f_Insert(fg_Format("{} {} {}", Type, Path, DepotPath.f_Replace(ImportFromStream, *Mapped)));
						AlreadyImported[Path];
					}

					if (CPerforceFunctions::fs_GetDepot(ImportFromStream) == "SDK")
						SDKImports[ImportFromStream][Path] = DepotPath;
				}
			}
			else if (Type == "share")
			{
				bHasShare = true;
				NewStream.m_Paths.f_Insert(*iPath);
			}
			else if (_bJustUpdate)
				NewStream.m_Paths.f_Insert(*iPath);
			//else
			//	DConOut("Type: {} Path: {} DepotPath: {}{\n}", Type << Path << DepotPath);
			//NewStream.m_Paths.f_Insert(*iPath);
		}

		bool bHasIsolated = false;
		for (auto iImport = SDKImports.f_GetIterator(); iImport; ++iImport)
		{
			CStr ImportFromStream = iImport.f_GetKey();

			auto *pOwned = OwnedStreamsRecursive.m_StreamMap.f_FindEqual(ImportFromStream);
			if (pOwned)
				ImportFromStream = *pOwned;
			else
				continue;

			// Task streams import isolated folders (for example builds of Qt and OpenSSL)
			TCVector<CStr> Isolated;
			fr_GetIsolatedPaths(_pFunctions->f_GetClient(), ImportFromStream, Isolated);
			if (!Isolated.f_IsEmpty())
			{
				bHasIsolated = true;
				break;
			}
		}

		if (bHasIsolated)
		{
			if (_State.m_ImportIsolated == 0)
			{
				if (bAutoImportSDK)
				{
					if (_State.m_bTaskStream)
						_State.m_ImportIsolated = 1;
					else
						_State.m_ImportIsolated = 2;
				}
				else
				{
					if (_State.m_bTaskStream)
					{
						CStr Reply = fg_AskUser
							(
								_Reader
								, "Would you like to import SDK builds from parent stream? (Y/n){\n}"
							)
						;
						if (Reply == "Y" || Reply == "y" || Reply == "")
							_State.m_ImportIsolated = 1;
						else
							_State.m_ImportIsolated = 2;
					}
					else
					{
						CStr Reply = fg_AskUser
							(
								_Reader
								, "Would you like to import SDK builds from parent stream? (y/N){\n}"
							)
						;
						if (Reply == "Y" || Reply == "y")
							_State.m_ImportIsolated = 1;
						else
							_State.m_ImportIsolated = 2;
					}
				}
			}

			if (_State.m_ImportIsolated == 1)
			{
				for (auto iImport = SDKImports.f_GetIterator(); iImport; ++iImport)
				{
					CStr ImportFromStream = iImport.f_GetKey();
					auto *pOwned = OwnedStreamsRecursive.m_StreamMap.f_FindEqual(ImportFromStream);
					CStr ParentImportFromStream;
					if (pOwned)
						ParentImportFromStream = *pOwned;
					else
						continue;

					// Task streams import isolated folders (for example builds of Qt and OpenSSL)
					TCVector<CStr> Isolated;
					fr_GetIsolatedPaths(_pFunctions->f_GetClient(), _bJustUpdate ? ImportFromStream : ParentImportFromStream, Isolated);
					for (auto iIsolate = Isolated.f_GetIterator(); iIsolate; ++iIsolate)
					{
						CStr Type;
						CStr Path;
						(CStr::CParse("{} {}") >> Type >> Path).f_Parse(*iIsolate);

						CStr FullPath = CFile::fs_AppendPath(ImportFromStream, Path);

						for (auto iImported = iImport->f_GetIterator(); iImported; ++iImported)
						{
							CStr ImportedPath = iImported.f_GetKey();
							CStr ImportedDepotPath = *iImported;
							CStr ImportedSuffix;
							CStr Suffix;
							CStr Prefix = CPerforceFunctions::fs_GetCommonPath(ImportedDepotPath, FullPath, ImportedSuffix, Suffix);
							if (Suffix.f_IsEmpty())
								Suffix = "/...";
							else
								Suffix = "/" + Suffix;
							if (ImportedSuffix.f_IsEmpty() || ImportedSuffix == "...")
							{
								CStr ImportPath = ImportedPath.f_Replace("/...", Suffix);
								CStr ImportDepotPath = ImportedDepotPath.f_Replace("/...", Suffix).f_Replace(ImportFromStream, ParentImportFromStream);
								if (!AlreadyImported.f_FindEqual(ImportPath))
								{
									AlreadyImported[ImportPath];
									NewStream.m_Paths.f_Insert(fg_Format("import {} {}", ImportPath, ImportDepotPath));
								}
							}
						}

					}
				}
			}
		}

		NewStream.m_Paths.f_Insert(ExtraImports);

		if (!OwnedStreams.m_Streams.f_IsEmpty())
		{
			auto pOwned = Registry.f_CreateChild("OwnedStreams");

			for (auto iStream = OwnedStreams.m_Streams.f_GetIterator(); iStream; ++iStream)
			{
				if (!OwnedStreamsOriginal.m_StreamMap.f_FindEqual(fg_Get<0>(*iStream)))
					pOwned->f_CreateChildNoPath(fg_Get<1>(*iStream), true)->f_SetThisValue(fg_Get<0>(*iStream));
			}
		}
		NewStream.m_Description = Registry.f_GenerateStr();

#if 0
		DConOut("//{}/{}:{\n}", CPerforceFunctions::fs_GetDepot(_StreamParent) << _StreamName);
		DConOut("Desc:\n{}", NewStream.m_Description);

		for (auto iPath = NewStream.m_Paths.f_GetIterator(); iPath; ++iPath)
		{
			DConOut("{}{\n}", *iPath);
		}
#else

		bool bWasCreated = !_pFunctions->f_GetClient().f_StreamExists(CleanName);
		_pFunctions->f_GetClient().f_SetStream(CleanName, NewStream);

		if (bWasCreated && bHasShare)
			_pFunctions->f_GetClient().f_PopulateStream(CleanName);
#endif

		_OwnedStreams += OwnedStreams;
		return CleanName;
	}

	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr DoneMessage = "Done!";
		auto ReportDone
			= g_OnScopeExit > [&]
			{
				DConOut("{}{\n}", DoneMessage);
			}
		;
		CStr ParentStreamName = f_GetOption(_Params, "Stream").f_Trim();
		CStr StreamType = f_GetOption(_Params, "Type", "").f_Trim();
		CStr NewStreamName = f_GetOption(_Params, "NewStreamName", "").f_Trim();

		bool bUpdate = f_GetOption(_Params, "Update", "").f_Trim() == "true";
		bool bQuite = f_GetOption(_Params, "Quite", "").f_Trim() == "true";
		bool bAutoCreateOwned = f_GetOption(_Params, "AutoCreateOwned", "").f_Trim() == "true";
		bool bAutoImportSDK = f_GetOption(_Params, "AutoImportSDK", "").f_Trim() == "true";

		if (ParentStreamName.f_IsEmpty())
			DError("You have to specify Stream");

		if (!bUpdate && StreamType != "Task" && StreamType != "Normal")
			DError("You have to specify Type (Task or Normal)");

		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
		CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");

		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = P4Client;

		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());

		CPerforceFunctions Functions(pClient);

		CPerforce_TemporaryStreamSwitcher StreamSwitcher(Functions);

		CPerforceClient::CStream ParentStream = pClient->f_GetStream(ParentStreamName);

		DConOut("Parent stream:{\n}", ParentStreamName);
		DConOut("\t{}{\n}", ParentStreamName);
		DConOut("\t//{}/{}{\n}", CPerforceFunctions::fs_GetDepot(ParentStreamName) << ParentStream.m_Name);

		CBlockingStdInReader StdInReader;

		CStr StreamName;

		if (bUpdate)
		{
			if (ParentStream.m_Type == "task")
				StreamType = "Task";
			else
			{
				CRegistryPreserveAll Registry;
				Registry = CPerforceFunctions::fs_GetRegistry(ParentStream);
				if (!Registry.f_GetValue("UniqueName", CStr()).f_IsEmpty())
					StreamType = "Task";
			}
			StreamName = ParentStream.m_Name;
		}
		else
		{
			if (NewStreamName.f_IsEmpty())
			{
				if (StreamType == "Task")
				{
					CStr Reply = fg_AskUser
						(
							StdInReader
							, "Please provide a name for the task{\n}"
						)
					;
					if (Reply.f_IsEmpty())
						DError("Task name cannot be empty");
					StreamName = ParentStream.m_Name + " (" + Reply + ")";
				}
				else
				{
					CStr Reply = fg_AskUser
						(
							StdInReader
							, "Please provide the full name for the stream{\n}"
						)
					;
					if (Reply.f_IsEmpty())
						DError("Stream name cannot be empty");
					StreamName = Reply;
				}
			}
			else
			{
				if (StreamType == "Task")
					StreamName = ParentStream.m_Name + " (" + NewStreamName + ")";
				else
					StreamName = NewStreamName;

			}
		}

		CCreateStreamState State;
		CPerforceFunctions::COwnedStreams OwnedStreams;
		fr_CreateStreams(&Functions, StreamSwitcher, StdInReader, StreamName, ParentStreamName, StreamType == "Task", State, bUpdate, TCSet<CStr>(), bQuite, OwnedStreams, bAutoCreateOwned, bAutoImportSDK);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceCreateStream);

class CTool_PerforceDeleteStream : public CTool2
{
public:

	void fr_DeleteStreams(CPerforceClientThrow &_Client, CBlockingStdInReader &_Reader, CStr const &_StreamName, bool _bTaskStream, bool _bForceObliterate)
	{
		CPerforceClient::CStream Stream = _Client.f_GetStream(_StreamName);

		CRegistry Registry;

		if (!Stream.m_Description.f_IsEmpty() && !Stream.m_Description.f_StartsWith("Created by"))
			Registry.f_ParseStr(Stream.m_Description);

		if (Stream.m_Type == "task")
			_bTaskStream = true;

		auto pOwned = Registry.f_GetChildNoPath("OwnedStreams");
		if (pOwned)
		{
			for (auto iOwned = pOwned->f_GetChildIterator(); iOwned; ++iOwned)
			{
				if (_Client.f_StreamExists(iOwned->f_GetThisValue()))
					fr_DeleteStreams(_Client, _Reader, iOwned->f_GetThisValue(), _bTaskStream, _bForceObliterate);
			}
		}

		CStr Question = "Are you sure you want to delete the stream named";
		if (_bForceObliterate)
			Question = "Are you sure you want to delete AND OBLITERATE the stream named";
		CStr Reply;
		if (_bTaskStream)
		{
			// When we are a task stream, default to deleting the owned streams
			Reply = fg_AskUser
				(
					_Reader
					, fg_Format("{}: {}? [Y/n]{\n}", Question, _StreamName)
				)
			;
			if (Reply == "")
				Reply = "y";
		}
		else
		{
			Reply = fg_AskUser
				(
					_Reader
					, fg_Format("{}: {}? [y/N]{\n}", Question, _StreamName)
				)
			;
		}

		if (Reply == "Y" || Reply == "y")
		{
			DConOut("Delete stream: {}{\n}", _StreamName);
			TCSet<CStr> Clients;
			_Client.f_GetClients
				(
					""
					, _StreamName
					, ""
					, [&](CStr const &_Client, CStr const &_Key, CStr const &_Value)
					{
						Clients[_Client];
					}
				)
			;

			CPerforceClient::CStream Stream = _Client.f_GetStream(_StreamName);
			CStr ParentStream = Stream.m_Parent;
			if (ParentStream == "none")
				ParentStream = "";

			for (auto &Client : Clients)
				_Client.f_SwitchWorkspaceStream(Client, ParentStream);

			_Client.f_DeleteStream(_StreamName);
			if ((Stream.m_Type != "task" && _bTaskStream) || _bForceObliterate)
			{
				CStr ToObliterate = _StreamName + "/...";
				DConOut("Obliterate: {}{\n}", ToObliterate);
				_Client.f_Obliterate(ToObliterate);
			}
		}

	}

	void f_DetectPendingMerges(CPerforce_TemporaryStreamSwitcher &_StreamSwitcher, TCUniquePointer<CPerforceClientThrow> & _pClient, CStr const& _Stream)
	{
		CPerforceFunctions Functions(_pClient);


		CPerforceFunctions::COwnedStreams OwnedStreams = Functions.f_GetStreamOwned(_Stream, true);
		TCSet<CStr> Streams;
		Streams[_Stream];
		for (auto iStream = OwnedStreams.m_Streams.f_GetIterator(); iStream; ++iStream)
			Streams[fg_Get<0>(*iStream)];

		for (auto& Stream : Streams)
		{
			auto& StreamInfo = Functions.f_GetStreamCached(Stream);
			if (StreamInfo.m_Parent.f_IsEmpty() || StreamInfo.m_Parent == "none")
				continue;

			DConOut("Checking stream: {}/{}{\n}", Functions.fs_GetDepot(Stream) << StreamInfo.m_Name);

			CStr DestinationWorkspace = _StreamSwitcher.f_GetClientForStream(StreamInfo.m_Parent, true);

			TCUniquePointer<CPerforceClientThrow> pClient;
			CPerforceClient::CConnectionInfo ConnectionInfo;
			ConnectionInfo.m_Server = _pClient->f_GetServer();
			ConnectionInfo.m_User = _pClient->f_GetUser();
			ConnectionInfo.m_Client = DestinationWorkspace;
			pClient = fg_Construct(ConnectionInfo);
			pClient->f_Login(CStr());

			TCVector<CPerforceClient::CIntegrationResult> Integrated;
			TCVector<CStr> MustSync;
			TCVector<CPerforceClient::CMergeError> Errors;
			TCSet<CPerforceClient::CIntegrationResult> UniqueResults;
			for (int i = 0; i < 2; ++i)
			{
				pClient->f_NoThrow().f_IntegrateStream(Stream, StreamInfo.m_Parent, true, Integrated, MustSync, Errors);

				CStr Error = pClient->f_NoThrow().f_GetLastError();
				if (!Error.f_IsEmpty())
					DConOut("{}\n", Error);

				for (auto& Error : Errors)
					DConOut("{}: {}\n", Error.m_Path << Error.m_Error);

				if (i == 0)
				{
					for (auto iResult = Integrated.f_GetIterator(); iResult; ++iResult)
					{
						switch (iResult->m_Action)
						{
						case CPerforceClient::EAction_Edit:
						case CPerforceClient::EAction_Integrate:
							MustSync.f_Insert(iResult->m_To);
							break;
						default:
							break;
						}
					}

					if (!MustSync.f_IsEmpty())
					{
						if (!pClient->f_NoThrow().f_Sync(CStr(), fg_Default(), false, MustSync))
						{
							CStr Error = pClient->f_NoThrow().f_GetLastError();
							if (Error != "File(s) up-to-date.")
								pClient->f_ThrowLastError();
						}
					}
				}

				for (auto iResult = Integrated.f_GetIterator(); iResult; ++iResult)
					UniqueResults[*iResult];
			}


			TCMap<CPerforceClient::EAction, zuint32> Actions;
			TCSet<int32> ChangeLists;
			TCMap<CStr, TCVector<CStr>> FileRevsToCheck;
			for (auto iResult = UniqueResults.f_GetIterator(); iResult; ++iResult)
			{
				++Actions[iResult->m_Action];

				DConOut("{}#{},{} -> {} \n", iResult->m_From << (iResult->m_StartFromRev + 1) << iResult->m_EndFromRev << iResult->m_To);

				CStr RevRange = fg_Format("{}#{},{}", iResult->m_From, iResult->m_StartFromRev + 1, iResult->m_EndFromRev);
				FileRevsToCheck[iResult->m_To].f_Insert(RevRange);
			}

			for (auto iRevToCheck = FileRevsToCheck.f_GetIterator(); iRevToCheck; ++iRevToCheck)
			{
				TCSet<int32> AlreadyIntegratedChangelists;
				{
					//auto TempLists = pClient->f_GetChangelists(iRevToCheck.f_GetKey(), true);
					//for (auto &List : TempLists)
						//AlreadyIntegratedChangelists[int32(List.m_ChangeID)];
				}

				CPerforceClient::CFileRevisions Revisions = pClient->f_GetFileRevisions(*iRevToCheck);
				for (auto &File : Revisions.m_Files)
				{
					for (auto &Revision : File.m_Revisions)
					{
						if (!AlreadyIntegratedChangelists.f_FindEqual(Revision.m_ChangeList))
							ChangeLists[Revision.m_ChangeList];
					}
				}
			}

			if (!ChangeLists.f_IsEmpty())
			{
				DConOut("PENDING INTEGRATIONS:{\n}{\n}", 0);

				TCMap<CStr, TCVector<CStr>> Comments;
				for (auto &ChangeList : ChangeLists)
				{
					CPerforceClient::CChangeList ChangeListInfo = pClient->f_GetChangelist(ChangeList);
					DConOut("{}: {}  {}{\n}{}{\n}", ChangeList << ChangeListInfo.m_PerforceDate << ChangeListInfo.m_User << ChangeListInfo.m_Description);
				}
			}

		}
	}

	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr DoneMessage = "Done!";
		auto ReportDone
			= g_OnScopeExit > [&]
			{
				DConOut("{}{\n}", DoneMessage);
			}
		;
		CStr StreamName = f_GetOption(_Params, "Stream").f_Trim();

		if (StreamName.f_IsEmpty())
			DError("You have to specify Stream");

		bool bForceObliterate = f_GetOption(_Params, "ForceObliterate", "").f_Trim() == "true";

		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
		CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");

		CBlockingStdInReader StdInReader;

		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = P4Client;

		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());

		CPerforceFunctions Functions(pClient);
		CPerforce_TemporaryStreamSwitcher StreamSwitcher(Functions);

		f_DetectPendingMerges(StreamSwitcher, pClient, StreamName);

		fr_DeleteStreams(*pClient, StdInReader, StreamName, false, bForceObliterate);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceDeleteStream);
