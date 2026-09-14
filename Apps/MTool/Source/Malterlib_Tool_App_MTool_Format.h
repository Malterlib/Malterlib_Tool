// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Develop/CodeFormatting>
#include <Mib/Develop/EditorConfig>

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
		NDevelop::CCodeFormattingSettings m_Settings;
		bool m_bResolveSettings = false;							// The worker resolves the settings and display path from its root.
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

	// Workers share no state: the caller owns selection, ordering, and reporting. A worker
	// given a root resolves its own jobs' settings there, so configuration runs on every
	// core along with the formatting.
	struct CFormatWorker : NConcurrency::CActor
	{
		explicit CFormatWorker(NStr::CStr _Root);

		NConcurrency::TCFuture<CFormatJobResult> f_Process(CFormatJob _Job);

	protected:
		NConcurrency::TCFuture<void> fp_Destroy() override;

	private:
		NStr::CStr mp_Root;
		NConcurrency::TCActor<NDevelop::CEditorConfigResolver> mp_Configurations;
		NConcurrency::CBlockingActorCheckout mp_BlockingActor;	// One per worker: the worker's file I/O queues here instead of each job checking one out.
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
	NConcurrency::TCFuture<NContainer::TCVector<CFormatJobResult>> fg_RunFormatJobs(NContainer::TCVector<CFormatJob> _Jobs, umint _nJobs, NStr::CStr _Root = {});

	// A bounded default for hosts that did not ask for a specific job count.
	umint fg_GetDefaultFormatJobs();
}
