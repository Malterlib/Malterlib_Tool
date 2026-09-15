// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Tool_App_MTool_Format.h"

namespace NMib::NTool::NValidate
{
	struct CValidationCounts
	{
		umint m_nFiles = 0;
		umint m_nExcluded = 0;
		umint m_nLineErrors = 0;
		umint m_nFormatFiles = 0;
		umint m_nFormatErrors = 0;
		umint m_nFormatFailed = 0;

		umint f_GetErrors() const;
		CValidationCounts &operator += (CValidationCounts const &_Other);
	};

	struct CValidationResult
	{
		NStr::CStr m_Kind;											// What was validated: "staged", "committed", or "tracked text".
		CValidationCounts m_Counts;
	};

	// Validates the changes of one repository, writing diagnostics through the sink; the
	// caller reports the summary. The changes are the index against HEAD, or the merge
	// base of _Base with HEAD against HEAD when _Base is given. Formatting jobs run on the
	// pool given, which repositories validated together share.
	NConcurrency::TCFuture<CValidationResult> fg_ValidateChanges
		(
			NStr::CStr _Directory
			, NStr::CStr _Base
			, NConcurrency::TCActor<NFormat::CFormatWorkerPool> _Workers
			, NFormat::CFormatSink _Sink
		)
	;

	// Audits the working trees holding the directories in one Format run: every text file
	// git does not ignore is checked against max_line_length, and the files that opt in
	// are analyzed by the engine. Diagnostics come in path order.
	NConcurrency::TCFuture<CValidationResult> fg_ValidateRepositories(NContainer::TCVector<NStr::CStr> _Directories, NFormat::CFormatSink _Sink);

	// Whether this process was started by a managed git hook, whose dispatcher names the
	// repository it serves. Git's own variables are not a sign: GIT_INDEX_FILE is also how
	// a caller points the tool at an alternate index.
	bool fg_IsRunningUnderGitHook();

	// The lines a validation ends with: the failure notice of a changed-line validation
	// with errors, and the summary.
	NStr::CStr fg_DescribeValidationFailure(NStr::CStr const &_Kind, CValidationCounts const &_Counts);
	NStr::CStr fg_DescribeValidationSummary(NStr::CStr const &_Kind, CValidationCounts const &_Counts, fp64 _Seconds);
}
