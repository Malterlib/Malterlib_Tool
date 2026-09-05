// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

#include <Mib/Concurrency/AsyncDestroy>

namespace NMib::NTool::NTestHelpers
{
	CStr fg_Repeat(CStr const &_Text, umint _Count)
	{
		CStr Result;
		for (umint i = 0; i < _Count; ++i)
			Result += _Text;

		return Result;
	}

	CRepositoryFixture::CRepositoryFixture()
		: m_Root(CFile::fs_GetProgramDirectory() / "MToolTests" / fg_TestGetCurrentPath().f_RemovePrefix("Malterlib/Tool/"))
		, m_Path(m_Root / "repo")
		, m_Environment(fg_GetSys()->f_Environment())
	{
		DMibRequire(fg_TestGetCurrentPath().f_StartsWith("Malterlib/Tool/"));
		if (CFile::fs_FileExists(m_Root))
			CFile::fs_DeleteDirectoryRecursive(m_Root);
		fg_TestAddCleanupPath(m_Root);

		CFile::fs_CreateDirectory(m_Path);
		for (auto Name : {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX", "GIT_ATTR_SOURCE"})
			m_Environment.f_Remove(Name);

		m_Environment["MToolIsMalterlib"] = "false";
		m_Environment["MToolRootDirectory"] = m_Root / "mtool";
		CFile::fs_CreateDirectory(m_Root / "tmp");
		for (auto Name : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"})
			m_Environment[Name] = m_Root / "tmp";

		m_Environment["GIT_CONFIG_GLOBAL"] = m_Root / "gitconfig";
		CFile::fs_WriteStringToFile
			(
				m_Root / "gitconfig"
				, "[user]\n\tname = MTool Test\n\temail = mtool-test@example.invalid\n"
				"[commit]\n\tgpgsign = false\n[core]\n\tautocrlf = false\n\thooksPath = no-hooks\n"
				, false
			)
		;

		m_ToolDirectory = CFile::fs_GetProgramDirectory() / "TestApps/MTool";
	}

	TCFuture<void> CRepositoryFixture::f_Init()
	{
		auto Capture = co_await (g_CaptureExceptions % "Initializing test repository");
		if (!CFile::fs_FileExists(m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension)))
			co_return DMibErrorInstance("MTool test executable is missing from '{}'; build and deploy Com_Test_Malterlib_Tool"_f << m_ToolDirectory);

		co_await f_Git({"init", "-q", "-b", "main"});
		f_Write(".editorconfig", "root = true\n\n[*]\nmax_line_length = 190\ntab_width = 4\n");

		co_return {};
	}

	void CRepositoryFixture::f_Write(CStr const &_Name, CStr const &_Contents) const
	{
		CStr Path = m_Path / _Name;
		CFile::fs_CreateDirectoryForFile(Path);
		CFile::fs_WriteStringToFile(Path, _Contents, false);
	}

	void CRepositoryFixture::f_WriteBinary(CStr const &_Name) const
	{
		CByteVector Data;
		Data.f_SetLen(501);
		for (auto &Byte : Data)
			Byte = 'X';

		Data[0] = 0;
		CFile::fs_WriteFile(m_Path / _Name, Data);
	}

	TCFuture<void> CRepositoryFixture::f_Stage()
	{
		co_await f_Git({"add", "--all"});

		co_return {};
	}

	TCFuture<void> CRepositoryFixture::f_Commit()
	{
		co_await f_Stage();
		co_await f_Git({"-c", "core.hooksPath=" + (m_Root / "no-hooks"), "commit", "-qm", "fixture"});

		co_return {};
	}

	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CRepositoryFixture::f_Run(CStr _Executable, TCVector<CStr> _Params, CStr _Directory, bool _bMib)
	{
		auto Capture = co_await (g_CaptureExceptions % ("Launching test command '{}'"_f << _Executable));
		CProcessLaunchActor::CSimpleLaunch Params{_Executable, _Params, _Directory, CProcessLaunchActor::ESimpleLaunchFlag_None};
		Params.m_Params.m_Environment = m_Environment;
		Params.m_Params.m_bMergeEnvironment = false;
		if (_bMib)
			Params.m_Params.m_Environment["MToolIsMalterlib"] = "true";

		TCActor<CProcessLaunchActor> Process = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(Process);

		co_return co_await Process(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Params));
	}

	TCFuture<CStr> CRepositoryFixture::f_Git(TCVector<CStr> _Params)
	{
		auto Capture = co_await (g_CaptureExceptions % "Running Git in test repository");
		auto Result = co_await f_Run("git", fg_Move(_Params), m_Path);
		if (Result.m_ExitCode)
			co_return DMibErrorInstance("Git failed: {}"_f << Result.f_GetCombinedOut());

		co_return Result.f_GetStdOut();
	}

	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> CRepositoryFixture::f_Tool(TCVector<CStr> _Params, bool _bMib)
	{
		co_return co_await f_Run(m_ToolDirectory / ("MTool" + CFile::mc_ExecutableExtension), fg_Move(_Params), m_Root, _bMib);
	}

	bool CRepositoryFixture::f_HasTemporaryAttributes() const
	{
		return CFile::fs_FileExists(m_Path / ".git/MToolValidate", EFileAttrib_Directory);
	}

	TCFuture<CStr> CRepositoryFixture::f_Validate(CStr _Case, uint32 _Expected, bool _bStaged, CStr _Base)
	{
		auto Capture = co_await (g_CaptureExceptions % "Validating test repository");
		DMibTestPath(_Case);

		TCVector<CStr> Params = {"Validate", "--no-color", "-C", m_Path};
		if (_Base)
			Params.f_Insert({"--base", _Base});
		else if (_bStaged)
			Params.f_Insert("--staged");

		auto Result = co_await f_Tool(fg_Move(Params));
		auto Output = Result.f_GetCombinedOut();
		DMibExpect(Result.m_ExitCode, ==, _Expected);
		if (Result.m_ExitCode != _Expected)
			co_return DMibErrorInstance(Output);

		DMibExpect(Output.f_Find("Time: "), >=, 0);
		if (Output.f_Find("Time: ") < 0)
			co_return DMibErrorInstance("Validation exited without a summary: {}"_f << Output);

		DMibExpect(Output.f_Find(" s."), >=, 0);

		bool bAbsolutePaths = true;
		for (auto const &Line : Output.f_SplitLine())
		{
			auto iMessage = Line.f_Find(": line length ");
			if (iMessage < 0)
				continue;

			auto pStart = Line.f_GetStr();
			auto pParse = pStart + iMessage;
			while (pParse != pStart && *(pParse - 1) != ':')
				--pParse;

			if (pParse == pStart)
			{
				bAbsolutePaths = false;
				continue;
			}

			CStr File(pStart, pParse - pStart - 1);
			auto Relative = CFile::fs_MakePathRelative(File, m_Path);
			bAbsolutePaths &= CFile::fs_IsPathAbsolute(File) && !CFile::fs_IsPathAbsolute(Relative) && !Relative.f_StartsWith("..");
		}

		DMibExpect(bAbsolutePaths, ==, true);
		DMibExpect(f_HasTemporaryAttributes(), ==, false);

		co_return Output;
	}
}
