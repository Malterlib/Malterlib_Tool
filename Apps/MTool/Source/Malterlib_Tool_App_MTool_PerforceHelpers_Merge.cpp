// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "PCH.h"
#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Perforce/Wrapper>
#include <Mib/Perforce/Functions>
#include "Malterlib_Tool_App_MTool_PerforceHelpers.h"
#include <Mib/Concurrency/ParallellForEach>




class CTool_PerforceMerge : public CTool2
{
public:
	
	struct CIntegrationPair
	{
		CStr m_From;
		CStr m_To;
		zbool m_bCopy;
		zbool m_bTask;
	};
	
	void fr_GetIntegrationPairs(CPerforceFunctions &_Functions, CStr const &_From, CStr const &_To, TCVector<CIntegrationPair> &_oPairs, bool _bParentChildren, bool _bToParent)
	{
		if (_bToParent)
		{
			CPerforceFunctions::COwnedStreams OwnedFrom = _Functions.f_GetStreamOwned(_From, true);
			for (auto iStream = OwnedFrom.m_Streams.f_GetIterator(); iStream; ++iStream)
			{
				auto &Pair = _oPairs.f_Insert();
				Pair.m_From = fg_Get<0>(*iStream);
				Pair.m_To = fg_Get<1>(*iStream);
			}
		}
		else
		{
			CPerforceFunctions::COwnedStreams OwnedFromRaw = _Functions.f_GetStreamOwned(_From, false);
			CPerforceFunctions::COwnedStreams OwnedToRaw = _Functions.f_GetStreamOwned(_To, false);
			
			TCMap<CStr, CStr> OwnedFrom;
			TCMap<CStr, CStr> OwnedTo;
			
			for (auto iStream = OwnedFromRaw.m_Streams.f_GetIterator(); iStream; ++iStream)
			{
				CStr Parent = _Functions.f_GetStreamRootParent(fg_Get<1>(*iStream));
				if (Parent.f_IsEmpty())
					Parent = fg_Get<1>(*iStream);
				OwnedFrom[Parent] = fg_Get<1>(*iStream);
			}
			for (auto iStream = OwnedToRaw.m_Streams.f_GetIterator(); iStream; ++iStream)
			{
				OwnedTo[_Functions.f_GetStreamRootParent(fg_Get<1>(*iStream))] = fg_Get<1>(*iStream);
			}
			
			for (auto iStream = OwnedFrom.f_GetIterator(); iStream; ++iStream)
			{
				auto pTo = OwnedTo.f_FindEqual(iStream.f_GetKey());
				if (pTo)
				{
					auto &Pair = _oPairs.f_Insert();
					Pair.m_From = *iStream;
					Pair.m_To = *pTo;
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
		
		CStr StreamName = f_GetOption(_Params, "Stream", "").f_Trim();
		CStr ChangeList = f_GetOption(_Params, "ChangeList", "").f_Trim();
		CStr Direction = f_GetOption(_Params, "Direction").f_Trim();
		CStr Child = f_GetOption(_Params, "Child", "").f_Trim();
		
		if (StreamName.f_IsEmpty() && ChangeList.f_IsEmpty())
			DError("You have to specify Stream or ChangeList");
		
		if (Direction != "Parent" && Direction != "Child" && Direction != "Sibling")
			DError("Direction has to be either 'Parent', 'Child' or 'Sibling'");

		DConOut("StreamName={}\n", StreamName);
		DConOut("ChangeList={}\n", ChangeList);
		DConOut("Direction={}\n", Direction);
		
		CStr P4Port = fg_GetSys()->f_GetEnvironmentVariable("P4PORT");
		CStr P4User = fg_GetSys()->f_GetEnvironmentVariable("P4USER");
		CStr P4Client = fg_GetSys()->f_GetEnvironmentVariable("P4CLIENT");

		DConOut("P4PORT={}\n", P4Port);
		DConOut("P4USER={}\n", P4User);
		DConOut("P4CLIENT={}\n", P4Client);
		
		CBlockingStdInReader StdInReader;
		
		TCUniquePointer<CPerforceClientThrow> pClient;
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = P4Client;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());
		
		CPerforceFunctions Functions(pClient);

		CPerforceClient::CDescription ChangeListInfo;
		if (StreamName.f_IsEmpty())
		{
			ChangeListInfo = pClient->f_Describe(ChangeList.f_ToInt(uint32(0)));
			
			if (ChangeListInfo.m_Files.f_IsEmpty())
				DError(fg_Format("No files found in changelist {}", ChangeList));
			
			StreamName = CPerforceFunctions::fs_GetStream(ChangeListInfo.m_Files.f_GetFirst().m_Name);
		}

		DConOut("{\n}", 0);
		
		//DConOut("StreamName: {}{\n}", StreamName);
		//DConOut("ChangeList: {}{\n}", ChangeList);
		//DConOut("Direction: {}{\n}", Direction);
		
		CPerforceClient::CStream Stream = Functions.f_GetStreamCached(StreamName);
		CRegistryPreserveAndOrder_CStr Registry = Functions.fs_GetRegistry(Stream);
		
		TCSet<CStr> ExtraChildren;
		
		for (auto iChild = Registry.f_GetChildIterator("ExtraChild"); iChild && iChild->f_GetName() == "ExtraChild"; ++iChild)
		{
			ExtraChildren[iChild->f_GetThisValue()];
		}
		
		CStr IntegrateFromStream = StreamName;
		TCVector<CStr> IntegrateToStreams;
		
		zbool bParentChildren;
		zbool bToParent;
		zbool bExtraChildChoosen;
		
		if (Direction == "Parent")
		{
			bToParent = true;
			IntegrateToStreams.f_Insert(Stream.m_Parent);
			if (Stream.m_Parent.f_IsEmpty() || Stream.m_Parent == "none")
			{
				DError(fg_Format("Stream {} has no parent", StreamName));
			}

			if (Stream.m_Options.f_Contains("toparent") < 0)
				DError(fg_Format("Integration to parent stream not allowed", StreamName));
		}
		else
		{
			// Find all task streams
			TCVector<CStr> Streams;
			if (Direction == "Sibling")
			{
				Streams = pClient->f_FindStreams(fg_Format("Parent={}", Stream.m_Parent));
				if (Streams.f_IsEmpty())
					DError(fg_Format("Could not find any siblings for stream '{}'", StreamName));
			}
			else
			{
				Streams = pClient->f_FindStreams(fg_Format("Parent={}", StreamName));
				for (auto &Stream : ExtraChildren)
					Streams.f_Insert(Stream);
				
				// No children found, go back one step and find children there instead
				if (Streams.f_IsEmpty())
				{
					if (!Stream.m_Parent.f_IsEmpty() && Stream.m_Parent != "none")
					{
						bParentChildren = true;
						Streams = pClient->f_FindStreams(fg_Format("Parent={} & ^(Type=task)", Stream.m_Parent));
						
						for (mint i = 0; i < Streams.f_GetLen();)
						{
							if (Streams[i] == StreamName)
								Streams.f_Remove(i);
							else
								++i;
						}
						
						if (Streams.f_IsEmpty())
							DError(fg_Format("Could not find any children for stream '{}' or '{}'", StreamName, Stream.m_Parent));
					}
					else
						DError(fg_Format("Could not find any children for stream '{}'", StreamName));
				}
			}

			for (mint i = 0; i < Streams.f_GetLen();)
			{
				auto &StreamName = Streams[i];
				CPerforceClient::CStream Stream = Functions.f_GetStreamCached(StreamName);
				
				if (Stream.m_Options.f_Contains("fromparent") < 0)
					Streams.f_Remove(i);
				else
					++i;
			}
			
			if (Streams.f_IsEmpty())
				DError("No streams allows integration from parent stream");

			if (Streams.f_GetLen() == 1)
			{
				IntegrateToStreams.f_Insert(Streams.f_GetFirst());
			}
			else if (Streams.f_GetLen() > 1)
			{
				DConOut("Children: {\n}", 0);
				aint Number = 0;
				for (auto iStream = Streams.f_GetIterator(); iStream; ++iStream, ++Number)
				{
					CPerforceClient::CStream Stream = Functions.f_GetStreamCached(*iStream);
					DConOut("\t{}) //{}/{}{\n}", Number << CPerforceFunctions::fs_GetDepot(*iStream) << Stream.m_Name);
				}

				DConOut("\ta) All{\n}", 0);
				
				CStr Result;
				if (Child.f_IsEmpty())
					Result = fg_AskUser(StdInReader, "Which child stream do you want to merge into?{\n}");
				else
					Result = Child;
				
				if (Result == "a" || Result == "A")
				{
					IntegrateToStreams.f_Insert(Streams);
				}
				else
				{
					aint ChoosenNumber = Result.f_ToInt(aint(-1));
					
					if (Streams.f_IsPosValid(ChoosenNumber))
					{
						if (ExtraChildren.f_FindEqual(Streams[ChoosenNumber]))
							bExtraChildChoosen = true;
						IntegrateToStreams.f_Insert(Streams[ChoosenNumber]);
					}
					else
						DError("You didn't choose a valid child stream");
				}
			}
		}
		
		TCVector<CIntegrationPair> IntegrationPairs;

		for (auto &ToStream : IntegrateToStreams)
		{
			//DConOut("IntegrateFromStream: {}{\n}", IntegrateFromStream);
			//DConOut("IntegrateToStream: {}{\n}", IntegrateToStream);

			auto &Pair = IntegrationPairs.f_Insert();
			Pair.m_From = IntegrateFromStream;
			Pair.m_To = ToStream;
			
			if (ChangeList.f_IsEmpty()) // Changelist can only integrate in one stream at a time
				fr_GetIntegrationPairs(Functions, IntegrateFromStream, ToStream, IntegrationPairs, bParentChildren, bToParent);
		}

		for (auto iPair = IntegrationPairs.f_GetIterator(); iPair; ++iPair)
		{
			auto &Pair = *iPair;
			
			CPerforceClient::CStream FromStream = Functions.f_GetStreamCached(Pair.m_From);
			CPerforceClient::CStream ToStream = Functions.f_GetStreamCached(Pair.m_To);
			
			if (FromStream.m_Type == "task" && FromStream.m_Parent != Pair.m_To)
				Pair.m_bTask = true;
			
			if (ChangeList.f_IsEmpty() && !bExtraChildChoosen)
			{
				if (Pair.m_From == ToStream.m_Parent)
				{
					// Parent -> child
					if (ToStream.m_Type == "release")
						Pair.m_bCopy = true;						
				}
				else if (Pair.m_To == FromStream.m_Parent)
				{
					// Child -> parent
					if (FromStream.m_Type != "release")
						Pair.m_bCopy = true;
				}
			}
		}
		
		IntegrationPairs = IntegrationPairs.f_Reverse();

		CPerforce_TemporaryStreamSwitcher StreamSwitcher(Functions);
		
		auto HeadChangelist = Functions.f_GetClient().f_GetHeadChangelist(CStr());
		
		struct CIntegrationResult
		{
			CStr m_FullCommentList;
			TCMap<CPerforceClient::EAction, zuint32> m_Actions;
		};
		
		TCMap<CStr, CIntegrationResult> PairResults;
		
		mint nThreads = 16;
		g_ThreadPool.f_Construct(nThreads);
		
		auto fl_Integrate
			= [&](bool _bPretend, bool _bNoSubmit) -> bool
			{
				bool bDoSomething = false;
				TCVector<CIntegrationPair> NewIntegrationPairs;
				for (auto iPair = IntegrationPairs.f_GetIterator(); iPair; ++iPair)
				{
					auto &Pair = *iPair;
					
					auto &PairResult = PairResults[Pair.m_From + "->" + Pair.m_To];

					auto &FullCommentList = PairResult.m_FullCommentList;
					auto &Actions = PairResult.m_Actions;
					
					CStr DestinationWorkspace = StreamSwitcher.f_GetClientForStream(Pair.m_To);

					// Login with new workspace
					
					TCVector<CStr> Opened = pClient->f_GetOpened(CStr(), DestinationWorkspace);
					
					if (!Opened.f_IsEmpty())
					{
						DConOut("Opened files in {}:{\n}", DestinationWorkspace);
						for (auto iOpened = Opened.f_GetIterator(); iOpened; ++iOpened)
							DConOut("{}{\n}", *iOpened);
						DError("Cannot merge when there are opened files in destination workspace");
					}						
					
					TCUniquePointer<CPerforceClientThrow> pClient;
					CPerforceClient::CConnectionInfo ConnectionInfo;
					ConnectionInfo.m_Server = P4Port;
					ConnectionInfo.m_User = P4User;
					ConnectionInfo.m_Client = DestinationWorkspace;
					pClient = fg_Construct(ConnectionInfo);
					pClient->f_Login(CStr());

					TCThreadSafeQueue<TCUniquePointer<CPerforceClientThrow>> ClientCache;
					for (mint i = 0; i < nThreads; ++i)
					{
						TCUniquePointer<CPerforceClientThrow> pClient;
						pClient = fg_Construct(ConnectionInfo);
						pClient->f_Login(CStr());
						ClientCache.f_Push(fg_Move(pClient));
					}
					
					bool bAlreadyIntegrated = true;
					int64 SplitChangelist = -1;
					CStr FileSpec = fg_Format("@{}", HeadChangelist);
					
					TCSet<CPerforceClient::CIntegrationResult> UniqueResults;

					CPerforceClient::CStream FromStream = Functions.f_GetStreamCached(Pair.m_From);

					CPerforceClient::CStream ToStream = Functions.f_GetStreamCached(Pair.m_To);
					auto fl_RealError
						= [&](CStr const &_Error, CStr& _oRetryChangelist)
						{
							if (_Error.f_Find("- resolve move to") >= 0 && _Error.f_Find("before integrating from"))
								return false;
							if (_Error.f_StartsWith("Some files couldn't be opened for move.") && _oRetryChangelist.f_IsEmpty())
							{
								aint nParsed = 0;
								int64 RetryChangelist = 0;
								(CStr::CParse("Some files couldn't be opened for move.  Try copying from @{} instead") >> RetryChangelist).f_Parse(_Error, nParsed);
								if (nParsed == 1)
								{
									if (SplitChangelist < 0 || RetryChangelist < SplitChangelist)
										SplitChangelist = RetryChangelist;
									_oRetryChangelist = fg_Format("@{}", RetryChangelist);
									return false;
								}										
							}
							
							if (_Error.f_StartsWith("Can't copy to target path with files already open"))
								return false;
							if (_Error.f_StartsWith("All revision(s) already integrated"))
								return false;
							if (_Error.f_StartsWith("File(s) up-to-date."))
								return false;
							if (_Error.f_StartsWith("No source file(s) in"))
								return false;
							if (_Error.f_StartsWith("No such file(s)."))
								return false;
							if (_Error.f_Find("- all revision(s) already integrated.") >= 0)
								return false;
							if (_Error.f_Find("- no such file(s)") >= 0)
								return false;
							return true;
						}
					;
					
					for (int i = 0; i < 2; ++i)
					{
						bool bPretend = i == 0 || _bPretend;
						TCVector<CStr> MustSync;
						TCVector<CPerforceClient::CIntegrationResult> Result;
						TCVector<CPerforceClient::CMergeError> Errors;
						bool bRetried = false;
						if (Pair.m_bCopy)
						{
							if (Pair.m_To == FromStream.m_Parent)
							{
								while (!pClient->f_NoThrow().f_CopyStreamToParent(Pair.m_From, bPretend, Result, MustSync, Errors, FileSpec))
								{
									CStr Error = pClient->f_NoThrow().f_GetLastError();
									
									CStr RetryChangelist;
									if (fl_RealError(Error, RetryChangelist))
										pClient->f_ThrowLastError();
									
									if (RetryChangelist.f_IsEmpty() || bRetried)
										break;
									
									if (!RetryChangelist.f_IsEmpty())
										FileSpec = RetryChangelist;
									bRetried = true;
								}
							}
							else if (Pair.m_From == ToStream.m_Parent)
							{
								while (!pClient->f_NoThrow().f_CopyStreamFromParent(Pair.m_To, bPretend, Result, MustSync, Errors, FileSpec))
								{
									CStr Error = pClient->f_NoThrow().f_GetLastError();
									CStr RetryChangelist;
									if (fl_RealError(Error, RetryChangelist))
										pClient->f_ThrowLastError();
									if (RetryChangelist.f_IsEmpty() || bRetried)
										break;
									if (!RetryChangelist.f_IsEmpty())
										FileSpec = RetryChangelist;
									bRetried = true;
								}
							}
							else 
							{
								while (!pClient->f_NoThrow().f_CopyStream(Pair.m_From, Pair.m_To, bPretend, Result, MustSync, Errors, FileSpec))
								{
									CStr Error = pClient->f_NoThrow().f_GetLastError();
									CStr RetryChangelist;
									if (fl_RealError(Error, RetryChangelist))
										pClient->f_ThrowLastError();
									if (RetryChangelist.f_IsEmpty() || bRetried)
										break;
									if (!RetryChangelist.f_IsEmpty())
										FileSpec = RetryChangelist;
									bRetried = true;
								}
							}
						}
						else if (Pair.m_bTask || !ChangeList.f_IsEmpty())
						{
							CStr IntegrateFrom = Pair.m_From + "/...";
							if (!ChangeList.f_IsEmpty())
								IntegrateFrom += CStr::CFormat("@{0},{0}") << ChangeList;
							else
								IntegrateFrom += FileSpec;
							CStr IntegrateTo = Pair.m_To + "/...";
							while (!pClient->f_NoThrow().f_IntegrateFiles(IntegrateFrom, IntegrateTo, bPretend, false, Result, MustSync, Errors))
							{
								CStr Error = pClient->f_NoThrow().f_GetLastError();
								CStr RetryChangelist;
								if (fl_RealError(Error, RetryChangelist))
									pClient->f_ThrowLastError();
								
								if (RetryChangelist.f_IsEmpty() || !ChangeList.f_IsEmpty() || bRetried)
									break;
								if (!RetryChangelist.f_IsEmpty())
									FileSpec = RetryChangelist;
								bRetried = true;
								IntegrateFrom = Pair.m_From + "/..." + FileSpec;
							}
						}
						else
						{
							if (Pair.m_To == FromStream.m_Parent)
							{
								while (!pClient->f_NoThrow().f_MergeStreamToParent(Pair.m_From, bPretend, Result, MustSync, Errors, FileSpec))
								{
									CStr Error = pClient->f_NoThrow().f_GetLastError();
									CStr RetryChangelist;
									if (fl_RealError(Error, RetryChangelist))
										pClient->f_ThrowLastError();
									if (RetryChangelist.f_IsEmpty() || bRetried)
										break;
									if (!RetryChangelist.f_IsEmpty())
										FileSpec = RetryChangelist;
									bRetried = true;
								}
							}
							else if (Pair.m_From == ToStream.m_Parent)
							{
								while (!pClient->f_NoThrow().f_MergeStreamFromParent(Pair.m_To, bPretend, Result, MustSync, Errors, FileSpec))
								{
									CStr Error = pClient->f_NoThrow().f_GetLastError();
									CStr RetryChangelist;
									if (fl_RealError(Error, RetryChangelist))
										pClient->f_ThrowLastError();
									if (RetryChangelist.f_IsEmpty() || bRetried)
										break;
									if (!RetryChangelist.f_IsEmpty())
										FileSpec = RetryChangelist;
									bRetried = true;
								}
							}
							else 
							{
								while (!pClient->f_NoThrow().f_MergeStream(Pair.m_From, Pair.m_To, bPretend, Result, MustSync, Errors, FileSpec))
								{
									CStr Error = pClient->f_NoThrow().f_GetLastError();
									CStr RetryChangelist;
									if (fl_RealError(Error, RetryChangelist))
										pClient->f_ThrowLastError();
									if (RetryChangelist.f_IsEmpty() || bRetried)
										break;
									if (!RetryChangelist.f_IsEmpty())
										FileSpec = RetryChangelist;
									bRetried = true;
								}
							}
						}

						if (!Result.f_IsEmpty())
							bAlreadyIntegrated = false;

						if (i == 0)
						{
							for (auto iResult = Result.f_GetIterator(); iResult; ++iResult)
							{
								switch (iResult->m_Action)
								{
								case CPerforceClient::EAction_Edit:
								case CPerforceClient::EAction_Integrate:
									MustSync.f_Insert(iResult->m_To);
									break;
								}
							}

							for (auto &Error : Errors)
							{
								DConErrOut("{}: {}", Error.m_Path << Error.m_Error);
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
						for (auto iResult = Result.f_GetIterator(); iResult; ++iResult)
							UniqueResults[*iResult];
					}
					if (!bAlreadyIntegrated)
					{
						NewIntegrationPairs.f_Insert(Pair);
						DConOut("{}{\n}", (Pair.m_bCopy ? "Copy" : "Merge"));
						//DConOut("\tFrom\t\t{}{\n}", Pair.m_From);
						DConOut("\tFrom\t\t//{}/{}{\n}", CPerforceFunctions::fs_GetDepot(Pair.m_From) << FromStream.m_Name);
						//DConOut("\tTo\t\t\t{}{\n}", Pair.m_To);
						DConOut("\tTo\t\t\t//{}/{}{\n}", CPerforceFunctions::fs_GetDepot(Pair.m_To) << ToStream.m_Name);
						
						if (Actions.f_IsEmpty())
						{
							CStr FullCommentListDisplay;
							FullCommentList = fg_Format
								(
									"[Integrate] //{}/{} -> //{}/{}{\n}{\n}"
									, CPerforceFunctions::fs_GetDepot(Pair.m_From)
									, FromStream.m_Name
									, CPerforceFunctions::fs_GetDepot(Pair.m_To)
									, ToStream.m_Name
								)
							;
							
							CMutual Lock;
							TCSet<int32> ChangeLists;
							TCVector<CStr> FileRevsToCheck;
							TCMap<CStr, CStr> IntegrateTo;
							mint nIntegrations = 0;
							for (auto iResult = UniqueResults.f_GetIterator(); iResult; ++iResult)
							{
								nIntegrations += iResult->m_EndFromRev - iResult->m_StartFromRev;
							}
							
							TCAtomic<mint> nDoneIntegrations;
							
							CClock Clock;
							Clock.f_Start();
							TCAtomic<fp32> LastDisplay(fp32(Clock.f_GetTime()));
							
							struct CToProcess
							{
								CStr m_From;
								CStr m_To;
							};
							
							TCVector<CToProcess> ToProcess;

							for (auto iResult = UniqueResults.f_GetIterator(); iResult; ++iResult)
							{
								for (uint32 iRev = iResult->m_StartFromRev; iRev < iResult->m_EndFromRev; ++iRev)
								{
									auto &New = ToProcess.f_Insert();
									New.m_From = CStr::CFormat("{0}#{1},{1}") << iResult->m_From << (iRev + 1);
									New.m_To = iResult->m_To;
									
								}
							}
							
							fg_ParallellForEach
								(
									ToProcess
									, [&ClientCache, &nIntegrations, &nDoneIntegrations, &LastDisplay, &Clock, &Lock, &Actions, &ChangeLists, &ConnectionInfo](CToProcess const &_ToProcess)
									{
										auto &IntegrateFrom = _ToProcess.m_From;
										auto &IntegrateTo = _ToProcess.m_To;
										
										auto ExistingClient = ClientCache.f_Pop();
										TCUniquePointer<CPerforceClientThrow> pClient;
										if (ExistingClient)
											pClient = fg_Move(*ExistingClient);
										else
										{
											pClient = fg_Construct(ConnectionInfo);
											pClient->f_Login(CStr());
										}
										auto Cleanup = fg_OnScopeExit
											(
												[&]()
												{
													ClientCache.f_Push(fg_Move(pClient));
												}
											)
										;

										TCVector<CPerforceClient::CIntegrationResult> Integrated;
										TCVector<CStr> MustSync;
										TCVector<CPerforceClient::CMergeError> Errors;
										if (pClient->f_NoThrow().f_IntegrateFiles(IntegrateFrom, IntegrateTo, true, false, Integrated, MustSync, Errors))
										{
											if (!Integrated.f_IsEmpty())
											{
												CPerforceClient::CFileRevisions Revisions = pClient->f_GetFileRevisions(IntegrateFrom);
												for (auto &File : Revisions.m_Files)
												{
													for (auto &Revision : File.m_Revisions)
													{
														{
															DLock(Lock);
															ChangeLists[Revision.m_ChangeList];
															for (auto iIntegrated = Integrated.f_GetIterator(); iIntegrated; ++iIntegrated)
																++Actions[iIntegrated->m_Action];
														}
													}
												}
											}
											//else
											//	DConOut("\t\t{} - EXCLUDED\n", IntegrateFrom);
										}
										//else
										//	DConOut("\t\t{} - EXCLUDED - {}\n", IntegrateFrom << pClient->f_NoThrow().f_GetLastError().f_Replace(IntegrateFrom, ""));
										
										++nDoneIntegrations;
										fp32 LastDisplayValue = LastDisplay.f_Load();
										fp32 CurrentTime = Clock.f_GetTime();
										if (CurrentTime - LastDisplayValue > 2.0)
										{
											if (LastDisplay.f_CompareExchangeStrong(LastDisplayValue, CurrentTime))
											{
												DConOut("\t\t{fe2} %\n", (fp64(nDoneIntegrations.f_Load()) / fp64(nIntegrations)) * 100.0);
											}
										}
									}
								)
							;
							
							ClientCache.f_Clear();
									
							struct CComment
							{
								CStr m_OneLine;
								TCVector<CStr> m_Lines;
							};
							
							TCMap<CStr, TCVector<CComment>> Comments;
							CComment *pLastComment = nullptr;
							for (auto &ChangeList : ChangeLists)
							{
								CPerforceClient::CChangeList ChangeListInfo = pClient->f_GetChangelist(ChangeList);
								
								ch8 const *pParse = ChangeListInfo.m_Description;
								
								CStr CurrentProduct = "Unknown";
								bool bDetailedDescription = false;
								bool bInIntegration = false;
								
								fg_ParseWhiteSpace(pParse);
								while (*pParse)
								{
									if (*pParse == '[')
									{
										CStr Product;
										aint nParsed = 0;
										aint nChars = (CStr::CParse("[{}]") >> Product).f_Parse(pParse, nParsed);
										if (nParsed == 1)
										{
											CurrentProduct = Product;
											pParse += nChars;
											bDetailedDescription = false;
											bInIntegration = false;
											fg_ParseWhiteSpace(pParse);
											continue;
										}
									}
									auto pParseStart = pParse;
									fg_ParseToEndOfLine(pParse);
									CStr Line(pParseStart, pParse - pParseStart);
									fg_ParseEndOfLine(pParse);

									if (Line == "Imported from Git")
										break; // The rest of the changelist is not relevant
									
									// #review: @Glenn_Stiemens, @Anders_Wass

									bool bFoundReview = false;
									{
										smint iReview;
										while ((iReview = Line.f_FindNoCase("#review")) >= 0)
										{
											mint Length = fg_StrLen("#review");
											if (Line.f_GetAt(iReview + Length) == ':')
												++Length;
											if (Line.f_GetAt(iReview + Length) == '-')
											{
												++Length;
												while (fg_CharIsNumber(Line.f_GetAt(iReview + Length)))
													++Length;
											}
											
											Line = Line.f_Delete(iReview, Length);
											bFoundReview = true;
										}
									}
									
									{
										smint iMention;
										while ((iMention = Line.f_FindChar('@')) >= 0)
										{
											mint Length = 1;
											for 
												(
													ch8 Char = Line.f_GetAt(iMention + Length)
													; fg_CharIsAlphabetical(Char) || Char == '_' || fg_CharIsNumber(Char)
													; Char = Line.f_GetAt(iMention + Length)
												)
											{
												++Length;
											}
											
											if (Line.f_GetAt(iMention + Length) == ',')
												++Length;
											Line = Line.f_Delete(iMention, Length);
											bFoundReview = true;
										}
									}

									if (bFoundReview)
									{
										Line = Line.f_Trim();
										if (Line.f_IsEmpty())
											continue;
									}

									if (Line.f_FindReverse(" (integrated)") >= 0)
									{
										bInIntegration = true;
										bDetailedDescription = false;
									}
									else if (bInIntegration)
									{
										if (Line.f_IsEmpty())
											bDetailedDescription = true;
										else if (Line.f_StartsWith("\t"))
										{
											bDetailedDescription = true;
											Line = Line.f_Extract(1);
										}
										else if (!Line.f_IsEmpty())
										{
											bInIntegration = false;
											bDetailedDescription = false;
										}
									}
									else
									{
										if (Line.f_IsEmpty() && !bDetailedDescription)
										{
											bDetailedDescription = true;
											continue;
										}
									}
									
									if (!bDetailedDescription && Line.f_IsEmpty())
										continue;								
									
									if (!bDetailedDescription && !Line.f_IsEmpty() && Line.f_FindReverse(" (integrated)") < 0)
										Line += " (integrated)";
									
									if (pLastComment && bDetailedDescription)
									{
										pLastComment->m_Lines.f_Insert(Line);
									}
									else
									{
										pLastComment = &Comments[CurrentProduct].f_Insert();
										pLastComment->m_OneLine = Line;
									}
								}
								
							}
							
							mint nComments = 0;
							for (auto iComment = Comments.f_GetIterator(); iComment; ++iComment)
								nComments += iComment->f_GetLen();
									
							bool bAbbreviated = nComments > 100;
									
							if (bAbbreviated)
								fg_AppendFormat(FullCommentList, "More than 100 changes. {} changes were not enumerated.{\n}", nComments);
							
							for (auto iComment = Comments.f_GetIterator(); iComment; ++iComment)
							{
								if (!bAbbreviated)
								{
									fg_AppendFormat(FullCommentList, "[{}]{\n}", iComment.f_GetKey());
									for (auto &Comment : *iComment)
									{
										fg_AppendFormat(FullCommentList, "{}{\n}", Comment.m_OneLine);
										while (!Comment.m_Lines.f_IsEmpty() && Comment.m_Lines.f_GetLast().f_IsEmpty())
											Comment.m_Lines.f_SetLen(Comment.m_Lines.f_GetLen() - 1);
										for (auto const &Line : Comment.m_Lines)
											fg_AppendFormat(FullCommentList, "\t{}{\n}", Line);
									}
									fg_AppendFormat(FullCommentList, "{\n}");
								}

								fg_AppendFormat(FullCommentListDisplay, "\t[{}]{\n}", iComment.f_GetKey());
								for (auto const &Comment : *iComment)
								{
									fg_AppendFormat(FullCommentListDisplay, "\t{}{\n}", Comment.m_OneLine);
									for (auto const &Line : Comment.m_Lines)
										fg_AppendFormat(FullCommentListDisplay, "\t\t{}{\n}", Line);
								}
								fg_AppendFormat(FullCommentListDisplay, "\t{\n}");
							}
							
							for (auto iAction = Actions.f_GetIterator(); iAction; ++iAction)
							{
								if (iAction.f_GetKey() == CPerforceClient::EAction_Add)
									DConOut("\t{}\t\t\t{} files{\n}", CPerforceClient::fs_ActionToStr(iAction.f_GetKey()) << *iAction);
								else
									DConOut("\t{}\t\t{} files{\n}", CPerforceClient::fs_ActionToStr(iAction.f_GetKey()) << *iAction);
							}
							
							DConOut("{\n}{}", FullCommentListDisplay);
							if (bAbbreviated)
								DConOut("\tWARNING: More than 100 changes, skipping output to changelist description.{\n}", 0);
						}
						
						if (SplitChangelist != -1)
						{
							//DConOut("\tWARNING: Merged instead of copied to handle renames correctly{\n}", 0);
							DConOut("\tWARNING: Limited merge to changelist {} to handle renames correctly, please do another merge after this one{\n}", SplitChangelist);
						}
						
						if (!_bPretend)
						{
							CPerforceClient::CChangeList ChangeList = pClient->f_GetChangelist(0);

							uint32 DestinationChangelist = 0;
							TCVector<CStr> Jobs;
							CStr SubmitJob = fg_GetSys()->f_GetEnvironmentVariable("MTool_P4SubmitJob");
							if (!SubmitJob.f_IsEmpty())
								Jobs.f_Insert(SubmitJob);
							TCVector<CStr> Files;
							for (auto &File : ChangeList.m_Files)
								Files.f_Insert(File.m_Name);
							
							DestinationChangelist = pClient->f_CreateChangelist(FullCommentList, Jobs, Files);

							pClient->f_ResolveAutomatic(CStr(), DestinationChangelist);

							if (!_bNoSubmit)
							{
								uint32 FinalChangelist = pClient->f_SubmitChangelist(DestinationChangelist, false);
							
								DConOut("\tSubmitted changelist {}{\n}", FinalChangelist);
							}
						}
						
						if (!Actions.f_IsEmpty())
							bDoSomething = true;
					}
				}
				IntegrationPairs = NewIntegrationPairs;

				return bDoSomething;
			}
		;
		
		DConOut("Doing test merge{\n}{\n}", 0);
		CClock Clock;
		Clock.f_Start();
		fp64 StartTime = Clock.f_GetTime();
		bool bNeedMerge = fl_Integrate(true, false);
		fp64 EndTime = Clock.f_GetTime();
		if (bNeedMerge)
		{
			DConOut("Test merge took {} seconds{\n}{\n}", (EndTime - StartTime));
			CStr Result = fg_AskUser(StdInReader, "Are you sure you want to do the merge? [Y/n/NoSubmit]{\n}");
			
			if (Result == "Y" || Result == "y" || Result == "" || Result == "NoSubmit")
			{
				DConOut("Doing real merge{\n}{\n}", 0);
				StartTime = Clock.f_GetTime();
				fl_Integrate(false, Result == "NoSubmit");
				EndTime = Clock.f_GetTime();				
				
				DConOut("Real merge took {} seconds{\n}{\n}", (EndTime - StartTime));
			}
			else
			{
				DoneMessage = "Aborted!";
				return 0;
			}
		}
		else
			DoneMessage = "Nothing to merge, done";
		
		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_PerforceMerge);
