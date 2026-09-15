// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Develop/CodeFormatting>
#include <Mib/Develop/EditorConfig>
#include <Mib/Function/Function>

namespace NMib::NTool::NFormat
{
	enum class EFormatMode
	{
		mc_Write			// Rewrite the file when the plan is not empty.
		, mc_Check			// Report violations without writing.
		, mc_Diff			// Print the proposed patch without writing.
		, mc_Report			// Report violations for a caller-supplied snapshot; never touches the filesystem.
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
		NStr::CStr m_Path;											// Absolute path, used for reading, writing, and diagnostics.
		NStr::CStr m_DisplayPath;									// Repository-relative path used in patch headers.
		NStr::CStr m_Root;											// Bounds configuration discovery when the worker resolves the settings.
		NDevelop::CCodeFormattingSettings m_Settings;
		bool m_bResolveSettings = false;							// The worker resolves the settings and display path within m_Root.
		NStr::CStr m_Source;										// Snapshot contents; empty means read m_Path.
		bool m_bHasSource = false;
		NContainer::TCVector<umint> m_ReportedLines;				// Sorted one-based lines to report; empty reports every line.
		NContainer::TCVector<NDevelop::CCodeFormattingRange> m_ByteRanges;
		umint m_iFirstLine = 0;										// One-based and inclusive; zero means no line selection.
		umint m_iLastLine = 0;
		NDevelop::ECodeRangePolicy m_RangePolicy = NDevelop::ECodeRangePolicy::mc_Expand;
		EFormatMode m_Mode = EFormatMode::mc_Write;
	};

	struct CFormatJobResult
	{
		NStr::CStr m_Path;
		EFormatOutcome m_Outcome = EFormatOutcome::mc_Unchanged;
		NStr::CStr m_Report;										// Diagnostics for stderr.
		NStr::CStr m_Patch;											// Unified diff for stdout.
		umint m_nReported = 0;										// Diagnostics that survived the reporting mask.
		umint m_nUnresolved = 0;									// Reported diagnostics without an automatic fix.
	};

	// Workers share no state but the blocking actors their file I/O runs on: the caller owns
	// selection, ordering, and reporting. A worker resolves its jobs' settings within their
	// roots, so configuration runs on every core along with the formatting.
	struct CFormatWorker : NConcurrency::CActor
	{
		explicit CFormatWorker(NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> const &_pBlockingActors);

		NConcurrency::TCFuture<CFormatJobResult> f_Process(CFormatJob _Job);

	protected:
		NConcurrency::TCFuture<void> fp_Destroy() override;

	private:
		NContainer::TCMap<NStr::CStr, NConcurrency::TCActor<NDevelop::CEditorConfigResolver>> mp_Configurations;	// One resolver per root.
		NStorage::TCSharedPointer<NConcurrency::CSharedRoundRobinBlockingActors> mp_pBlockingActors;
	};

	// Resolves the repository root that bounds configuration discovery for a directory. A
	// directory asks git only when it holds a '.git' entry of its own or nothing above it is
	// known; otherwise it takes the root of the nearest known directory above it. A directory
	// whose root is being resolved is asked once, and later callers wait for that answer, so
	// every file's resolve can be issued at once.
	struct CFormatRootResolver : NConcurrency::CActor
	{
		NConcurrency::TCFuture<NStr::CStr> f_Resolve(NStr::CStr _Directory);

	private:
		struct CEntry
		{
			NConcurrency::TCAsyncResult<NStr::CStr> m_Result;
			NContainer::TCVector<NConcurrency::TCPromise<NStr::CStr>> m_Waiters;
		};

		NConcurrency::TCFuture<NStr::CStr> fp_AskGit(NStr::CStr _Directory);

		NContainer::TCMap<NStr::CStr, NStorage::TCSharedPointer<CEntry>> mp_Entries;
	};

	// Runs the jobs over the worker pool and returns their results in job order, so a
	// parallel run reports exactly like a single-job run.
	NConcurrency::TCFuture<NContainer::TCVector<CFormatJobResult>> fg_RunFormatJobs(NContainer::TCVector<CFormatJob> _Jobs, umint _nJobs);

	// A bounded default for hosts that did not ask for a specific job count.
	umint fg_GetDefaultFormatJobs();

	struct CFormatOptions
	{
		EFormatMode m_Mode = EFormatMode::mc_Write;
		umint m_nJobs = 1;
		umint m_iFirstLine = 0;
		umint m_iLastLine = 0;
		NContainer::TCVector<NDevelop::CCodeFormattingRange> m_ByteRanges;
		NDevelop::ECodeRangePolicy m_RangePolicy = NDevelop::ECodeRangePolicy::mc_Expand;
	};

	struct CFormatSummary
	{
		umint m_nSelected = 0;
		umint m_nExcluded = 0;
		umint m_nUnchanged = 0;
		umint m_nChanged = 0;
		umint m_nUnresolved = 0;
		umint m_nFailed = 0;
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
		uint32 m_ExitCode = 0;										// One when violations remain or a check would change a file.
	};

	// The files and patterns named against one working directory.
	struct CFormatSelection
	{
		NStr::CStr m_WorkingDirectory;
		NContainer::TCVector<NStr::CStr> m_Files;
		NContainer::TCVector<NStr::CStr> m_Patterns;
		bool m_bRecursive = false;
	};

	// One run over every selection: the trees are walked in parallel, a selection's files
	// are formatted as soon as it is walked, and the report covers them all in path order.
	// A failure to read, write, or format a file is an error, distinct from a violation.
	NConcurrency::TCFuture<CFormatRunResult> fg_RunFormat(NContainer::TCVector<CFormatSelection> _Selections, CFormatOptions _Options, CFormatSink _Sink);
}
