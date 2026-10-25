// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Container/LinkedList>
#include <Mib/Develop/CodeFormatting>
#include <Mib/Develop/EditorConfig>
#include <Mib/Function/Function>
#include <Mib/Git/Ignore>

namespace NMib::NTool::NFormat
{
	enum class EFormatMode
	{
		mc_Write    // Rewrite the file when the plan is not empty.
		, mc_Check  // Report violations without writing.
		, mc_Diff   // Print the proposed patch without writing.
		, mc_Report // Report violations for a caller-supplied snapshot; never touches the filesystem.
	};

	enum class EFormatOutcome
	{
		mc_Excluded
		, mc_Unchanged
		, mc_Changed
		, mc_Failed
	};

	struct CFormatJob
	{
		NStr::CStr m_Path;                                                                 // Absolute path, used for reading, writing, and diagnostics.
		NStr::CStr m_DisplayPath;                                                          // Repository-relative path used in patch headers.
		NStr::CStr m_Root;                                                                 // Bounds configuration discovery when the worker resolves the settings.
		NDevelop::CCodeFormattingSettings m_Settings;
		bool m_bResolveSettings = false;                                                   // The worker resolves the settings and display path within m_Root.
		bool m_bValidateLineLength = false;                                                // Also check max_line_length, on a file the engine does not format too.
		NGit::EGitTextAttribute m_TextAttribute = NGit::EGitTextAttribute::mc_Unspecified; // Decides text or binary before git's heuristic.
		NStr::CStr m_Source;                                                               // Snapshot contents; empty means read m_Path.
		bool m_bHasSource = false;
		NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const> m_pStyle;          // The snapshot's style; the worker resolves one unless m_bHasStyle.
		bool m_bHasStyle = false;
		NContainer::TCVector<umint> m_ReportedLines;                                       // Sorted one-based lines to report; empty reports every line.
		NContainer::TCVector<NDevelop::CCodeFormattingRange> m_ByteRanges;
		umint m_iFirstLine = 0;                                                            // One-based and inclusive; zero means no line selection.
		umint m_iLastLine = 0;
		NDevelop::ECodeRangePolicy m_RangePolicy = NDevelop::ECodeRangePolicy::mc_Expand;
		EFormatMode m_Mode = EFormatMode::mc_Write;
		NStorage::TCSharedPointer<NAtomic::TCAtomic<bool>> m_pCancelled;                   // Once set, the file is left unwritten.
		NStorage::TCSharedPointer<NAtomic::TCAtomic<bool>> m_pStopped;                     // The run's own m_pCancelled, set when its selection fails.
		NStr::CStr m_PatchRoot;                                                            // With m_bResolveSettings, what patch paths are relative to, when not m_Root.

		bool f_IsCancelled() const;
	};

	struct CFormatJobResult
	{
		NStr::CStr m_Path;
		EFormatOutcome m_Outcome = EFormatOutcome::mc_Unchanged;
		NStr::CStr m_Report;       // Diagnostics for stderr.
		NStr::CStr m_Patch;        // Unified diff for stdout.
		umint m_nReported = 0;     // Diagnostics that survived the reporting mask.
		umint m_nUnresolved = 0;   // Reported diagnostics without an automatic fix.
		umint m_nLineErrors = 0;   // Lines over max_line_length, in a file the engine did not format.
		bool m_bFormatted = false; // The engine analyzed the file.
		fp64 m_Seconds = 0.0;      // The time the engine spent analyzing the file.
	};

	// Resolves the style a directory's files are formatted with: the nearest
	// '.malterlib-format' in the directory or above it, or where none is found,
	// 'Malterlib/Core/.malterlib-format' in the nearest directory above that holds one. The
	// document a file extends is applied under it. Each directory is looked up and each file
	// read once per run, and a directory without either reads no name as more than C++ does.
	// The documents a snapshot of a repository holds, by absolute path: a document inside m_Root is read from them, and one
	// missing from them is missing whatever the working tree holds; one outside it is read from disk.
	struct CFormatStyleSnapshot
	{
		NStr::CStr m_Root;
		NContainer::TCMap<NStr::CStr, NStr::CStr> m_Documents;
		NContainer::TCMap<NStr::CStr, NStr::CStr> m_Blobs; // The snapshot's other regular files, by absolute path, to their blobs, for a document extended.
		NStr::CStr m_GitDirectory;                         // Where git reads the blobs, with m_GitEnvironment.
		NMib::CSystemEnvironment m_GitEnvironment;
	};

