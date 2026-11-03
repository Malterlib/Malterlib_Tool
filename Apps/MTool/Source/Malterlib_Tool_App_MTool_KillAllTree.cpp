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
		~CProcessEntry()
		{
			m_AllProcess.f_DeleteAllDefiniteType();
		}

		CProcessEntry()
		{
			m_pParent = nullptr;
			m_ParentID = 0;
		}

		CProcessEntry *m_pParent;
		CStr m_FileName;
		uint32 m_Process;
		uint32 m_ParentID;
		DLinkDS_Link(CProcessEntry, m_Link);
		DLinkDS_List(CProcessEntry, m_Link) m_Children;

		DLinkDS_Link(CProcessEntry, m_LinkAll);
		DLinkDS_List(CProcessEntry, m_LinkAll) m_AllProcess;
		using CAllIter = DLinkDS_Iter(CProcessEntry, m_LinkAll);

		bool operator == (uint32 _Process) const noexcept
		{
			return m_Process == _Process;
		}

		bool operator == (CStr const &_Process) const noexcept
		{
			return m_FileName == _Process;
		}

		void f_MapProcess(uint32 _ID, uint32 _ParentID, CWStr const &_FileName)
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

		bool f_FindParent(uint32 _Parent)
		{
			if (m_ParentID == _Parent)
				return true;
			if (m_pParent)
				return m_pParent->f_FindParent(_Parent);
			return false;
		}

		void f_KillTree(int32 _Depth, TCVector<CStr> &_ProcessNames)
		{
			HANDLE Process = OpenProcess(PROCESS_TERMINATE, false, m_Process);
			if (Process)
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
					TerminateProcess(Process, 44);
					DConOut("{sf ,sj*}Killed process id {}: {} Parent: {}" DNewLine, "", _Depth * 4, m_Process, m_FileName, m_ParentID);
				}
				else
					DConOut("{sf ,sj*}Did NOT kill process id {}: {} Parent: {}" DNewLine, "", _Depth * 4, m_Process, m_FileName, m_ParentID);
				CloseHandle(Process);
			}
			CProcessEntry *pEntry = m_Children.f_Pop();
			while (pEntry)
			{
				pEntry->f_KillTree(_Depth + 1, _ProcessNames);
				pEntry = m_Children.f_Pop();
			}
			delete this;
		}

		void f_TraceTree(int32 _Depth = 0)
		{
			DConOut("{sf ,sj*}Process id {}: {} Parent: {}" DNewLine, "", _Depth * 4, m_Process, m_FileName, m_ParentID);
			DLinkDS_Iter(CProcessEntry, m_Link) Iter = m_Children;
			while (Iter)
			{
				Iter->f_TraceTree(_Depth + 1);

				++Iter;
			}
		}
	};

	void f_TerminateProcessTree(TCVector<CStr> &_ProcessNames, uint32 _ParentOf)
	{
		{
			CProcessEntry RootProcess;
			HANDLE ProcessSnapshot;
			PROCESSENTRY32 ProcessEntry;

			// Take a snapshot of all processes in the system.
			ProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (ProcessSnapshot != INVALID_HANDLE_VALUE)
			{
				ProcessEntry.dwSize = sizeof(PROCESSENTRY32);

				if (Process32First(ProcessSnapshot, &ProcessEntry))
				{
					do
					{
						RootProcess.f_MapProcess(ProcessEntry.th32ProcessID, ProcessEntry.th32ParentProcessID, ProcessEntry.szExeFile);
					}
					while (Process32Next(ProcessSnapshot, &ProcessEntry));
				}

				CloseHandle(ProcessSnapshot);
			}

		RestartSearch:
			{
				CProcessEntry::CAllIter Iter = RootProcess.m_AllProcess;
				while (Iter)
				{
					bool bFound = false;

					umint nProces = _ProcessNames.f_GetLen();

					for (umint iName = 0; iName < nProces; ++iName)
					{
						if (Iter->m_FileName == _ProcessNames[iName])
						{
							bFound = true;
							break;
						}
					}

					if (bFound)
					{
						Iter->f_KillTree(0, _ProcessNames);
						goto RestartSearch;
					}
					++Iter;
				}
			}
		}
	}

	aint f_Run(NContainer::CRegistry &_Params)
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
};

DMibRuntimeClass(CTool, CTool_KillAllTree);
#endif
