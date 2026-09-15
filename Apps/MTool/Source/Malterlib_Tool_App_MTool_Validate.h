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

	// Validates one repository, writing diagnostics through the sink; the caller reports
	// the summary. The changes are the index against HEAD, or the merge base of _Base
	// with HEAD against HEAD when _Base is given; the repository audit covers every
	// tracked text file. Formatting jobs run on the pool given, which repositories
	// validated together share.
	NConcurrency::TCFuture<CValidationResult> fg_ValidateChanges
		(
			NStr::CStr _Directory
			, NStr::CStr _Base
			, NConcurrency::TCActor<NFormat::CFormatWorkerPool> _Workers
			, NFormat::CFormatSink _Sink
		)
	;
	NConcurrency::TCFuture<CValidationResult> fg_ValidateRepository
		(
			NStr::CStr _Directory
			, NConcurrency::TCActor<NFormat::CFormatWorkerPool> _Workers
			, NFormat::CFormatSink _Sink
		)
	;

	// The lines a validation ends with: the failure notice of a changed-line validation
	// with errors, and the summary.
	NStr::CStr fg_DescribeValidationFailure(NStr::CStr const &_Kind, CValidationCounts const &_Counts);
	NStr::CStr fg_DescribeValidationSummary(NStr::CStr const &_Kind, CValidationCounts const &_Counts, fp64 _Seconds);
}
