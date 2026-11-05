// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Main.h"

#ifdef DPlatformFamily_Windows

#include <Windows.h>
#include <Tlhelp32.h>

struct CTool_KillAllTree : public CTool
{
	struct CProcessEntry
	{
		~CProcessEntry();
		CProcessEntry();

		bool operator == (uint32 _Process) const noexcept;
		bool operator == (CStr const &_Process) const noexcept;
		void f_MapProcess(uint32 _ID, uint32 _ParentID, CWStr const &_FileName);
		bool f_FindParent(uint32 _Parent);
		void f_KillTree(int32 _Depth, TCVector<CStr> &_ProcessNames);
		void f_TraceTree(int32 _Depth = 0);

		CProcessEntry *m_pParent;
		CStr m_FileName;
		uint32 m_Process;
		uint32 m_ParentID;
		DLinkDS_Link(CProcessEntry, m_Link);
		DLinkDS_List(CProcessEntry, m_Link) m_Children;

		DLinkDS_Link(CProcessEntry, m_LinkAll);
		DLinkDS_List(CProcessEntry, m_LinkAll) m_AllProcess;
		using CAllIter = DLinkDS_Iter(CProcessEntry, m_LinkAll); // NOLINT(malterlib-member-order): it uses the translator type the m_LinkAll link macro declares, so it comes after it
	};

	void f_TerminateProcessTree(TCVector<CStr> &_ProcessNames, uint32 _ParentOf);
	aint f_Run(NContainer::CRegistry &_Params);
};

CTool_KillAllTree::CProcessEntry::~CProcessEntry()
{
	m_AllProcess.f_DeleteAllDefiniteType();
}

CTool_KillAllTree::CProcessEntry::CProcessEntry()
{
	m_pParent = nullptr;
	m_ParentID = 0;
}

bool CTool_KillAllTree::CProcessEntry::operator == (uint32 _Process) const noexcept
{
	return m_Process == _Process;
}

bool CTool_KillAllTree::CProcessEntry::operator == (CStr const &_Process) const noexcept
{
	return m_FileName == _Process;
}

void CTool_KillAllTree::CProcessEntry::f_MapProcess(uint32 _ID, uint32 _ParentID, CWStr const &_FileName)
{
	CProcessEntry *pEntry = m_AllProcess.f_Find(_ID);

	if (!pEntry)
	{
		pEntry = DNew CProcessEntry;
		pEntry->m_Process = _ID;
		m_AllProcess.f_Insert(pEntry);
	}
	pEntry->m_FileName = CStr(_FileName);
	pEntry->m_ParentID = _ParentID;

	CProcessEntry *pParent = m_AllProcess.f_Find(_ParentID);

	if (pParent != pEntry)
	{
		if (!pParent)
		{
			pParent = DNew CProcessEntry;
			pParent->m_Process = _ParentID;
			m_AllProcess.f_Insert(pParent);
			CProcessEntry *pParent2 = m_AllProcess.f_Find(0);
			if (!pParent2)
			{
				pParent2 = DNew CProcessEntry;
				pParent2->m_Process = 0;
				m_AllProcess.f_Insert(pParent2);
			}
			pParent->m_pParent = pParent2;
			pParent2->m_Children.f_Insert(pParent);
		}
		pEntry->m_pParent = pParent;
		pParent->m_Children.f_Insert(pEntry);
	}
	else
		m_Children.f_Insert(pEntry);
}

bool CTool_KillAllTree::CProcessEntry::f_FindParent(uint32 _Parent)
{
	if (m_ParentID == _Parent)
		return true;
	if (m_pParent)
		return m_pParent->f_FindParent(_Parent);
	return false;
}

void CTool_KillAllTree::CProcessEntry::f_KillTree(int32 _Depth, TCVector<CStr> &_ProcessNames)
{
	HANDLE pProcess = OpenProcess(PROCESS_TERMINATE, false, m_Process);
	if (pProcess)
	{
		umint nProcesses = _ProcessNames.f_GetLen();
		bool bFound = false;
		for (umint iName = 0; iName < nProcesses; ++iName)
		{
			if (m_FileName.f_CmpNoCase(_ProcessNames[iName]) == 0)
			{
				bFound = true;
				break;
			}
		}

		if (bFound)
		{
			TerminateProcess(pProcess, 44);
			DConOut("{sf ,sj*}Killed process id {}: {} Parent: {}" DNewLine, "", _Depth * 4, m_Process, m_FileName, m_ParentID);
		}
		else
			DConOut("{sf ,sj*}Did NOT kill process id {}: {} Parent: {}" DNewLine, "", _Depth * 4, m_Process, m_FileName, m_ParentID);
		CloseHandle(pProcess);
	}
	CProcessEntry *pEntry = m_Children.f_Pop();
	while (pEntry)
	{
		pEntry->f_KillTree(_Depth + 1, _ProcessNames);
		pEntry = m_Children.f_Pop();
	}
	delete this;
}

void CTool_KillAllTree::CProcessEntry::f_TraceTree(int32 _Depth)
{
	DConOut("{sf ,sj*}Process id {}: {} Parent: {}" DNewLine, "", _Depth * 4, m_Process, m_FileName, m_ParentID);
	DLinkDS_Iter(CProcessEntry, m_Link) iChild = m_Children;
	while (iChild)
	{
		iChild->f_TraceTree(_Depth + 1);

		++iChild;
	}
}

void CTool_KillAllTree::f_TerminateProcessTree(TCVector<CStr> &_ProcessNames, uint32 _ParentOf)
{
	{
		CProcessEntry RootProcess;
		HANDLE pProcessSnapshot;
		PROCESSENTRY32 ProcessEntry;

		// Take a snapshot of all processes in the system.
		pProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (pProcessSnapshot != INVALID_HANDLE_VALUE)
		{
			ProcessEntry.dwSize = sizeof(PROCESSENTRY32);

			if (Process32First(pProcessSnapshot, &ProcessEntry))
			{
				do
				{
					RootProcess.f_MapProcess(ProcessEntry.th32ProcessID, ProcessEntry.th32ParentProcessID, ProcessEntry.szExeFile);
				}
				while (Process32Next(pProcessSnapshot, &ProcessEntry));
			}

			CloseHandle(pProcessSnapshot);
		}

	RestartSearch:
		{
			CProcessEntry::CAllIter iProcess = RootProcess.m_AllProcess;
			while (iProcess)
			{
				bool bFound = false;

				umint nProces = _ProcessNames.f_GetLen();

				for (umint iName = 0; iName < nProces; ++iName)
				{
					if (iProcess->m_FileName == _ProcessNames[iName])
					{
						bFound = true;
						break;
					}
				}

				if (bFound)
				{
					iProcess->f_KillTree(0, _ProcessNames);
					goto RestartSearch;
				}
				++iProcess;
			}
		}
	}
}

aint CTool_KillAllTree::f_Run(NContainer::CRegistry &_Params)
{
	TCVector<CStr> ProcessNames;
	aint iParam = 0;
	while (_Params.f_GetValue(CStr::fs_ToStr(iParam), "NotExist") != "NotExist")
	{
		ProcessNames.f_Insert(_Params.f_GetValue(CStr::fs_ToStr(iParam), "NotExist"));
		++iParam;
	}
	f_TerminateProcessTree(ProcessNames, -1);
	DConOut("Done killing processes" DNewLine, 0);
	return 0;
}

DMibRuntimeClass(CTool, CTool_KillAllTree);
#endif
