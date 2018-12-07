// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"
#include <Mib/Cryptography/UUID>


class CTool_PerforceForceDeleteChangelist : public CTool2
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
		CStr Changelist = f_GetOption(_Params, "Changelist").f_Trim();
		
		uint32 SourceChangelist = Changelist.f_ToInt(uint32(0));
		
		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
		CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");

		DConOut("Changelist: {}{\n}", Changelist);
		
		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = P4Client;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());

		DConOut("Logged in to perforce{\n}", 0);
		
		if (fg_Confirm("Are you sure you want to delete the changelist?{\n}"))
		{
			CPerforceClient::CChangeList ChangeList = pClient->f_GetChangelist(SourceChangelist);
			
			if (!ChangeList.m_Jobs.f_IsEmpty())
			{
				DConOut("Changelist has jobs attached to it, removing them{\n}", 0);
				pClient->f_RemoveJobsFromChangelist(SourceChangelist, ChangeList.m_Jobs);
			}

			CPerforceClient::CDescription Description = pClient->f_DescribeShelved(SourceChangelist);
			
			if (!Description.m_Files.f_IsEmpty())
			{
				DConOut("Changelist has shelved files, deleting them{\n}", 0);
				pClient->f_DeleteShelvedFile(SourceChangelist, CStr(), true);
			}
			
			if (!ChangeList.m_Files.f_IsEmpty())
			{
				DConOut("Changelist has checked out files, reverting them{\n}", 0);
				if (!pClient->f_NoThrow().f_RevertChangelist(SourceChangelist, true))
				{
					if (pClient->f_NoThrow().f_GetLastError() != "File(s) not opened for edit.")
						pClient->f_ThrowLastError();
				}
					
				CPerforceClient::CChangeList ChangeListNew = pClient->f_GetChangelist(SourceChangelist);
				
				if (!ChangeListNew.m_Files.f_IsEmpty())
				{
					if (fg_Confirm("There are changed checked out files in this changelist, are you really sure you want to revert them?"))
					{
						pClient->f_RevertChangelist(SourceChangelist, false);
					}
					else
					{
						DConOut("Aborted!{\n}", 0);
						return 0;
					}
				}
			}
			
			DConOut("Deleting changelist{\n}", 0);

			pClient->f_DeleteChangelist(SourceChangelist, true);
		}
		else
			DoneMessage = "Aborted!";
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceForceDeleteChangelist);

class CTool_PerforceTakeOwnership : public CTool2
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
		CStr ChangeList = f_GetOption(_Params, "ChangeList").f_Trim();
		
		if (ChangeList.f_IsEmpty())
			DError("You have to specify a changelist");
		
		int32 ChangeListNumber = ChangeList.f_ToInt(int32(0));
		if (ChangeListNumber <= 0)
			DError(fg_Format("Invalid change list number: {}", ChangeListNumber));
		
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

		pClient->f_SetChangelistOwner(ChangeListNumber, P4User);
		pClient->f_SetChangelistClient(ChangeListNumber, P4Client);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceTakeOwnership);