	struct CFormatStyleResolver : NConcurrency::CActor
	{
		explicit CFormatStyleResolver
			(
				NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> const &_pBlockingActors
				, NStorage::TCSharedPointer<CFormatStyleSnapshot> const &_pSnapshot = {}
			)
		;

		NConcurrency::TCFuture<NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const>> f_Resolve(NStr::CStr _Directory);

	private:
		struct CLoad
		{
			NConcurrency::TCAsyncResult<NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const>> m_Result;
			NContainer::TCVector<NConcurrency::TCPromise<NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const>>> m_Waiters;
			NStr::CStr m_Extending; // The base this pending load waits on.
		};

		NConcurrency::TCFuture<NStr::CStr> fp_FindDocument(NStr::CStr _Directory, NStr::CStr _Name);
		NStr::CStr const *fp_FindInSnapshot(NStr::CStr const &_Path, bool &o_bInSnapshot) const;
		NConcurrency::TCFuture<NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const>> fp_Load(NStr::CStr _Path, NContainer::TCVector<NStr::CStr> _Chain);
		NConcurrency::TCFuture<NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const>> fp_Read(NStr::CStr _Path, NContainer::TCVector<NStr::CStr> _Chain);

		NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> mp_pBlockingActors;
		NStorage::TCSharedPointer<CFormatStyleSnapshot> mp_pSnapshot;
		NContainer::TCMap<NStr::CStr, NStr::CStr> mp_Documents;                   // Directory to the document it takes; empty for none.
		NContainer::TCMap<NStr::CStr, NStr::CStr> mp_Fallbacks;                   // Directory to the fallback document above it; empty for none.
		NContainer::TCMap<NStr::CStr, NStorage::TCSharedPointer<CLoad>> mp_Loads; // Document path to its style, loaded or loading.
	};

	// Analyzes on the concurrency manager's high CPU actors, one analysis per actor at a time, each started on an actor that
	// is idle: an actor runs its queue in order, so an analysis dealt to one in turn would wait behind any long one in it.
	struct CFormatAnalysis
	{
		NDevelop::CCodeFormattingResult m_Result;
		fp64 m_Seconds = 0.0; // The time the engine spent, not counting the wait for an actor.
	};

	struct CFormatAnalyzers : NConcurrency::CActor
	{
		explicit CFormatAnalyzers(umint _nMaxConcurrent);

		NConcurrency::TCFuture<CFormatAnalysis> f_Analyze(NDevelop::CCodeFormattingRequest _Request);

	protected:
		NConcurrency::TCFuture<void> fp_Destroy() override;

	private:
		struct CQueuedAnalysis
		{
			NDevelop::CCodeFormattingRequest m_Request;
			NConcurrency::TCPromise<CFormatAnalysis> m_Promise;
		};

		NConcurrency::TCFuture<CFormatAnalysis> fp_Run(umint _iActor, NDevelop::CCodeFormattingRequest _Request);

		NContainer::TCVector<NConcurrency::TCActor<NConcurrency::CConcurrentActor>> mp_Actors;
		NContainer::TCVector<umint> mp_Idle;                // Indices into mp_Actors.
		NContainer::TCLinkedList<CQueuedAnalysis> mp_Queue; // Waiting for an idle actor, in the order they came.
		bool mp_bDestroying = false;                        // No queued analysis starts once the analyzers are being destroyed.
	};

	// Workers share no state but the blocking actors their file I/O runs on and the analyzers: the caller owns selection,
	// ordering, and reporting. A worker resolves its jobs' settings within their roots, so configuration runs on every core
	// along with the formatting.
	struct CFormatWorker : NConcurrency::CActor
	{
		CFormatWorker
			(
				NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> const &_pBlockingActors
				, NConcurrency::TCActor<CFormatStyleResolver> const &_StyleResolver
				, NConcurrency::TCActor<CFormatAnalyzers> const &_Analyzers
			)
		;

