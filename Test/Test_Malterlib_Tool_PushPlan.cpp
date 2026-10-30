// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CPushPlan_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("PushPlan") -> TCFuture<void>
			{
				CRepositoryFixture Repo;
				co_await Repo.f_Init();

				CCheckoutFixture Checkout(Repo);
				co_await Checkout.f_Init({"A", "B", "C"});
				co_await Checkout.f_MibSucceeds({"update-repos"});

				CStr A = Checkout.f_Repository("A");
				CStr B = Checkout.f_Repository("B");
				CStr C = Checkout.f_Repository("C");

				// The root's default branch on origin pins A and B, but not C, which is new to it
				CStr PinA = co_await Checkout.f_GitIn(A, {"rev-parse", "HEAD"});
				CStr PinB = co_await Checkout.f_GitIn(B, {"rev-parse", "HEAD"});
				{
					CEJsonSorted Pins;
					Pins["Repos/A"]["Hash"] = PinA;
					Pins["Repos/B"]["Hash"] = PinB;
					Repo.f_Write("Repos.MRepo", Pins.f_ToString());
					co_await Repo.f_Commit();
					co_await Repo.f_Git({"push", "-q", "origin", "main"});
				}

				auto fRemoteRef = [&](CStr _Directory, CStr _Ref) -> TCFuture<CStr>
					{
						return Checkout.f_GitIn(_Directory, {"ls-remote", "origin", _Ref});
					}
				;

				// Rewrites the only commit of the default branch, which makes pushing it a forced push
				auto fRewrite = [&](CStr _Directory) -> TCFuture<CStr>
					{
						return Checkout.f_GitIn(_Directory, {"-c", "core.hooksPath=" + (Repo.m_Root / "no-hooks"), "commit", "-q", "--amend", "-m", "Rewritten"});
					}
				;

				auto fPlan = [&](TCVector<CStr> _Params) -> TCFuture<CProcessLaunchActor::CSimpleLaunchResult>
					{
						TCVector<CStr> Params = {"push", "--pretend", "--json"};
						Params.f_Insert(fg_Move(_Params));
						return Checkout.f_Mib(fg_Move(Params));
					}
				;

				auto fParsePlan = [](CStr const &_Output) -> CEJsonOrdered
					{
						return CEJsonOrdered::fs_FromString(_Output, "push --pretend --json");
					}
				;

				DMibTestCategory("ForkMovedSincePin") -> TCFuture<void>
				{
					// Someone pushed to A's default branch after the root pinned it
					CStr Moved = co_await Checkout.f_CommitIn(Repo.m_Root / "work/A", "Moved");
					co_await Checkout.f_GitIn(Repo.m_Root / "work/A", {"push", "-q", Checkout.f_Remote("A"), "main"});
					co_await Checkout.f_CommitIn(A, "LocalA");

					auto Result = co_await Checkout.f_Mib({"push", "--force", "--repo-name", "Repos/A"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find(CStr("pins {}, but origin/main is at {}"_f << PinA << Moved)), >=, 0);

					CStr RemoteMain = co_await fRemoteRef(A, "refs/heads/main");
					DMibExpect(RemoteMain.f_StartsWith(Moved), ==, true);
					CStr RemoteTags = co_await Checkout.f_GitIn(A, {"ls-remote", "--tags", "origin"});
					DMibExpect(RemoteTags, ==, "");

					co_return {};
				};

				DMibTestCategory("AtPreviousPin") -> TCFuture<void>
				{
					co_await fRewrite(B);
					CStr Rewritten = co_await Checkout.f_GitIn(B, {"rev-parse", "HEAD"});
					CStr ShortPin = co_await Checkout.f_GitIn(B, {"rev-parse", "--short", PinB});

					auto Planned = co_await fPlan({"--force", "--repo-name", "Repos/B"});
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 1);

					auto &Push = Pushes.f_Array()[0];
					DMibExpect(Push["Path"].f_String(), ==, "Repos/B");
					DMibExpect(Push["Remote"].f_String(), ==, "origin");
					DMibExpect(Push["Branch"].f_String(), ==, "main");
					DMibExpect(Push["RemoteHead"].f_String(), ==, PinB);
					DMibExpect(Push["Target"].f_String(), ==, Rewritten);
					DMibExpect(Push["Kind"].f_String(), ==, "ForceUpdate");
					DMibExpect(Push["Lease"].f_String(), ==, PinB);
					DMibExpect(Push["LeaseSource"].f_String(), ==, "PreviousPin");
					DMibAssert(Push["BackupTags"].f_Array().f_GetLen(), ==, 1);
					DMibExpect(Push["BackupTags"][0]["Name"].f_String(), ==, "main_" + ShortPin);
					DMibExpect(Push["BackupTags"][0]["Commit"].f_String(), ==, PinB);
					DMibExpect(Push["BackupTags"][0]["Action"].f_String(), ==, "Create");

					co_await Checkout.f_MibSucceeds({"push", "--force", "--repo-name", "Repos/B"});

					CStr RemoteMain = co_await fRemoteRef(B, "refs/heads/main");
					DMibExpect(RemoteMain.f_StartsWith(Rewritten), ==, true);
					CStr RemoteTag = co_await fRemoteRef(B, "refs/tags/main_" + ShortPin);
					DMibExpect(RemoteTag.f_StartsWith(PinB), ==, true);

					co_return {};
				};

				DMibTestCategory("NewRepository") -> TCFuture<void>
				{
					CStr RemoteHead = co_await Checkout.f_GitIn(C, {"rev-parse", "HEAD"});
					co_await fRewrite(C);

					auto Planned = co_await fPlan({"--force", "--repo-name", "Repos/C"});
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 1);

					auto &Push = Pushes.f_Array()[0];
					DMibExpect(Push["Kind"].f_String(), ==, "ForceUpdate");
					DMibExpect(Push["Lease"].f_String(), ==, RemoteHead);
					DMibExpect(Push["LeaseSource"].f_String(), ==, "RemoteHead");
					DMibAssert(Push["BackupTags"].f_Array().f_GetLen(), ==, 1);
					DMibExpect(Push["BackupTags"][0]["Commit"].f_String(), ==, RemoteHead);

					// The plan is what the push does, also with a tag that is named like the planned commit and points elsewhere
					CStr Rewritten = co_await Checkout.f_GitIn(C, {"rev-parse", "HEAD"});
					co_await Checkout.f_GitIn(C, {"tag", Rewritten, RemoteHead});
					co_await Checkout.f_MibSucceeds({"push", "--force", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"tag", "-d", Rewritten});
					CStr RemoteMain = co_await fRemoteRef(C, "refs/heads/main");
					DMibExpect(RemoteMain.f_StartsWith(Rewritten), ==, true);

					co_return {};
				};

				// The feature branch of C is only on C, so these leave the repositories as they are, instead of returning C to the root's branch
				DMibTestCategory("StaleTrackingRef") -> TCFuture<void>
				{
					// A feature branch that moved on the remote after this checkout last fetched it
					co_await Checkout.f_GitIn(C, {"checkout", "-q", "-b", "feature"});
					co_await Checkout.f_CommitIn(C, "Feature");
					co_await Checkout.f_GitIn(C, {"push", "-q", "-u", "origin", "feature"});

					CStr Other = Repo.m_Root / "work/OtherC";
					co_await Repo.f_Run("git", {"clone", "-q", "-b", "feature", Checkout.f_Remote("C"), Other}, Repo.m_Root);
					CStr Moved = co_await Checkout.f_CommitIn(Other, "FeatureMoved");
					co_await Checkout.f_GitIn(Other, {"push", "-q", "origin", "feature"});

					CStr Tracking = co_await Checkout.f_GitIn(C, {"rev-parse", "refs/remotes/origin/feature"});
					co_await fRewrite(C);

					auto Result = co_await Checkout.f_Mib({"push", "--force", "--skip-update", "--repo-name", "Repos/C"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find(CStr("origin/feature is at {} on the remote, but this checkout last fetched {}"_f << Moved << Tracking)), >=, 0);

					CStr RemoteFeature = co_await fRemoteRef(C, "refs/heads/feature");
					DMibExpect(RemoteFeature.f_StartsWith(Moved), ==, true);

					// Reading the remote's head fetched its commit, without moving the remote-tracking ref
					CStr TrackingAfter = co_await Checkout.f_GitIn(C, {"rev-parse", "refs/remotes/origin/feature"});
					DMibExpect(TrackingAfter, ==, Tracking);
					CStr MovedObject = co_await Checkout.f_GitIn(C, {"cat-file", "-t", Moved});
					DMibExpect(MovedObject, ==, "commit");

					co_return {};
				};

				DMibTestCategory("MissingTrackingRef") -> TCFuture<void>
				{
					// The branch exists on the remote, but this checkout has no remote-tracking ref for it
					co_await Checkout.f_GitIn(C, {"fetch", "-q", "origin"});
					co_await Checkout.f_GitIn(C, {"reset", "-q", "--hard", "origin/feature"});
					CStr RemoteHead = co_await Checkout.f_GitIn(C, {"rev-parse", "HEAD"});
					CStr Target = co_await Checkout.f_CommitIn(C, "FeatureAhead");
					co_await Checkout.f_GitIn(C, {"update-ref", "-d", "refs/remotes/origin/feature"});

					auto Planned = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 1);

					auto &Push = Pushes.f_Array()[0];
					DMibExpect(Push["Kind"].f_String(), ==, "Update");
					DMibExpect(Push["RemoteHead"].f_String(), ==, RemoteHead);
					DMibExpect(Push["Target"].f_String(), ==, Target);
					DMibExpect(Push["Lease"].f_String(), ==, "");
					DMibExpect(Push["BackupTags"].f_Array().f_GetLen(), ==, 0);

					co_return {};
				};

				DMibTestCategory("PushUrl") -> TCFuture<void>
				{
					// The fetch URL and the remote-tracking ref have the commit already, while the push URL, where the push goes, is
					// behind
					CStr Mirror = Repo.m_Root / "remotes/MirrorC.git";
					co_await Repo.f_Run("git", {"clone", "-q", "--bare", Checkout.f_Remote("C"), Mirror}, Repo.m_Root);
					CStr MirrorHead = co_await Checkout.f_GitIn(Mirror, {"rev-parse", "refs/heads/feature"});
					CStr Target = co_await Checkout.f_GitIn(C, {"rev-parse", "HEAD"});
					co_await Checkout.f_GitIn(C, {"push", "-q", "origin", "feature"});
					co_await Checkout.f_GitIn(C, {"config", "remote.origin.pushurl", Mirror});

					// The push URL is asked with the other settings of the remote, here an upload-pack command that leaves a mark
					CStr UploadPack = Repo.m_Root / "bin/upload-pack-marking";
					CStr UploadPackMark = Repo.m_Root / "upload-pack-ran";
					CFile::fs_CreateDirectoryForFile(UploadPack);
					CFile::fs_WriteStringToFile(UploadPack, "#!/bin/sh\ntouch \"" + UploadPackMark + "\"\nexec git-upload-pack \"$@\"\n", false);
					CFile::fs_SetAttributes(UploadPack, CFile::fs_GetAttributes(UploadPack) | EFileAttrib_Executable);
					co_await Checkout.f_GitIn(C, {"config", "remote.origin.uploadpack", UploadPack});

					auto Planned = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "remote.origin.uploadpack"});
					DMibExpectTrue(CFile::fs_FileExists(UploadPackMark));
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 1);
					DMibExpect(Pushes.f_Array()[0]["RemoteHead"].f_String(), ==, MirrorHead);
					DMibExpect(Pushes.f_Array()[0]["Target"].f_String(), ==, Target);

					co_await Checkout.f_MibSucceeds({"push", "--skip-update", "--repo-name", "Repos/C"});
					CStr MirrorAfter = co_await Checkout.f_GitIn(Mirror, {"rev-parse", "refs/heads/feature"});
					DMibExpect(MirrorAfter, ==, Target);

					// Configured directly, as a narrow fetch refspec leaves no remote-tracking ref to set it from
					CStr Merge = co_await Checkout.f_GitIn(C, {"config", "branch.feature.merge"});
					DMibExpect(Merge, ==, "refs/heads/feature");

					// A second push URL at another commit can't share the lease of the push
					CStr Mirror2 = Repo.m_Root / "remotes/MirrorC2.git";
					co_await Repo.f_Run("git", {"clone", "-q", "--bare", Mirror, Mirror2}, Repo.m_Root);
					co_await Checkout.f_GitIn(Mirror2, {"update-ref", "refs/heads/feature", MirrorHead});
					co_await Checkout.f_GitIn(C, {"config", "--add", "remote.origin.pushurl", Mirror2});
					co_await Checkout.f_CommitIn(C, "AfterMirror");
					auto Differing = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					DMibExpect(Differing.m_ExitCode, !=, 0);
					DMibExpect(Differing.f_GetCombinedOut().f_Find("have feature at different commits"), >=, 0);

					co_await Checkout.f_GitIn(C, {"config", "--unset-all", "remote.origin.pushurl"});
					co_await Checkout.f_GitIn(C, {"reset", "-q", "--hard", "HEAD~1"});

					// A push URL that URL rewriting resolved is asked as it was resolved, not rewritten again: the push goes to the
					// mirror, where the branch is up to date, so only tags follow, not to the second mirror, which is behind
					co_await Checkout.f_GitIn(C, {"config", "url.{}.insteadOf"_f << Mirror, "chain-a:"});
					co_await Checkout.f_GitIn(C, {"config", "url.{}.insteadOf"_f << Mirror2, Mirror});
					co_await Checkout.f_GitIn(C, {"config", "remote.origin.pushurl", "chain-a:"});
					auto Rewritten = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"config", "--unset-all", "remote.origin.pushurl"});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "url.{}.insteadOf"_f << Mirror});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "url.{}.insteadOf"_f << Mirror2});
					DMibAssert(Rewritten.m_ExitCode, ==, 0);
					auto RewrittenPushes = fParsePlan(Rewritten.f_GetStdOut());
					DMibAssert(RewrittenPushes.f_Array().f_GetLen(), ==, 1);
					DMibExpect(RewrittenPushes.f_Array()[0]["Kind"].f_String(), ==, "FollowTags");
					DMibExpect(RewrittenPushes.f_Array()[0]["RemoteHead"].f_String(), ==, RewrittenPushes.f_Array()[0]["Target"].f_String());

					// A push URL that no URL is rewritten to, from pushInsteadOf, is asked as git rewrites it, not refused
					CStr OriginUrl = co_await Checkout.f_GitIn(C, {"config", "remote.origin.url"});
					co_await Checkout.f_GitIn(C, {"config", "url.{}.pushInsteadOf"_f << Mirror, OriginUrl});
					co_await Checkout.f_GitIn(C, {"config", "url.{}.insteadOf"_f << Mirror2, Mirror});
					auto PushRewritten = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "url.{}.pushInsteadOf"_f << Mirror});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "url.{}.insteadOf"_f << Mirror2});
					DMibExpect(PushRewritten.m_ExitCode, ==, 0);

					co_return {};
				};

				DMibTestCategory("TailMatch") -> TCFuture<void>
				{
					// A branch named a/refs/heads/feature, listed first, also matches the pattern of feature and is already at the target
					CStr RemoteFeature = (co_await fRemoteRef(C, "refs/heads/feature")).f_Left(40);
					CStr Ahead = co_await Checkout.f_CommitIn(C, "TailAhead");
					co_await Checkout.f_GitIn(C, {"push", "-q", "origin", Ahead + ":refs/heads/a/refs/heads/feature"});
					co_await Checkout.f_GitIn(C, {"update-ref", "refs/remotes/origin/feature", RemoteFeature});

					auto Planned = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"push", "-q", "origin", ":refs/heads/a/refs/heads/feature"});
					co_await Checkout.f_GitIn(C, {"reset", "-q", "--hard", "HEAD~1"});
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 1);
					DMibExpect(Pushes.f_Array()[0]["RemoteHead"].f_String(), ==, RemoteFeature);
					DMibExpect(Pushes.f_Array()[0]["Target"].f_String(), ==, Ahead);

					co_return {};
				};

				DMibTestCategory("FollowTags") -> TCFuture<void>
				{
					// The branch is up to date on the remote, which the tracking ref doesn't know yet, and only a tag is new
					CStr Head = co_await Checkout.f_GitIn(C, {"rev-parse", "HEAD"});
					co_await Checkout.f_GitIn(C, {"push", "-q", "-f", "origin", "HEAD:refs/heads/feature"});
					co_await Checkout.f_GitIn(C, {"update-ref", "refs/remotes/origin/feature", "HEAD~1"});
					co_await Checkout.f_GitIn(C, {"tag", "-a", "-m", "Followed", "followed-tag"});

					auto Planned = co_await fPlan({"--skip-update", "--force", "--follow-tags", "--repo-name", "Repos/C"});
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 1);
					DMibExpect(Pushes.f_Array()[0]["Kind"].f_String(), ==, "FollowTags");
					DMibExpect(Pushes.f_Array()[0]["Target"].f_String(), ==, Head);

					co_await Checkout.f_GitIn(C, {"update-ref", "refs/remotes/origin/feature", "HEAD~1"});
					co_await Checkout.f_MibSucceeds({"push", "--skip-update", "--force", "--follow-tags", "--repo-name", "Repos/C"});
					CStr RemoteTag = co_await fRemoteRef(C, "refs/tags/followed-tag");
					DMibExpect(RemoteTag.f_IsEmpty(), ==, false);

					co_return {};
				};

				DMibTestCategory("SeedEmptyPushUrl") -> TCFuture<void>
				{
					// The remote fetches from a repository with branches, but pushes to an empty one, where the default branch is seeded
					CStr Empty = Repo.m_Root / "remotes/EmptyC.git";
					CFile::fs_CreateDirectory(Empty);
					co_await Repo.f_Run("git", {"init", "-q", "--bare", "-b", "main"}, Empty);
					co_await Checkout.f_GitIn(C, {"config", "remote.origin.pushurl", Empty});

					auto Planned = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "remote.origin.pushurl"});
					DMibAssert(Planned.m_ExitCode, ==, 0);
					auto Pushes = fParsePlan(Planned.f_GetStdOut());
					DMibAssert(Pushes.f_Array().f_GetLen(), ==, 2);
					DMibExpect(Pushes.f_Array()[0]["Kind"].f_String(), ==, "SeedDefault");
					DMibExpect(Pushes.f_Array()[0]["Branch"].f_String(), ==, "main");
					DMibExpect(Pushes.f_Array()[1]["Kind"].f_String(), ==, "New");

					// A branch whose tip the default branch has needs no push, also to a push URL of its own
					CStr Feature = co_await Checkout.f_GitIn(C, {"symbolic-ref", "--short", "HEAD"});
					co_await Checkout.f_GitIn(C, {"checkout", "-q", "-b", "on-main", "origin/main"});
					co_await Checkout.f_GitIn(C, {"config", "remote.origin.pushurl", Empty});
					auto OnMain = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"config", "--unset", "remote.origin.pushurl"});
					co_await Checkout.f_GitIn(C, {"checkout", "-q", Feature});
					co_await Checkout.f_GitIn(C, {"branch", "-q", "-D", "on-main"});
					DMibAssert(OnMain.m_ExitCode, ==, 0);
					DMibExpect(fParsePlan(OnMain.f_GetStdOut()).f_Array().f_GetLen(), ==, 0);

					// The seed goes to every push URL, so one that already has a default branch stops it
					CStr OnlyMain = Repo.m_Root / "remotes/OnlyMainC.git";
					co_await Repo.f_Run("git", {"clone", "-q", "--bare", Checkout.f_Remote("C"), OnlyMain}, Repo.m_Root);
					co_await Checkout.f_GitIn(OnlyMain, {"update-ref", "-d", "refs/heads/feature"});
					co_await Checkout.f_GitIn(C, {"config", "remote.origin.pushurl", Empty});
					co_await Checkout.f_GitIn(C, {"config", "--add", "remote.origin.pushurl", OnlyMain});
					auto Mixed = co_await fPlan({"--skip-update", "--repo-name", "Repos/C"});
					co_await Checkout.f_GitIn(C, {"config", "--unset-all", "remote.origin.pushurl"});
					DMibExpect(Mixed.m_ExitCode, !=, 0);
					DMibExpect(Mixed.f_GetCombinedOut().f_Find("have branches and some none"), >=, 0);

					co_return {};
				};

				DMibTestCategory("NothingPushedOnError") -> TCFuture<void>
				{
					// B can be pushed, but the root can't be fast-forwarded, and the root is pushed after B
					co_await Checkout.f_GitIn(C, {"checkout", "-q", "main"});
					CStr BAhead = co_await Checkout.f_CommitIn(B, "BAhead");
					CStr RemoteB = co_await fRemoteRef(B, "refs/heads/main");
					co_await fRewrite(Repo.m_Path);

					auto Result = co_await Checkout.f_Mib({"push"});
					DMibExpect(Result.m_ExitCode, !=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Nothing was pushed"), >=, 0);
					DMibExpect(Result.f_GetCombinedOut().f_Find("Cannot fast forward to origin/main"), >=, 0);

					CStr RemoteBAfter = co_await fRemoteRef(B, "refs/heads/main");
					DMibExpect(RemoteBAfter, ==, RemoteB);
					DMibExpect(RemoteBAfter.f_StartsWith(BAhead), ==, false);

					co_return {};
				};

				co_return {};
			};
		}
	};

	DMibTestRegister(CPushPlan_Tests, Malterlib::Tool);
}