class CTool_PerforceUnshelveRelatedStream : public CTool2
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
		
		CStr ChangeList = f_GetOption(_Params, "ChangeList").f_Trim();
		
		if (ChangeList.f_IsEmpty())
			DError("You have to specify a changelist");
		
		int32 ChangeListNumber = ChangeList.f_ToInt(int32(0));
		if (ChangeListNumber <= 0)
			DError(fg_Format("Invalid change list number: {}", ChangeListNumber));
		
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
		
		CStr CurrentStream = Functions.f_GetCurrentStream();
		
		TCVector<CStr> RelatedStreams;
		TCSet<CStr> RelatedStreamsSet;
		
		RelatedStreams.f_Insert(CurrentStream);
		RelatedStreamsSet[CurrentStream];
		
		CPerforceClient::CStream Stream = Functions.f_GetStreamCached(CurrentStream);
		
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
		
		TCMap<CStr, TCMap<uint32, CStr>> ParentMap;
		
		for (auto iStream = RelatedStreams.f_GetIterator(); iStream; ++iStream)
		{
			uint32 iParent = 0;
			
			CStr Parent = *iStream;
			while (!Parent.f_IsEmpty() && Parent != "none")
			{
				auto &Map = ParentMap[Parent][iParent];
				if (Map.f_IsEmpty())
					ParentMap[Parent][iParent] = *iStream;
				CPerforceClient::CStream Stream = Functions.f_GetStreamCached(Parent);
				Parent = Stream.m_Parent;
				++iParent;
			}
			
		}
		
		TCSet<CStr> DescribedStreams;
		auto Description = pClient->f_DescribeShelved(ChangeListNumber);
		auto ChangeListInfo = pClient->f_GetChangelist(ChangeListNumber);
		
		for (auto &File : Description.m_Files)
			DescribedStreams[CPerforceFunctions::fs_GetStream(File.m_Name)];

		TCMap<CStr, CStr> MappedStreams;
		for (auto &Stream : DescribedStreams)
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
				
				CPerforceClient::CStream Stream = Functions.f_GetStreamCached(Parent);
				Parent = Stream.m_Parent;
			}
			
			if (!bFound)
				DConOut("Warning: No common parent stream found for {}{\n}", Stream);
			
		}
		
		CPerforceClient::CBranchSpec Branch;
		
		Branch.m_Owner = pClient->f_GetUser();
		
		
		for (auto iStream = MappedStreams.f_GetIterator(); iStream; ++iStream)
		{
			auto &Mapping = Branch.m_View.f_Insert();
			
			Mapping.m_From = iStream.f_GetKey() + "/...";
			Mapping.m_To = *iStream + "/...";	

			DConOut("{} -> {}{\n}", Mapping.m_From << Mapping.m_To);
		}
		
		CStr MappingName = fg_Format("Temp_{}", NCryptography::fg_GetRandomUuidString());
		
		pClient->f_CreateBranch(MappingName, Branch);
		auto Cleanup
			= fg_OnScopeExit
			(	
				[&]
				{
					try
					{
						pClient->f_DeleteBranch(MappingName);
					}
					catch (NException::CException const &_Exception)
					{
						DConOut("Failed to delete branch: {}", _Exception.f_GetErrorStr());
					}
				}
			)
		;

		uint32 DestinationChangelist = pClient->f_CreateChangelist(ChangeListInfo.m_Description, Description.m_Jobs, fg_Default());
		
		pClient->f_UnshelveWithBranch(ChangeListNumber, MappingName, DestinationChangelist);
		
		pClient->f_ResolveAutomatic(CStr(), DestinationChangelist);

		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceUnshelveRelatedStream);

class CTool_PerforceConvertChangelistsToGit : public CTool2
{
public:

	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr SearchPath = f_GetOption(_Params, "SearchPath", "").f_Trim();
		
		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());
		
		CPerforceFunctions Functions(pClient);
		
		TCVector<CPerforceClient::CChangeList> Changelists = pClient->f_GetChangelists(SearchPath, 0);
		
		uint64 nChanges = f_GetOption(_Params, "MaxChanges", CStr::fs_ToStr(TCLimitsInt<uint64>::mc_Max)).f_Trim().f_ToInt(uint64(TCLimitsInt<uint64>::mc_Max));
		
		for (auto const &Changelist: Changelists)
		{
			if (--nChanges == 0)
				break;
			auto const *pParse = Changelist.m_Description.f_GetStr();
			fg_ParseWhiteSpace(pParse);

			CStr Project;
			CStr FirstLine;
			CStr CommitLine;
			CStr ExtendedMessage;
			
			if (Changelist.m_Description.f_Find(" (integrated)") >= 0 && Changelist.m_Description.f_Find("[Integrate] ") < 0)
			{
				CommitLine = "[Integrate] Unknown -> Unknown";
			}
			else
			{
				if (*pParse == '[')
				{
					++pParse;
					auto pStart = pParse;
					auto pEndOfLine = pParse;
					fg_ParseToEndOfLine(pEndOfLine);
					while (*pParse && pParse != pEndOfLine && *pParse != ']')
						++pParse;
					
					Project = CStr(pStart, pParse - pStart);
					
					if (*pParse == ']')
						++pParse;
				}
				
				fg_ParseWhiteSpace(pParse);
				{
					auto pStart = pParse;
					fg_ParseToEndOfLine(pParse);
					FirstLine = CStr(pStart, pParse - pStart);
				}
				
				if (Project.f_IsEmpty())
					CommitLine = FirstLine;
				else
					CommitLine = fg_Format("[{}] {}", Project, FirstLine);
			}
			
			fg_ParseWhiteSpace(pParse);
			while (*pParse)
			{
				auto pStart = pParse;
				fg_ParseToEndOfLine(pParse);
				fg_AddStrSep(ExtendedMessage, CStr(pStart, pParse - pStart), DMibNewLine);
				fg_ParseEndOfLine(pParse);
			}
			
			CStr CommitMessage = CommitLine;
			
			if (!ExtendedMessage.f_IsEmpty())
			{
				CommitMessage += CStr::CFormat("{\n}{\n}");
				CommitMessage += ExtendedMessage;
			}
			
			CommitMessage += CStr::CFormat("{\n}");
			
			if (CommitMessage != Changelist.m_Description)
			{
				//NSys::fg_Debug_DiffStrings(CommitMessage, Changelist.m_Description);
				DConOut("----------------------------------------------{\n}{}", CommitMessage);
				pClient->f_SetChangelistDescription(Changelist.m_ChangeID, CommitMessage, true);
			}
			
		}
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceConvertChangelistsToGit);