		NConcurrency::TCFuture<CFormatJobResult> f_Process(CFormatJob _Job);

	protected:
		NConcurrency::TCFuture<void> fp_Destroy() override;

	private:
		NContainer::TCMap<NStr::CStr, NConcurrency::TCActor<NDevelop::CEditorConfigResolver>> mp_Configurations;  // One resolver per root.
		NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> mp_pBlockingActors;
		NConcurrency::TCActor<CFormatStyleResolver> mp_StyleResolver;                                             // Shared by the pool's workers.
		NConcurrency::TCActor<CFormatAnalyzers> mp_Analyzers;                                                     // Shared by the pool's workers.
		NContainer::TCMap<NStr::CStr, NStorage::TCSharedPointer<NDevelop::CCodeFormattingStyle const>> mp_Styles; // Directory to its style, so a worker asks once.
	};

	// The workers of a run, taking jobs in turn, and the blocking actors their file I/O
	// runs on. Several validations share one pool so that their jobs share both.
	struct CFormatWorkerPool : NConcurrency::CActor
	{
		CFormatWorkerPool(umint _nWorkers, NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> const &_pBlockingActors);

		NConcurrency::TCFuture<CFormatJobResult> f_Process(CFormatJob _Job);

	protected:
		NConcurrency::TCFuture<void> fp_Destroy() override;

	private:
		struct CQueuedJob
		{
			CFormatJob m_Job;
			NConcurrency::TCPromise<CFormatJobResult> m_Promise;
		};

		NConcurrency::TCFuture<CFormatJobResult> fp_Run(umint _iWorker, CFormatJob _Job);
		umint fp_FindFreeWorker() const;
		void fp_StartQueued();

		NContainer::TCVector<NConcurrency::TCActor<CFormatWorker>> mp_Workers;
		NContainer::TCVector<umint> mp_RunningJobs;    // Per worker, the jobs it has in hand.
		NContainer::TCLinkedList<CQueuedJob> mp_Queue; // The jobs no worker has room for yet, in the order they came.
		bool mp_bDestroying = false;                   // No queued job starts once the pool is being destroyed.
		NConcurrency::TCActor<CFormatStyleResolver> mp_StyleResolver;
		NConcurrency::TCActor<CFormatAnalyzers> mp_Analyzers;
	};

	// A pool with its own blocking actors, for a caller that runs no walk of its own.
	NConcurrency::TCActor<CFormatWorkerPool> fg_ConstructFormatWorkerPool(umint _nWorkers);

	// Resolves the repository root that bounds configuration discovery for a directory. A
	// directory asks git only when it holds a '.git' entry of its own or nothing above it is
	// known; otherwise it takes the root of the nearest known directory above it. A directory
	// whose root is being resolved is asked once, and later callers wait for that answer, so
	// every file's resolve can be issued at once.
	struct CFormatRootResolver : NConcurrency::CActor
	{
		explicit CFormatRootResolver(NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> const &_pBlockingActors);

		NConcurrency::TCFuture<NStr::CStr> f_Resolve(NStr::CStr _Directory);

	private:
		struct CEntry
		{
			NConcurrency::TCAsyncResult<NStr::CStr> m_Result;
			NContainer::TCVector<NConcurrency::TCPromise<NStr::CStr>> m_Waiters;
		};

		NConcurrency::TCFuture<NStr::CStr> fp_AskGit(NStr::CStr _Directory);

		NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> mp_pBlockingActors;
		NContainer::TCMap<NStr::CStr, NStorage::TCSharedPointer<CEntry>> mp_Entries;
	};

	// Runs the jobs over the pool and returns their results in job order, so a parallel
	// run reports exactly like a single-job run; a job that failed is a failed result.
	NConcurrency::TCFuture<NContainer::TCVector<CFormatJobResult>> fg_RunFormatJobs(NConcurrency::TCActor<CFormatWorkerPool> _Workers, NContainer::TCVector<CFormatJob> _Jobs);

	// A bounded default for hosts that did not ask for a specific job count.
	umint fg_GetDefaultFormatJobs();

