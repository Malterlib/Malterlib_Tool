// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>

// The Perforce headers use Malterlib's types without their namespaces.
namespace NMib::NPerforce
{
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NFunction;
	using namespace NMib::NConcurrency;
	using namespace NMib::NEncoding;
}

#include <Mib/Perforce/TestServer>
#include <Mib/Perforce/Wrapper>

namespace NMib::NTool
{
	using namespace NTestHelpers;

	namespace
	{
		// Dev is populated from Main, the stream of the fixture's submits.
		void fg_CreateDevelopmentStream(NPerforce::CPerforceClientThrow &_Client)
		{
			NPerforce::CPerforceClient::CStream Spec;
			Spec.m_Name = "Dev";
			Spec.m_Parent = "//Streams/Main";
			Spec.m_Type = "development";
			Spec.m_Description = "Created by the test.";
			Spec.m_Options = {"allsubmit", "unlocked", "toparent", "fromparent", "mergedown"};
			Spec.m_Paths = {"share ..."};
			_Client.f_SetStream("//Streams/Dev", Spec);
			_Client.f_PopulateStream("//Streams/Dev");
		}

		// The workspace can only be used from _Host.
		void fg_CreateWorkspace(NPerforce::CPerforceClientThrow &_Client, CStr const &_Name, CStr const &_Stream, CStr const &_Root, CStr const &_Host)
		{
			NPerforce::CPerforceClient::CClient Spec;
			Spec.m_Root = _Root;
			Spec.m_Host = _Host;
			Spec.m_Description = "Perforce test workspace.";
			Spec.m_Options = {"allwrite", "clobber", "nocompress", "unlocked", "nomodtime", "rmdir"};
			Spec.m_Stream = _Stream;
			_Client.f_SetClient(_Name, Spec);
		}

		struct CPerforceMergeRun
		{
			CProcessLaunchActor::CSimpleLaunchResult m_Result;
			CStr m_DevRoot;
			CStr m_Port;
			TCVector<CStr> m_Unresolved;  // The files of the destination workspace that are opened and need resolving
			TCMap<CStr, CStr> m_Contents; // The destination workspace's files after the merge
			bool m_bSubmitted = false;
		};

