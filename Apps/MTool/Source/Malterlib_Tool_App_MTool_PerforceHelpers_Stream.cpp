// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"
#include <Mib/Cryptography/UUID>

class CTool_PerforceInitializeStream : public CTool2
{
public:
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
		
		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
		CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");
		
		DConOut("Stream: {}{\n}", StreamName);
		
		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = P4Client;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());

		DConOut("Logged in to perforce{\n}", 0);
		
		CPerforceFunctions Functions(pClient);
		
		if (StreamName == "*")
		{
			TCVector<CStr> Failed;
			auto Streams = pClient->f_GetStreams();
			
			for (auto &Stream : Streams)
			{
				try
				{
					CPerforceFunctions::fs_InitializeStream(Stream, Functions);
				}
				catch (CException const &_Error)
				{
					Failed.f_Insert(CStr::CFormat("{}: {}{\n}") << Stream << _Error.f_GetErrorStr());
				}
			}
			
			for (auto &Error : Failed)
				DConErrOutRaw(Error);
		}
		else
			CPerforceFunctions::fs_InitializeStream(StreamName, Functions);
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceInitializeStream);


class CTool_PerforceSwitchWorkspaceTaskStream : public CTool2
{
public:

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
		
		bool bSwitchOwned = f_GetOption(_Params, "SwitchOwned", "").f_Trim() == "true";
		
		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
		CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");
		
		DConOut("Stream: {}{\n}", StreamName);
		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = P4Client;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());
		
		CPerforceFunctions Functions(pClient);

		CPerforceFunctions::fs_SwitchStream(Functions, StreamName, true);
		
		if (bSwitchOwned)
		{
			auto Stream = Functions.f_GetStreamCached(StreamName);
		
			auto Registry = CPerforceFunctions::fs_GetRegistry(Stream);
			auto pOwned = Registry.f_GetChildNoPath("OwnedStreams");
			if (pOwned)
			{
				for (auto iOwned = pOwned->f_GetChildIterator(); iOwned; ++iOwned)
				{
					CPerforceFunctions::fs_SwitchStream(Functions, iOwned->f_GetThisValue(), true);
				}
			}
		}
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceSwitchWorkspaceTaskStream);


class CTool_PerforceCreateStreamBranches : public CTool2
{
public:

