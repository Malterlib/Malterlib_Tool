// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Malterlib.h"

namespace
{
	bool fg_IsIncludeSpace(ch8 _Char)
	{
		return _Char == ' ' || _Char == '\t' || _Char == '\r' || _Char == '\n' || _Char == '\v' || _Char == '\f';
	}

	CStr fg_ReadInclude(ch8 const *&o_pParse)
	{
		if (*o_pParse != '@')
			return {};

		auto pStart = ++o_pParse;
		while (*o_pParse && *o_pParse != '`' && !fg_IsIncludeSpace(*o_pParse))
			++o_pParse;

		return CStr(pStart, o_pParse - pStart);
	}

	CStr fg_WholeLineInclude(CStr const &_Line)
	{
		auto pParse = _Line.f_GetStr();
		while (fg_IsIncludeSpace(*pParse))
			++pParse;
		for (umint i = 0; i < 3 && *pParse == '`'; ++i)
			++pParse;
		while (fg_IsIncludeSpace(*pParse))
			++pParse;

		auto Path = fg_ReadInclude(pParse);
		if (!Path)
			return {};

		while (fg_IsIncludeSpace(*pParse))
			++pParse;
		for (umint i = 0; i < 3 && *pParse == '`'; ++i)
			++pParse;
		while (fg_IsIncludeSpace(*pParse))
			++pParse;

		return *pParse ? CStr() : Path;
	}

	struct CAgentIncludes
	{
		void f_Add(CStr const &_Path, CStr const &_Parent)
		{
			auto Absolute = CFile::fs_GetExpandedPath(_Path, _Parent);
			if (m_Seen.f_FindEqual(Absolute))
				return;

			m_Seen[Absolute];
			m_Order.f_Insert(fg_Move(Absolute));
		}

		TCVector<CStr> m_Order;
		TCSet<CStr> m_Seen;
	};

	CStr fg_AnnotateInline(CStr const &_Line, CStr const &_Parent, CAgentIncludes &o_Includes, bool _bCode)
	{
		CStr Result;
		auto pStart = _Line.f_GetStr();
		auto pCopy = pStart;
		auto pParse = pStart;
		while (*pParse)
		{
			auto pToken = pParse;
			if (_bCode ? *pParse == '`' && *(pParse + 1) == '@' : *pParse == '@' && (pParse == pStart || *(pParse - 1) != '`'))
			{
				if (_bCode)
					++pParse;

				auto Path = fg_ReadInclude(pParse);
				if (Path && (!_bCode || *pParse == '`'))
				{
					if (_bCode)
						++pParse;

					o_Includes.f_Add(Path, _Parent);
					Result += CStr(pCopy, pParse - pCopy) + " (see below)";
					pCopy = pParse;
					continue;
				}
			}

			pParse = pToken + 1;
		}

		Result += pCopy;

		return Result;
	}

	struct CAgentGenerator
	{
		CStr f_Relative(CStr const &_Path) const
		{
			return CFile::fs_MakePathRelative(_Path, m_Root).f_Replace("\\", "/");
		}

		void f_Expand(CStr const &_Path)
		{
			auto Relative = f_Relative(_Path);
			if (m_Stack.f_Contains(_Path) >= 0)
			{
				m_Output += "<!-- Cyclic include detected: ";
				for (auto const &Path : m_Stack)
					m_Output += Path + " -> ";

				m_Output += _Path + " -->\n";
				return;
			}
			if (m_Visited.f_FindEqual(_Path))
			{
				m_Output += "<!-- Duplicate include skipped: " + Relative + " -->\n";
				return;
			}
			if (!CFile::fs_FileExists(_Path))
			{
				m_Output += "<!-- Missing include: " + Relative + " (resolved to " + _Path + ") -->\n";
				return;
			}

			auto Text = CFile::fs_ReadStringFromFile(_Path, true);
			auto Lines = Text.f_SplitLine();
			auto Parent = CFile::fs_GetPath(_Path);
			CAgentIncludes WholeLines;
			CAgentIncludes Inline;
			m_Stack.f_Insert(_Path);
			m_Output += "<!-- Begin include: " + Relative + " -->\n";

			for (umint i = 0; i < Lines.f_GetLen(); ++i)
			{
				auto const &Line = Lines[i];
				auto Include = fg_WholeLineInclude(Line);
				if (Include)
				{
					WholeLines.f_Add(Include, Parent);
					m_Output += Line + "  (see below)\n";
					continue;
				}

				auto Annotated = fg_AnnotateInline(Line, Parent, Inline, true);
				m_Output += fg_AnnotateInline(Annotated, Parent, Inline, false);
				if (i + 1 < Lines.f_GetLen())
					m_Output += "\n";
			}

			for (auto const &Path : WholeLines.m_Order)
				f_Expand(Path);
			for (auto const &Path : Inline.m_Order)
			{
				if (!WholeLines.m_Seen.f_FindEqual(Path))
					f_Expand(Path);
			}

			m_Stack.f_SetLen(m_Stack.f_GetLen() - 1);
			m_Visited[_Path];
			m_Output += "\n<!-- End include: " + Relative + " -->\n";
		}

		CStr m_Root;
		CStr m_Output;
		TCVector<CStr> m_Stack;
		TCSet<CStr> m_Visited;
	};
}

void CTool_Malterlib::f_Register_UpdateAgents(CDistributedAppCommandLineSpecification::CSection &o_UtilitiesSection)
{
	o_UtilitiesSection.f_RegisterCommand
		(
			{
				"Names"_o= _o["update-agents"]
				, "Description"_o= "Generate AGENTS.md by expanding Markdown @path includes from CLAUDE.md.\n"
				"Includes are resolved relative to their containing file. Whole-line includes precede inline includes.\n"
				"Cycles, duplicates, and missing includes are marked in the output. No repository update is performed.\n"
				, "GlobalOptions"_o= CDistributedAppCommandLineSpecification::fs_RelevantHelpGlobalOptions()
				, "Options"_o=
				{
					"CurrentDirectory?"_o=
					{
						"Names"_o= _o["--current-directory", "-C"]
						, "Default"_o= fs_GetLogicalCurrentDirectory()
						, "Description"_o= "Directory containing the input and output Markdown files."
					}
					, "Input?"_o=
					{
						"Names"_o= _o["--input"]
						, "Default"_o= "CLAUDE.md"
						, "Description"_o= "Input Markdown file, relative to --current-directory."
					}
					, "Output?"_o=
					{
						"Names"_o= _o["--output"]
						, "Default"_o= "AGENTS.md"
						, "Description"_o= "Generated Markdown file, relative to --current-directory."
					}
				}
			}
			, [](CEJsonSorted _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
			{
				auto Capture = co_await (g_CaptureExceptions % "Generating agent instructions");
				auto Root = CFile::fs_GetExpandedPath(_Params["CurrentDirectory"].f_String());
				auto Input = CFile::fs_GetExpandedPath(_Params["Input"].f_String(), Root);
				auto Output = CFile::fs_GetExpandedPath(_Params["Output"].f_String(), Root);
				if (!CFile::fs_FileExists(Input))
				{
					*_pCommandLine %= "Input not found: {}\n"_f << Input;
					co_return 2;
				}

				CAgentGenerator Generator;
				Generator.m_Root = Root;
				Generator.m_Output = "<!-- GENERATED FILE: Do not edit manually. Run ./mib update-agents to regenerate. -->\n\n";
				Generator.f_Expand(Input);
				CFile::fs_WriteStringToFile(Output, Generator.m_Output, false);
				*_pCommandLine %= "Wrote {}\n"_f << Output;

				co_return 0;
			}
		)
	;
}