		// Merges //Streams/Main into its child //Streams/Dev with MTool PerforceMerge, answering its question with _Answer. Both streams
		// change Both.txt, on different lines, and Clash.txt, on the same line, both of which a safe resolve leaves unresolved. MTool gets
		// the server and user from its environment, or with _bSettingsInConfigFile from the .p4config file of its working directory.
		auto fg_RunPerforceMerge(CRepositoryFixture &_Repo, CStr _Answer, CStr _DevWorkspace, CStr _User, bool _bSettingsInConfigFile) -> TCUnsafeFuture<CPerforceMergeRun>
		{
			using namespace NPerforce;
			using CPerforceFile = CPerforceTestServer::CFile;

			CPerforceMergeRun Run;
			Run.m_DevRoot = _Repo.m_Root / "Dev";
			CStr MainRoot = _Repo.m_Root / "Main";
			CStr Host = "PerforceTestServer";

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, _Repo.m_Root / "Perforce", _User);
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			// PerforceMerge counts the changes to merge by integrating each revision outside the stream views, which p4d refuses by
			// default.
			co_await Server(&CPerforceTestServer::f_SetConfigurable, CStr("dm.integ.stream.allowdepotfilespec"), CStr("1"));
			CStr Port = co_await Server(&CPerforceTestServer::f_GetPort);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Both.txt", "1\n2\n3\n4\n5\n"}, {"Clash.txt", "1\n2\n3\n"}, {"Theirs.txt", "a\nb\nc\n"}}
					, CStr("Add the files")
				)
			;

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection]
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						fg_CreateDevelopmentStream(Client);
					}
					% "Failed to create the development stream"
				)
			;

			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Dev")
					, TCVector<CPerforceFile>{{"Both.txt", "1 dev\n2\n3\n4\n5\n"}, {"Clash.txt", "1\n2 dev\n3\n"}}
					, CStr("Change the development stream")
				)
			;
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Both.txt", "1\n2\n3\n4\n5 main\n"}, {"Clash.txt", "1\n2 main\n3\n"}, {"Theirs.txt", "a\nb\nc main\n"}}
					, CStr("Change the mainline")
				)
			;

			// MTool finds the destination stream's workspace among the user's workspaces of its host.
			auto DevConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, _DevWorkspace);
			int64 DevHead = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, DevConnection, Host, MainRoot, DevRoot = Run.m_DevRoot, DevWorkspace = _DevWorkspace]() -> int64
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						fg_CreateWorkspace(Client, "MainWork", "//Streams/Main", MainRoot, Host);
						fg_CreateWorkspace(Client, DevWorkspace, "//Streams/Dev", DevRoot, Host);

						CPerforceClientThrow DevClient(DevConnection);
						DevClient.f_Login(CStr());
						DevClient.f_Sync(DevRoot / "...");

						return DevClient.f_GetHeadChangelist("//Streams/Dev/...");
					}
					% "Failed to create the workspaces"
				)
			;

			Run.m_Port = Port;
			_Repo.f_SetPerforceEnvironment();
			if (_bSettingsInConfigFile)
				CFile::fs_WriteStringToFile(_Repo.m_Root / ".p4config", "P4PORT={}\nP4USER={}\n"_f << Port << _User, false);
			else
			{
				_Repo.m_Environment["P4PORT"] = Port;
				_Repo.m_Environment["P4USER"] = _User;
			}
			_Repo.m_Environment["P4CLIENT"] = "MainWork";
			_Repo.m_Environment["P4HOST"] = Host;
			_Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;
			_Repo.m_Environment["MTool_AskUser"] = _Answer;

			Run.m_Result = co_await _Repo.f_Tool({"PerforceMerge", "Stream=//Streams/Main", "Direction=Child"});

			auto [Unresolved, DevHeadAfter] = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [DevConnection]() -> TCTuple<TCVector<CStr>, int64>
					{
						CPerforceClientThrow Client(DevConnection);
						Client.f_Login(CStr());

						return {Client.f_GetFileStatClientFiles({"-Ru", "//Streams/Dev/..."}), Client.f_GetHeadChangelist("//Streams/Dev/...")};
					}
					% "Failed to read the state of the destination workspace"
				)
			;

			Run.m_Unresolved = fg_Move(Unresolved);
			Run.m_bSubmitted = DevHeadAfter != DevHead;
			for (auto &pName : {"Both.txt", "Clash.txt", "Theirs.txt"})
				Run.m_Contents[pName] = CFile::fs_ReadStringFromFile(Run.m_DevRoot / pName, true);

			co_return fg_Move(Run);
		}

		struct CPerforceUnshelveRun
		{
			CProcessLaunchActor::CSimpleLaunchResult m_Result;
			CStr m_DevRoot;
			CStr m_Port;
			TCVector<CStr> m_Unresolved; // The files of Dev's workspace that are opened and need resolving
		};

		// Unshelves a change shelved in //Streams/Main into its child //Streams/Dev with MTool PerforceUnshelveRelatedStream, whose
		// settings are in the .p4config file of its working directory. Its edit of Both.txt, which Dev changes on another line, is left
		// to merge. The workspaces can only be used from _Host, which MTool's P4HOST names.
		auto fg_RunPerforceUnshelve(CRepositoryFixture &_Repo, CStr _Host) -> TCUnsafeFuture<CPerforceUnshelveRun>
		{
			using namespace NPerforce;
			using CPerforceFile = CPerforceTestServer::CFile;

			CPerforceUnshelveRun Run;
			Run.m_DevRoot = _Repo.m_Root / "Dev";
			CStr MainRoot = _Repo.m_Root / "Main";

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, _Repo.m_Root / "Perforce", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			// The unshelve maps the change with a branch spec, outside the stream views, which p4d refuses by default.
			co_await Server(&CPerforceTestServer::f_SetConfigurable, CStr("dm.integ.stream.allowdepotfilespec"), CStr("1"));
			Run.m_Port = co_await Server(&CPerforceTestServer::f_GetPort);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceFile>{{"Both.txt", "1\n2\n3\n4\n5\n"}, {"Theirs.txt", "a\nb\nc\n"}}
					, CStr("Add the files")
				)
			;

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection]
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						fg_CreateDevelopmentStream(Client);
					}
					% "Failed to create the development stream"
				)
			;

			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Dev"), TCVector<CPerforceFile>{{"Both.txt", "1 dev\n2\n3\n4\n5\n"}}, CStr("Change the development stream"));

			auto MainConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("MainWork"));
			MainConnection.m_Host = _Host;
			auto DevConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("DevWork"));
			DevConnection.m_Host = _Host;

			uint32 Shelved = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, MainConnection, DevConnection, Host = _Host, MainRoot, DevRoot = Run.m_DevRoot]() -> uint32
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						fg_CreateWorkspace(Client, "MainWork", "//Streams/Main", MainRoot, Host);
						fg_CreateWorkspace(Client, "DevWork", "//Streams/Dev", DevRoot, Host);

						CPerforceClientThrow MainClient(MainConnection);
						MainClient.f_Login(CStr());
						MainClient.f_Sync(MainRoot / "...");
						MainClient.f_OpenForEdit(MainRoot / "Both.txt");
						MainClient.f_OpenForEdit(MainRoot / "Theirs.txt");
						CFile::fs_WriteStringToFile(MainRoot / "Both.txt", "1\n2\n3\n4\n5 main\n", false);
						CFile::fs_WriteStringToFile(MainRoot / "Theirs.txt", "a\nb\nc main\n", false);
						TCVector<CStr> Files = {"//Streams/Main/Both.txt", "//Streams/Main/Theirs.txt"};
						uint32 Changelist = MainClient.f_CreateChangelist("Change the mainline", {}, Files);
						MainClient.f_ShelveChangelist(Changelist, false, false, Files);

						CPerforceClientThrow DevClient(DevConnection);
						DevClient.f_Login(CStr());
						DevClient.f_Sync(DevRoot / "...");

						return Changelist;
					}
					% "Failed to shelve the change"
				)
			;

			_Repo.f_SetPerforceEnvironment();
			CFile::fs_WriteStringToFile(_Repo.m_Root / ".p4config", "P4PORT={}\nP4USER=tester\nP4CLIENT=DevWork\n"_f << Run.m_Port, false);
			_Repo.m_Environment["P4HOST"] = _Host;
			_Repo.m_Environment["P4TICKETS"] = Connection.m_TicketFile;

			Run.m_Result = co_await _Repo.f_Tool({"PerforceUnshelveRelatedStream", "ChangeList={}"_f << Shelved});

			Run.m_Unresolved = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [DevConnection]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(DevConnection);
						Client.f_Login(CStr());

						return Client.f_GetFileStatClientFiles({"-Ru", "//Streams/Dev/..."});
					}
					% "Failed to read the state of Dev's workspace"
				)
			;

			co_return fg_Move(Run);
		}
	}

	struct CPerforceMerge_Tests : CTest
	{
		void f_DoTests();
	};

	void CPerforceMerge_Tests::f_DoTests()
	{
		// The merge resolves safely: a file changed on both sides is left for the user to merge, even when it would merge without
		// conflicts, and the files that need merging are reported with how to continue.
		DMibTestSuite("PerforceMerge") -> TCFuture<void>
		{
			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce merge tests: p4d was not found in PATH");
				co_return {};
			}

			DMibTestCategory("Submit") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				// The commands to continue with name the server and user that MTool's working directory gives it, and are quoted for
				// Bash: the names of the workspace and the user have $ and a quote. A client spec refuses a name with whitespace.
				auto Run = co_await fg_RunPerforceMerge(Repo, "Y", "Dev$Work's", "Merge$User's", true);
				CStr Output = Run.m_Result.f_GetCombinedOut();
				if (Run.m_Result.m_ExitCode == 0 || Output.f_Find(Run.m_DevRoot / "Both.txt") < 0)
					DMibConOut("PerforceMerge output:\n{}\n", Output);

				DMibExpect(Run.m_Result.m_ExitCode, !=, 0u);
				DMibExpectFalse(Run.m_bSubmitted);
				DMibExpect(Run.m_Unresolved, ==, (TCVector<CStr>{Run.m_DevRoot / "Both.txt", Run.m_DevRoot / "Clash.txt"}));
				DMibExpect(Run.m_Contents["Both.txt"], ==, NPerforce::CPerforceTestServer::fs_WorkspaceText("1 dev\n2\n3\n4\n5\n"));
				DMibExpect(Run.m_Contents["Clash.txt"], ==, NPerforce::CPerforceTestServer::fs_WorkspaceText("1\n2 dev\n3\n"));
				DMibExpect(Run.m_Contents["Theirs.txt"], ==, NPerforce::CPerforceTestServer::fs_WorkspaceText("a\nb\nc main\n"));
				DMibExpect(Output.f_Find(Run.m_DevRoot / "Both.txt"), >=, 0);
				DMibExpect(Output.f_Find(Run.m_DevRoot / "Clash.txt"), >=, 0);
				DMibExpect(Output.f_Find(Run.m_DevRoot / "Theirs.txt"), <, 0);
				CStr Connection = "p4 -p '{}' -u 'Merge$User'\\''s' -H PerforceTestServer -c 'Dev$Work'\\''s'"_f << Run.m_Port;
				DMibExpect(Output.f_Find(Connection + " resolve -c "), >=, 0);
				DMibExpect(Output.f_Find(Connection + " submit -c "), >=, 0);

				co_return {};
			};

			DMibTestCategory("NoSubmit") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				auto Run = co_await fg_RunPerforceMerge(Repo, "NoSubmit", "DevWork", "tester", false);
				CStr Output = Run.m_Result.f_GetCombinedOut();
				if (Run.m_Result.m_ExitCode != 0 || Output.f_Find(Run.m_DevRoot / "Both.txt") < 0)
					DMibConOut("PerforceMerge output:\n{}\n", Output);

				DMibExpect(Run.m_Result.m_ExitCode, ==, 0u);
				DMibExpectFalse(Run.m_bSubmitted);
				DMibExpect(Run.m_Unresolved, ==, (TCVector<CStr>{Run.m_DevRoot / "Both.txt", Run.m_DevRoot / "Clash.txt"}));
				DMibExpect(Run.m_Contents["Both.txt"], ==, NPerforce::CPerforceTestServer::fs_WorkspaceText("1 dev\n2\n3\n4\n5\n"));
				DMibExpect(Run.m_Contents["Theirs.txt"], ==, NPerforce::CPerforceTestServer::fs_WorkspaceText("a\nb\nc main\n"));
				DMibExpect(Output.f_Find(Run.m_DevRoot / "Both.txt"), >=, 0);
				DMibExpect(Output.f_Find(Run.m_DevRoot / "Clash.txt"), >=, 0);
				DMibExpect(Output.f_Find(CStr("p4 -p '{}' -u tester -H PerforceTestServer -c DevWork resolve -c "_f << Run.m_Port)), >=, 0);

				co_return {};
			};

			co_return {};
		};

		// The report names the workspace, server, user and host the session uses, which here come from the .p4config file and P4HOST
		// rather than the connection, quoted for Bash.
		DMibTestSuite("PerforceUnshelveRelatedStream") -> TCFuture<void>
		{
			if (NPerforce::CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce unshelve tests: p4d was not found in PATH");
				co_return {};
			}

			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			auto Run = co_await fg_RunPerforceUnshelve(Repo, "Unshelve$Host's");
			CStr Output = Run.m_Result.f_GetCombinedOut();
			CStr Connection = "p4 -p '{}' -u tester -H 'Unshelve$Host'\\''s' -c DevWork"_f << Run.m_Port;
			if (Run.m_Result.m_ExitCode != 0 || Output.f_Find(Connection) < 0)
				DMibConOut("PerforceUnshelveRelatedStream output:\n{}\n", Output);

			DMibExpect(Run.m_Result.m_ExitCode, ==, 0u);
			DMibExpect(Run.m_Unresolved, ==, TCVector<CStr>{Run.m_DevRoot / "Both.txt"});
			DMibExpect(Output.f_Find(Run.m_DevRoot / "Both.txt"), >=, 0);
			DMibExpect(Output.f_Find(Run.m_DevRoot / "Theirs.txt"), <, 0);
			DMibExpect(Output.f_Find(Connection + " resolve -c "), >=, 0);
			DMibExpect(Output.f_Find(Connection + " submit -c "), >=, 0);

			co_return {};
		};
	}

	DMibTestRegister(CPerforceMerge_Tests, Malterlib::Tool);
}