	static void fs_CreateStreamBranchForStreams(CPerforceFunctions &_Functions, CStr const &_BranchName, CStr const &_From, CStr const &_To)
	{
		auto fl_GetRelated
			= [&](CStr const &_Stream) -> TCVector<CStr>
			{
				TCSet<CStr> RelatedStreamsSet;
				TCVector<CStr> RelatedStreams;
				
				RelatedStreamsSet[_Stream];
				RelatedStreams.f_Insert(_Stream);
				
				CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(_Stream);
				
				for (auto &Import : Stream.m_Paths)
				{
					CStr Type;
					CStr Path;
					CStr DepotPath;
					(CStr::CParse("{} {} {}") >> Type >> Path >> DepotPath).f_Parse(Import);

					if (Type == "import" || Type == "import+")
					{
						CStr Stream = CPerforceFunctions::fs_GetStream(DepotPath);
						if (RelatedStreamsSet(Stream).f_WasCreated())
							RelatedStreams.f_Insert(Stream);
					}
				}
				return RelatedStreams;
			}
		;
		TCVector<CStr> ToStreams = fl_GetRelated(_To);
		
		TCMap<CStr, TCMap<uint32, CStr>> ParentMap;
		
		for (auto iStream = ToStreams.f_GetIterator(); iStream; ++iStream)
		{
			uint32 iParent = 0;
			
			CStr Parent = *iStream;
			while (!Parent.f_IsEmpty() && Parent != "none")
			{
				auto &Map = ParentMap[Parent][iParent];
				if (Map.f_IsEmpty())
					ParentMap[Parent][iParent] = *iStream;
				
				CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(Parent);
				Parent = Stream.m_Parent;
				++iParent;
			}
		}
		
		TCVector<CStr> FromStreams = fl_GetRelated(_From);
		
		TCMap<CStr, CStr> MappedStreams;
		for (auto &Stream : FromStreams)
		{
			CStr Parent = Stream;
			bool bFound = false;
			
			while (!Parent.f_IsEmpty() && Parent != "none")
			{
				auto pParents = ParentMap.f_FindEqual(Parent);
				
				if (pParents)
				{
					MappedStreams[Stream] = *pParents->f_FindSmallest();
					bFound = true;
					break;
				}
				
				CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(Parent);
				Parent = Stream.m_Parent;
			}
			
			if (!bFound)
				DConOut("Warning: No common parent stream found for {}{\n}", Stream);
			
		}
		
		CPerforceClient::CBranchSpec Branch;
		
		Branch.m_Owner = _Functions.f_GetClient().f_GetUser();
		
		CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(_From);

		auto fl_AddBranchMapping
			= [&](CStr const &_From, CStr const &_To)
			{
				if (_From == _To)
					return;
				auto &Mapping = Branch.m_View.f_Insert();
				
				Mapping.m_From = _From;
				Mapping.m_To = _To;

				DConOut("{} -> {}{\n}", Mapping.m_From << Mapping.m_To);
			}
		;
		
		auto DisableCreate = _Functions.f_GetDisabledCreate(_Functions.f_GetStreamCached(_From));
		
		TCMap<CStr, CStr> UsedMappings;
		
		for (auto &Import : Stream.m_Paths)
		{
			CStr Type;
			CStr Path;
			CStr DepotPath;
			(CStr::CParse("{} {} {}") >> Type >> Path >> DepotPath).f_Parse(Import);

			if (Type == "share")
			{
				auto pStream = MappedStreams.f_FindEqual(_From);
				if (pStream)
				{
					UsedMappings[_From] = *pStream;
					fl_AddBranchMapping(CFile::fs_AppendPath(_From, Path), CFile::fs_AppendPath(*pStream, Path));
				}
			}
			else if (Type == "import" || Type == "import+")
			{
				CStr FromStream = CPerforceFunctions::fs_GetStream(DepotPath);
				if (DisableCreate.f_FindEqual(FromStream))
					continue; // Don't consider these streams
				
				CStr FullPath = DepotPath.f_Extract(FromStream.f_GetLen() + 1);
				
				auto pStream = MappedStreams.f_FindEqual(FromStream);
				if (pStream)
				{
					UsedMappings[FromStream] = *pStream;
					fl_AddBranchMapping(CFile::fs_AppendPath(FromStream, FullPath), CFile::fs_AppendPath(*pStream, FullPath));
				}
			}
		}
		
		for (auto iStream = UsedMappings.f_GetIterator(); iStream; ++iStream)
		{
			auto BranchForStream = _Functions.f_GetClient().f_GetBranchForStreams(iStream.f_GetKey(), *iStream);
			for (auto &Mapping : BranchForStream.m_View)
			{
				if (Mapping.m_bNegative)
					Branch.m_View.f_Insert(Mapping);
			}
		}
		_Functions.f_GetClient().f_CreateBranch(_BranchName, Branch);
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
		CStr StreamRoot = f_GetOption(_Params, "Stream").f_Trim();
		
		if (StreamRoot.f_IsEmpty())
			DError("You have to specify a stream");
		
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
		
		CPerforceClient::CStream Stream = Functions.f_GetStreamCached(StreamRoot);
		CPerforceClient::CStream ParentStream = Functions.f_GetStreamCached(Stream.m_Parent);
		
		CStr FromParentName 
			= fg_Format
			(
				"//{}/{} -> //{}/{}"
				, CPerforceFunctions::fs_GetDepot(Stream.m_Parent)
				, ParentStream.m_Name
				, CPerforceFunctions::fs_GetDepot(StreamRoot)
				, Stream.m_Name
			)
		;
		
		CStr ToParentName 
			= fg_Format
			(
				"//{}/{} -> //{}/{}"
				, CPerforceFunctions::fs_GetDepot(StreamRoot)
				, Stream.m_Name
				, CPerforceFunctions::fs_GetDepot(Stream.m_Parent)
				, ParentStream.m_Name
			)
		;
		
		FromParentName = FromParentName.f_Replace(" ", "_").f_Replace("(", "").f_Replace(")", "");
		ToParentName = ToParentName.f_Replace(" ", "_").f_Replace("(", "").f_Replace(")", "");
		
		fs_CreateStreamBranchForStreams(Functions, FromParentName , Stream.m_Parent, StreamRoot);
		fs_CreateStreamBranchForStreams(Functions, ToParentName, StreamRoot, Stream.m_Parent);
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceCreateStreamBranches);

class CTool_PerforceCreatePatchesForStream : public CTool2
{
public:

