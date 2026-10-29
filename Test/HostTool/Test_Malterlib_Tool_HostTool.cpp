// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdio.h>

// The test host tool of Malterlib_Tool.MHeader: writes the file that its first parameter names. The narrow parameters of
// Windows lose the characters of a path outside the ANSI code page, so it takes them wide there
#ifdef _WIN32
int wmain(int _nArguments, wchar_t **_pArguments)
#else
int main(int _nArguments, char **_pArguments)
#endif
{
	if (_nArguments < 2)
		return 1;

#ifdef _WIN32
	FILE *pFile = _wfopen(_pArguments[1], L"wb");
#else
	FILE *pFile = fopen(_pArguments[1], "wb");
#endif
	if (!pFile)
		return 1;

	bool bWritten = fputs("Written by the test host tool\n", pFile) >= 0;
	bool bClosed = fclose(pFile) == 0;

	return bWritten && bClosed ? 0 : 1;
}