	struct CFormatOptions
	{
		EFormatMode m_Mode = EFormatMode::mc_Write;
		umint m_nJobs = 1;
		bool m_bValidateLineLength = false;                              // Audit: also check max_line_length, and exclude binary files.
		bool m_bRequireFiles = true;                                     // No file selected is an error.
		umint m_iFirstLine = 0;
		umint m_iLastLine = 0;
		NContainer::TCVector<NDevelop::CCodeFormattingRange> m_ByteRanges;
		NDevelop::ECodeRangePolicy m_RangePolicy = NDevelop::ECodeRangePolicy::mc_Expand;
		umint m_nSlowest = 0;                                            // How many of the files that took the longest the run reports.
		NStorage::TCSharedPointer<NAtomic::TCAtomic<bool>> m_pCancelled; // Once set, no further file is scheduled or written.
		NStr::CStr m_PatchRoot;                                          // Patch paths are relative to it; empty: the file's repository, or the working directory holding it.
	};

	struct CFormatSummary
	{
		umint m_nSelected = 0;
		umint m_nExcluded = 0;
		umint m_nUnchanged = 0;
		umint m_nChanged = 0;
		umint m_nUnresolved = 0;
		umint m_nFailed = 0;
		umint m_nLineErrors = 0;
		umint m_nFormatFiles = 0; // Files the engine analyzed.
		umint m_nReported = 0;    // Diagnostics the engine reported for them.
	};

	// Where a run writes as it goes: diagnostics and the summary line, and the patches of
	// a diff run. The tool binds these to its command line; a run made on behalf of another
	// command collects them.
	struct CFormatSink
	{
		NFunction::TCFunctionMovable<void (NStr::CStr const &_Text)> m_fReport;
		NFunction::TCFunctionMovable<void (NStr::CStr const &_Text)> m_fPatch;
	};

	struct CFormatRunResult
	{
		CFormatSummary m_Summary;
		uint32 m_ExitCode = 0; // One when violations remain or a check would change a file.
	};

	// The files and patterns named against one working directory.
	struct CFormatSelection
	{
		NStr::CStr m_WorkingDirectory;
		NContainer::TCVector<NStr::CStr> m_Files;
		NContainer::TCVector<NStr::CStr> m_Patterns;
		bool m_bRecursive = false;
		bool m_bNestedRepositories = false; // A recursive walk also enters the repositories checked out where the one around them ignores them.
		bool m_bOwnRepository = false;      // A recursive walk enters no repository below the one it starts in, ignored or not.
	};

	// One run over every selection: the trees are walked in parallel, a selection's files
	// are formatted as soon as it is walked, and the diagnostics cover them all in path
	// order. The caller reports the summary, timed from wherever its work began. A failure
	// to read, write, or format a file is an error, distinct from a violation.
	NConcurrency::TCFuture<CFormatRunResult> fg_RunFormat(NContainer::TCVector<CFormatSelection> _Selections, CFormatOptions _Options, CFormatSink _Sink);
	NStr::CStr fg_DescribeFormatSummary(CFormatSummary const &_Summary, EFormatMode _Mode, fp64 _Seconds);

	// The max_line_length check of one line, appending its diagnostic to the report; false
	// when the line is over the limit. NUL occupies one column, like space, and is
	// normalized before splitting, whose substring constructors treat a leading NUL as empty.
	bool fg_ValidateLineLength(NStr::CStr const &_Line, NStr::CStr const &_AbsolutePath, umint _LineNumber, NDevelop::CCodeFormattingSettings const &_Settings, NStr::CStr &o_Report);

	struct CPlainFormatDirectives
	{
		bool f_IsDisabled(NStr::CStr const &_Line, NStr::CStr const &_AbsolutePath, umint _LineNumber, NStr::CStr &o_Report, umint &o_nErrors);
		void f_Finish(NStr::CStr const &_AbsolutePath, NStr::CStr &o_Report, umint &o_nErrors);

		umint m_iOffLine = 0; // The line that turned the check off, or zero while it is on
	};

	void fg_NormalizeValidationNuls(NStr::CStr &_Text);
	NStr::CStr fg_StrFromBytes(void const *_pData, umint _nBytes);
}