	static void fs_CreatePatchesForStream(CPerforceFunctions &_Functions, CStr const &_From, CStr const &_To)
	{
		auto fl_GetRelated
			= [&](CStr const &_Stream) -> TCVector<CStr>
			{
				TCSet<CStr> RelatedStreamsSet;
				TCVector<CStr> RelatedStreams;
				
				RelatedStreamsSet[_Stream];
				RelatedStreams.f_Insert(_Stream);
				
				CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(_Stream);
				
				for (auto &Import : Stream.m_Paths)
				{
					CStr Type;
					CStr Path;
					CStr DepotPath;
					(CStr::CParse("{} {} {}") >> Type >> Path >> DepotPath).f_Parse(Import);

					if (Type == "import" || Type == "import+")
					{
						CStr Stream = CPerforceFunctions::fs_GetStream(DepotPath);
						if (RelatedStreamsSet(Stream).f_WasCreated())
							RelatedStreams.f_Insert(Stream);
					}
				}
				return RelatedStreams;
			}
		;
		TCVector<CStr> ToStreams = fl_GetRelated(_To);
		
		TCMap<CStr, TCMap<uint32, CStr>> ParentMap;
		
		for (auto iStream = ToStreams.f_GetIterator(); iStream; ++iStream)
		{
			uint32 iParent = 0;
			
			CStr Parent = *iStream;
			while (!Parent.f_IsEmpty() && Parent != "none")
			{
				auto &Map = ParentMap[Parent][iParent];
				if (Map.f_IsEmpty())
					ParentMap[Parent][iParent] = *iStream;
				
				CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(Parent);
				Parent = Stream.m_Parent;
				++iParent;
			}
		}
		
		TCVector<CStr> FromStreams = fl_GetRelated(_From);
		
		TCMap<CStr, CStr> MappedStreams;
		for (auto &Stream : FromStreams)
		{
			CStr Parent = Stream;
			bool bFound = false;
			
			while (!Parent.f_IsEmpty() && Parent != "none")
			{
				auto pParents = ParentMap.f_FindEqual(Parent);
				
				if (pParents)
				{
					MappedStreams[Stream] = *pParents->f_FindSmallest();
//					DConOut("{} -> {}\n", Stream << *pParents->f_FindSmallest());
					bFound = true;
					break;
				}
				
				CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(Parent);
				Parent = Stream.m_Parent;
			}
			
			if (!bFound)
				DConOut("Warning: No common parent stream found for {}{\n}", Stream);
			
		}
		
		auto DisableCreate = _Functions.f_GetDisabledCreate(_Functions.f_GetStreamCached(_From));
		
		CStr RootPath;
		try
		{
			TCVector<CStr> Files = _Functions.f_GetClient().f_ClientFiles(_Functions.f_GetClient().f_GetClientRoot() + "/....BranchRoot");
		
			if (Files.f_IsEmpty())
				DError("No brach root found");
			RootPath = Files[0];
			if (RootPath.f_Find("/.BranchRoot") >= 0)
				RootPath = CFile::fs_GetPath(RootPath);
		}
		catch (CException const &)
		{
			RootPath = _Functions.f_GetClient().f_GetClientRoot();
		}
		CStr PatchDir = CFile::fs_AppendPath(RootPath, "Patches");
		
		auto ConnectionInfo = _Functions.f_GetClient().f_GetConnectionInfo();
		
		ConnectionInfo.m_bDisableTagging = true;
		
		for (auto iStream = MappedStreams.f_GetIterator(); iStream; ++iStream)
		{
			if (iStream.f_GetKey() == *iStream)
				continue;
			if (DisableCreate.f_FindEqual(iStream.f_GetKey()))
			{
				DConOut("DisableCreate: {}\n", iStream.f_GetKey());
				continue;
			}
			
			CPerforceClient::CBranchSpec Branch;
			
			Branch.m_Owner = _Functions.f_GetClient().f_GetUser();
			
			CPerforceClient::CStream StreamFrom = _Functions.f_GetStreamCached(iStream.f_GetKey());
			CPerforceClient::CStream StreamTo = _Functions.f_GetStreamCached(*iStream);
			
			CStr PatchPrefix = _Functions.f_GetPatchPrefix(StreamFrom);

			auto &Mapping = Branch.m_View.f_Insert();
			
			Mapping.m_From = iStream.f_GetKey() + "/...";
			Mapping.m_To = *iStream + "/...";
			
			auto BranchForStream = _Functions.f_GetClient().f_GetBranchForStreams(iStream.f_GetKey(), *iStream);
			for (auto &Mapping : BranchForStream.m_View)
			{
				if (Mapping.m_bNegative)
					Branch.m_View.f_Insert(Mapping);
			}
			CStr BranchName = fg_Format("Temp_{}", NCryptography::fg_GetRandomUuidString());
			_Functions.f_GetClient().f_CreateBranch(BranchName, Branch);
			auto Cleanup
				= fg_OnScopeExit
				(	
					[&]
					{
						try
						{
							_Functions.f_GetClient().f_DeleteBranch(BranchName);
						}
						catch (NException::CException const &_Exception)
						{
							DConOut("Failed to delete branch: {}", _Exception.f_GetErrorStr());
						}
					}
				)
			;

			
			auto fl_GeneratePatch
				= [&](bool _bFullContext)
				{
					CStr Patch;
					if (!_Functions.f_GetClient().f_NoThrow().f_CreatePatch(BranchName, _bFullContext, Patch))
					{
						CStr Error = _Functions.f_GetClient().f_NoThrow().f_GetLastError();
						
						if (Error != "No source file(s) in branch view." && Error != "No file(s) to diff." && Error != "No differing files.")
							_Functions.f_GetClient().f_ThrowLastError();
					}
			
					if (!Patch.f_IsEmpty())
					{
						CStr FileName;
						if (_bFullContext)
							FileName = fg_Format("{}.{}.patch", CPerforceFunctions::fs_GetDepot(*iStream), StreamTo.m_Name).f_Replace("/", ".");
						else
							FileName = fg_Format("{}.{}.minimal.patch", CPerforceFunctions::fs_GetDepot(*iStream), StreamTo.m_Name).f_Replace("/", ".");
						
						CStr FullPath = CFile::fs_AppendPath(PatchDir, FileName);
						DConOut("{}{\n}", FullPath);
						
						Patch = Patch.f_Replace(iStream.f_GetKey() + "/", PatchPrefix);
						Patch = Patch.f_Replace(*iStream + "/", PatchPrefix);
					
						CFile::fs_CreateDirectory(CFile::fs_GetPath(FullPath));
						CFile::fs_WriteStringToFile(CStr(FullPath), Patch, false);
					}
				}
			;
			
			fl_GeneratePatch(false);
			fl_GeneratePatch(true);
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
		CStr StreamRoot = f_GetOption(_Params, "Stream").f_Trim();
		
		if (StreamRoot.f_IsEmpty())
			DError("You have to specify a stream");
		
		bool bForChildren = f_GetOption(_Params, "ForChildren", "") == "true";
		
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
		CStr DestinationWorkspace = StreamSwitcher.f_GetClientForStream(StreamRoot);
		
		ConnectionInfo.m_Client = DestinationWorkspace;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());
		
		if (bForChildren)
		{
			TCVector<CStr> Streams = pClient->f_FindStreams(fg_Format("Parent={}", StreamRoot));
			for (auto &Stream : Streams)
			{
				fs_CreatePatchesForStream(Functions, StreamRoot, Stream);
			}
		}
		else
		{
			CPerforceClient::CStream Stream = Functions.f_GetStreamCached(StreamRoot);
			fs_CreatePatchesForStream(Functions, Stream.m_Parent, StreamRoot);
		}
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceCreatePatchesForStream);


class CTool_PerforceSetBuildServer : public CTool2
{
public:

	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr DoneMessage = "Done!";
		auto ReportDone
			= g_OnScopeExit > [&]
			{
				DConOut("{}{\n}", DoneMessage);
			}
		;
		CStr StreamRoot = f_GetOption(_Params, "Stream").f_Trim();
		
