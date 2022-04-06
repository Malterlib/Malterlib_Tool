// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"


class CTool_PerforceSubmitInStream : public CTool2
{
public:
	static CStr fs_GetStream(CStr const &_DepotPath)
	{
		ch8 const *pParse = _DepotPath.f_GetStr();
		if (*pParse == '/')
			++pParse;
		else
			DError(fg_Format("Could not parse stream from: {}", _DepotPath));
		if (*pParse == '/')
			++pParse;
		else
			DError(fg_Format("Could not parse stream from: {}", _DepotPath));
		
		ch8 const *pStartDepot = pParse;
		
		while (*pParse && *pParse != '/')
			++pParse;
		
		CStr Depot = CStr(pStartDepot, pParse - pStartDepot);
		
		if (*pParse == '/')
			++pParse;
		else
			DError(fg_Format("Could not parse stream from: {}", _DepotPath));

		ch8 const *pStartStream = pParse;
		
		while (*pParse && *pParse != '/')
			++pParse;

		CStr Stream = CStr(pStartStream, pParse - pStartStream);
		
		return fg_Format("//{}/{}", Depot, Stream);
	}
	
	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr DoneMessage = "Done!";
		auto ReportDone
			= g_OnScopeExit / [&]
			{
				DConOut("{}{\n}", DoneMessage);
			}
		;
		
		CStr Workspace = f_GetOption(_Params, "Workspace").f_Trim();
		CStr File = f_GetOption(_Params, "File").f_Trim();
		
		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");

