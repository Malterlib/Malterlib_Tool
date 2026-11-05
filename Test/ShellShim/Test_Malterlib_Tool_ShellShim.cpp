// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The shell shim of Malterlib_Tool.MHeader, for the tests on Windows: runs the shell script next to it that has its name
// without the executable extension, with the shell that MalterlibTestShell names, passing on its arguments, standard handles
// and exit code. Windows only starts executables, so the tests run shell scripts that stand in for tools through copies of it.

#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <wchar.h>

extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void *_pModule, wchar_t *_pFileName, unsigned long _Size);

namespace
{
	// _wspawnv joins the arguments with spaces, so each is quoted as the C runtime of the child splits them again
	wchar_t *fg_QuoteArgument(wchar_t const *_pArgument)
	{
		size_t Length = wcslen(_pArgument);
		wchar_t *pQuoted = (wchar_t *)malloc((Length * 2 + 3) * sizeof(wchar_t));
		if (!pQuoted)
			return nullptr;

		wchar_t *pOut = pQuoted;
		*pOut++ = L'"';
		size_t nBackslashes = 0;
		for (wchar_t const *pIn = _pArgument; *pIn; ++pIn)
		{
			if (*pIn == L'\\')
				++nBackslashes;
			else
			{
				// Backslashes before a quote are doubled, and the quote escaped
				if (*pIn == L'"')
				{
					for (size_t iBackslash = 0; iBackslash < nBackslashes + 1; ++iBackslash)
						*pOut++ = L'\\';
				}
				nBackslashes = 0;
			}
			*pOut++ = *pIn;
		}

		// Backslashes before the closing quote are doubled
		for (size_t iBackslash = 0; iBackslash < nBackslashes; ++iBackslash)
			*pOut++ = L'\\';
		*pOut++ = L'"';
		*pOut = 0;

		return pQuoted;
	}
}

int wmain(int _nArguments, wchar_t **_pArguments)
{
	wchar_t const *pShell = _wgetenv(L"MalterlibTestShell");
	if (!pShell || !*pShell)
		return 127;

	wchar_t Script[32768];
	unsigned long ScriptLength = GetModuleFileNameW(nullptr, Script, 32768);
	if (ScriptLength == 0 || ScriptLength >= 32768)
		return 127;

	wchar_t *pExtension = wcsrchr(Script, L'.');
	wchar_t *pSeparator = wcsrchr(Script, L'\\');
	if (pExtension && (!pSeparator || pExtension > pSeparator))
		*pExtension = 0;

	wchar_t const **pArguments = (wchar_t const **)malloc(sizeof(wchar_t *) * (_nArguments + 2));
	if (!pArguments)
		return 127;

	pArguments[0] = fg_QuoteArgument(pShell);
	pArguments[1] = fg_QuoteArgument(Script);
	for (int iArgument = 1; iArgument < _nArguments; ++iArgument)
		pArguments[iArgument + 1] = fg_QuoteArgument(_pArguments[iArgument]);
	pArguments[_nArguments + 1] = nullptr;

	for (int iArgument = 0; iArgument < _nArguments + 1; ++iArgument)
	{
		if (!pArguments[iArgument])
			return 127;
	}

	intptr_t ExitCode = _wspawnv(_P_WAIT, pShell, pArguments);
	return ExitCode < 0 ? 127 : (int)ExitCode;
}