		if (StreamRoot.f_IsEmpty())
			DError("You have to specify a stream");

		CStr Action = f_GetOption(_Params, "Action").f_Trim();
		if (Action != "Add" && Action != "Remove")
			DError("You have to specify Action=Add or Action=Remove");
		
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
		CStr DestinationWorkspace = StreamSwitcher.f_GetClientForStream(StreamRoot);
		
		ConnectionInfo.m_Client = DestinationWorkspace;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());
		
		if (Action == "Add")
		{
			CPerforceClient::CStream Stream;
			Stream = Functions.f_GetStreamCached(StreamRoot);
			for (CStr StreamIter = Stream.m_Parent; !StreamIter.f_IsEmpty() && StreamIter != "none"; StreamIter = Stream.m_Parent)
			{
				Stream = Functions.f_GetStreamCached(StreamIter);
				
				CStr FileName = fg_Format("{}/MLBranch", StreamIter);
				
				if (!pClient->f_FileExistsInDepot(FileName))
					continue;
				
				CPerforceClient::CFileStats Stats = pClient->f_FileStats(FileName);
				
				uint32 Revision = Stats.m_HeadRev;
				
				if (Stats.m_HeadAction == CPerforceClient::EAction_Delete)
					--Revision;
				
				CStr ReadFile = fg_Format("{}#0,{}", FileName, Revision);
				CStr DestinationFile = StreamRoot + "/MLBranch";
				
				TCVector<CPerforceClient::CIntegrationResult> IntegrateResult;
				TCVector<CPerforceClient::CMergeError> Errors;
				for (auto i = 0; i < 2; ++i)
				{
					TCVector<CStr> MustSync;
					IntegrateResult.f_Clear();
					Errors.f_Clear();
					if (!pClient->f_NoThrow().f_IntegrateFiles(ReadFile, DestinationFile, i == 0, true, IntegrateResult, MustSync, Errors))
					{
						if (pClient->f_NoThrow().f_GetLastError().f_Find("- all revision(s) already integrated.") >= 0)
						{
							if (!pClient->f_FileExistsInDepotNotDeleted(DestinationFile))
							{
								CPerforceClient::CFileStats Stats = pClient->f_FileStats(DestinationFile);
								
								CStr ToSync = fg_Format("{}#{}", DestinationFile, Stats.m_HeadRev - 1);
								pClient->f_Sync(ToSync, fg_Default(), true);
								pClient->f_Add(DestinationFile);
								//pClient->f_Sync(DestinationFile, fg_Default(), false);
								//pClient->f_ResolveMine(DestinationFile);
								pClient->f_Submit(DestinationFile, "[BuildSystem]\nFixed: Added to build server", fg_GetSys()->f_GetEnvironmentVariable("MTool_P4SubmitJob"));
								return 0;
							}
							else
								pClient->f_ThrowLastError();
						}
						else
							pClient->f_ThrowLastError();
					}
					if (!MustSync.f_IsEmpty())
						pClient->f_Sync(CStr(), fg_Default(), false, MustSync);
				}
				
				if (!Errors.f_IsEmpty())
				{
					for (auto &Error : Errors)
					{
						DConErrOut("{}: {}\n", Error.m_Path << Error.m_Error);
					}
				}
				if (!IntegrateResult.f_IsEmpty())
				{
					if (IntegrateResult[0].m_Action == CPerforceClient::EAction_Integrate)
						pClient->f_ResolveAutomatic(DestinationFile);
					
					if (!IntegrateResult.f_IsEmpty())
						pClient->f_Submit(DestinationFile, "[BuildSystem]\nFixed: Added to build server", fg_GetSys()->f_GetEnvironmentVariable("MTool_P4SubmitJob"));
					break;
				}
			}
		}
		else
		{
			CStr DestinationFile = StreamRoot + "/MLBranch";
			
			if (!pClient->f_FileExistsInDepotNotDeleted(DestinationFile))
			{
				DConOut("Stream is already disabled on build server", 0);
				return 0;
			}
			
			pClient->f_NoThrow().f_Sync(DestinationFile);
			pClient->f_Delete(DestinationFile);
			pClient->f_Submit(DestinationFile, "[BuildSystem]\nFixed: Removed from build server", fg_GetSys()->f_GetEnvironmentVariable("MTool_P4SubmitJob"));
		}
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceSetBuildServer);