		DConOut("Workspace: {}{\n}", Workspace);
		DConOut("File: {}{\n}", File);
		
		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = Workspace;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());

		DConOut("Logged in to perforce{\n}", 0);
		
		CStr DepotPath = pClient->f_GetDepotPath(File, true);

		DConOut("Depot path: {}{\n}", DepotPath);
		
		CStr DestinationStream = fs_GetStream(DepotPath);

		DConOut("DestinationStream: {}{\n}", DestinationStream);
		
		TCVector<CPerforceClient::CChangeList> ChangeLists = pClient->f_GetChangelists(DepotPath, false, Workspace, "pending");
		
		int64 SourceChangelist = 0;
		CStr SourceWorkspace;

		CStr SourceComment;
		TCVector<CStr> SourceJobs;
		TCVector<CPerforceClient::CChangeList::CFile> SourceFiles;
		
		
		for (auto iChangeList = ChangeLists.f_GetIterator(); iChangeList; ++iChangeList)
		{
			CPerforceClient::CChangeList ChangeList;

			if (pClient->f_NoThrow().f_GetChangelist(iChangeList->m_ChangeID, ChangeList))
			{
				bool bHasFile = false;
				for (auto iFile = ChangeList.m_Files.f_GetIterator(); iFile; ++iFile)
				{
					if (iFile->m_Name == DepotPath)
					{
						bHasFile = true;
						break;
					}
				}
				if (bHasFile)
				{
					SourceChangelist = iChangeList->m_ChangeID;
					SourceWorkspace = ChangeList.m_Client;
					SourceComment = ChangeList.m_Description;
					SourceJobs = ChangeList.m_Jobs;
					SourceFiles = ChangeList.m_Files;
					break;
				}
			}
		}
		
		if (SourceChangelist == 0)
			DError(fg_Format("Could not determine changelist for file (cannot submit from default changelist): {}", DepotPath));
		
		DConOut("SourceChangelist: {}{\n}", SourceChangelist);
		DConOut("SourceWorkspace: {}{\n}", SourceWorkspace);
		
		CStr CurrentStream;
		CStr CurrentHost;
		pClient->f_GetClient
			(
				Workspace
				, [&](CStr const &_Key, CStr const &_Value)
				{
					if (_Key == "Stream")
						CurrentStream = _Value;
					else if (_Key == "Host")
						CurrentHost = _Value;
				}
			)
		;
		
		if (CurrentStream.f_IsEmpty())
			DError(fg_Format("Failed to get stream for workspace: {}", Workspace));
		
		DConOut("CurrentStream: {}{\n}", CurrentStream);
		DConOut("CurrentHost: {}{\n}", CurrentHost);

		if (CurrentStream == DestinationStream)
			DError(fg_Format("The file is in the same stream, this is not supported: {}", DestinationStream));
		
		CPerforceFunctions Functions(pClient);

		CPerforce_TemporaryStreamSwitcher StreamSwitcher(Functions);
		
		CStr DestinationWorkspace = StreamSwitcher.f_GetClientForStream(DestinationStream);

		DConOut("DestinationWorkspace: {}{\n}", DestinationWorkspace);

		TCVector<CStr> SourceFileInDestinationStream;
		for (auto iFile = SourceFiles.f_GetIterator(); iFile; ++iFile)
		{
			if (iFile->m_Name.f_StartsWith(DestinationStream))
				SourceFileInDestinationStream.f_Insert(iFile->m_Name);
		}

		uint32 DestinationChangelist = pClient->f_CreateChangelist(SourceComment, SourceJobs, fg_Default());

		DConOut("DestinationChangelist: {}{\n}", DestinationChangelist);
		
		pClient->f_MoveToChangelist(SourceFileInDestinationStream, DestinationChangelist);
		pClient->f_ShelveChangelist(DestinationChangelist, false, true, SourceFileInDestinationStream);
		pClient->f_MoveToChangelist(SourceFileInDestinationStream, SourceChangelist);
		
		TCUniquePointer<CPerforceClientThrow> pDestinationClient;
		CPerforceClient::CConnectionInfo DestinationConnectionInfo;
		DestinationConnectionInfo.m_Server = P4Port;
		DestinationConnectionInfo.m_User = P4User;
		DestinationConnectionInfo.m_Client = DestinationWorkspace;
		
		pDestinationClient = fg_Construct(DestinationConnectionInfo);
		pDestinationClient->f_Login(CStr());
		
		pDestinationClient->f_SetChangelistClient(DestinationChangelist, DestinationWorkspace);
		
		uint32 FinalChangelist = pDestinationClient->f_SubmitChangelist(DestinationChangelist, true);

		DConOut("Successfully submitted changelist, syncing and reverting submitted files{\n}", 0);

		TCSet<CStr> ShelvedFiles;

		{
			CPerforceClient::CDescription Description = pClient->f_DescribeShelved(SourceChangelist);
			
			for (auto iFile = Description.m_Files.f_GetIterator(); iFile; ++iFile)
				ShelvedFiles[iFile->m_Name];
		}
		
		TCSet<CStr> AddedFiles;
		TCSet<CStr> RemovedFiles;

		{
			CPerforceClient::CDescription ChangeList = pDestinationClient->f_Describe(FinalChangelist);
			for (auto iFile = ChangeList.m_Files.f_GetIterator(); iFile; ++iFile)
			{
				if (iFile->m_Action == CPerforceClient::EAction_Add)
					AddedFiles[iFile->m_Name];
				else if (iFile->m_Action == CPerforceClient::EAction_Delete)
					RemovedFiles[iFile->m_Name];
			}
		}
		
		
		for (auto iFile = SourceFileInDestinationStream.f_GetIterator(); iFile; ++iFile)
		{
			if (AddedFiles.f_FindEqual(*iFile))
			{
				pClient->f_Revert(*iFile, false);

				pClient->f_Sync(*iFile, fg_Default(), true);
			}
			else if (RemovedFiles.f_FindEqual(*iFile))
			{
				pClient->f_Revert(*iFile, false);
			}
			else
			{
				pClient->f_Sync(*iFile, fg_Default());

				pClient->f_ResolveSafe(*iFile);
				
				pClient->f_Revert(*iFile, true);
			}
			
			if (ShelvedFiles.f_FindEqual(*iFile))
				pClient->f_DeleteShelvedFile(SourceChangelist, *iFile);
		}


		CPerforceClient::CChangeList ChangeList = pClient->f_GetChangelist(SourceChangelist);
		CPerforceClient::CDescription Description = pClient->f_DescribeShelved(SourceChangelist);
		
		if (ChangeList.m_Files.f_IsEmpty() && Description.m_Files.f_IsEmpty())
		{
			DConOut("Changelist is now empty, deleting it{\n}", 0);
			if (!ChangeList.m_Jobs.f_IsEmpty())
			{
				pClient->f_RemoveJobsFromChangelist(SourceChangelist, ChangeList.m_Jobs);
			}
			
			pClient->f_DeleteChangelist(SourceChangelist);
		}
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceSubmitInStream);

