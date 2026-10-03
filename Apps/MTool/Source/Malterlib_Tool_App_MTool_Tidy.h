// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Tool_App_MTool_Format.h"

namespace NMib::NTool::NTidy
{
	// Which compile commands of the per-target databases are checked.
	enum class ETidyMode
	{
		mc_File				// Each source once, with the command of the best target and configuration.
		, mc_Target			// Every source of the selected targets.
		, mc_AllTargets		// Every source of every target, once per distinct command.
	};

	struct CTidyOptions
	{
		ETidyMode m_Mode = ETidyMode::mc_File;
		NStr::CStr m_ConfigStore;									// The ConfigStore directory of the generated build system.
		NStr::CStr m_ClangTidy;										// The clang-tidy executable.
		NStr::CStr m_ApplyReplacements;								// The clang-apply-replacements executable, for --fix.
		NContainer::TCVector<NStr::CStr> m_Targets;					// Wildcards: the targets checked, or with mc_File, preferred.
		NStr::CStr m_Workspace;										// Wildcard: the workspace checked, or with mc_File, preferred.
		NContainer::TCVector<NStr::CStr> m_Configurations;			// Configurations checked, each by name, base name or file name.
		NStr::CStr m_Checks;										// Added to the checks of the .clang-tidy files.
		NStr::CStr m_Base;											// Only check files changed from the merge base with this to HEAD.
		bool m_bStaged = false;										// Only check files changed in the index.
		bool m_bFix = false;										// Apply the fixes the checks offer.
		umint m_nJobs = 1;
		NContainer::TCVector<NStr::CStr> m_CheckedRepositories;		// The locations of the repositories checked.
		NContainer::TCVector<NStr::CStr> m_AllRepositories;			// Every repository location, which tells what nests in them.
		NStorage::TCSharedPointer<NAtomic::TCAtomic<bool>> m_pCancelled;	// Set when the user aborts: no command starts and the running ones stop.
		bool m_bShowProgress = false;								// Report how many commands are left as they run.
		bool m_bAnsiProgress = false;								// Rewrite one progress line in place, instead of a line now and then.
	};

	struct CTidySummary
	{
		umint m_nCommands = 0;										// Compile commands checked.
		umint m_nFiles = 0;											// Distinct sources checked.
		umint m_nWarnings = 0;
		umint m_nErrors = 0;
		umint m_nFailed = 0;										// Commands clang-tidy could not check.
		umint m_nWithoutChecks = 0;									// Commands of sources that no .clang-tidy enables a check for.
		umint m_nUncheckedChanged = 0;								// Changed files that no checked command compiles or includes.
		bool m_bFixed = false;										// The fixes were applied.
	};

	struct CTidyRunResult
	{
		CTidySummary m_Summary;
		uint32 m_ExitCode = 0;										// One when a check reports something or a command fails.
	};

	// Runs clang-tidy over the compile commands the build system generated for the sources of the checked
	// repositories, a bounded number at a time. A compile reports its source and the headers of the
	// repository the source is in, but not the headers of repositories nested in it, which their own
	// sources report, as the checks plugin does in a build. Diagnostics are written once each, in path
	// order, through the sink's report.
	NConcurrency::TCFuture<CTidyRunResult> fg_RunTidy(CTidyOptions _Options, NFormat::CFormatSink _Sink);

	NStr::CStr fg_DescribeTidySummary(CTidySummary const &_Summary, fp64 _Seconds);

	// A path as an extended regular expression that matches it literally.
	NStr::CStr fg_EscapeTidyRegex(NStr::CStr const &_Path);

	// The location of the repository a path is in: the deepest of the locations that hold it, or empty.
	NStr::CStr fg_GetTidyRepository(NStr::CStr const &_Path, NContainer::TCVector<NStr::CStr> const &_Locations);
}
