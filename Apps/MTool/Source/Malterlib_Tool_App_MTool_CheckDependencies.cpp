// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Concurrency/ParallellForEach>

namespace NMib
{
	namespace NStr
	{
		template <typename tf_CStr, typename tf_FOnLine>
		void fg_StrForEachLine(tf_CStr const& _Str, tf_FOnLine && _fOnLine)
		{
			using CStrPtrType = typename NMib::NStr::TCStrPtrFromCharType<typename tf_CStr::CChar>::CType;
			auto const* pParse = _Str.f_GetStr();
			while (*pParse)
			{
				auto pStart = pParse;
				fg_ParseToEndOfLine(pParse);
				auto pEnd = pParse;
				fg_ParseEndOfLine(pParse);
				CStrPtrType StrPtr;
				StrPtr.f_SetConstPtr(pStart, pEnd - pStart);
				_fOnLine(StrPtr);
			}
		}
	}
}

class CTool_CheckDependencies : public CTool2
{
public:

	struct CDirectory
	{
		CStr m_Path;
		CStr m_Pattern;
		EFileAttrib m_Attributes;
		bool m_bRecursive;
		bool m_bFollowLinks;
		CTime m_Timestamp;
		TCVector<CStr> m_Excluded;
		TCVector<CStr> m_FoundFiles;
	};

	struct CDependencyFile
	{
		CStr m_Path;
		CTime m_Timestamp;
		CHashDigest_MD5 m_Digest;
		bool m_bDigest = false;
	};

	struct CDependency
	{
		TCVector<CStr> m_Outputs;
		TCVector<CDirectory> m_Directories;
		TCVector<CDependencyFile> m_Files;

		void f_NeedsUpdating() const
		{
			for (auto & File : m_Outputs)
			{
				if (CFile::fs_FileExists(File))
					CFile::fs_DeleteFile(File);
			}
		}
	};

	TCThreadLocal<TCMap<CStr, NTime::CTime>> m_WriteTimes;

	~CTool_CheckDependencies()
	{
		m_WriteTimes.f_Destroy();
	}

	NTime::CTime f_GetWriteTime(NStr::CStr const& _File)
	{
		auto WriteTime = (*m_WriteTimes)(_File);
		if (!WriteTime.f_WasCreated())
			return *WriteTime;
		*WriteTime = CFile::fs_GetWriteTime(_File);
		return *WriteTime;
	}


	virtual aint f_Run(TCVector<CStr> const &_Files, TCMap<CStr, CStr> const &_Params) override
	{
		CStr const *pOneDir = _Params.f_FindEqual("Directory");
		CClock Clock;
		Clock.f_Start();
		if (_Files.f_GetLen() != 1 && !pOneDir)
			DError("You need to specify ONE file");

		CStr const *pVerbose = _Params.f_FindEqual("Verbose");

		bool bVerbose = pVerbose ? *pVerbose == "true" : false;

		CStr const *pRelative = _Params.f_FindEqual("Relative");
		bool bRelative = pRelative? *pRelative == "true" : false;

		CStr CurrentDir = CFile::fs_GetCurrentDirectory();

		auto fConvertPath = [&](CStr const &_Path)
			{
				if (bRelative)
					return CFile::fs_MakePathRelative(_Path, CurrentDir);
				return _Path;
			}
		;

		TCVector<CStr> Directories;

		if (pOneDir)
		{
			if (CFile::fs_FileExists(*pOneDir, EFileAttrib_Directory))
				Directories.f_Insert(*pOneDir);
		}
		else
		{
			CStr File = _Files[0];
			CStr Contents = CFile::fs_ReadStringFromFile(CStr(File));

			while (!Contents.f_IsEmpty())
			{
				CStr Line = fg_GetStrLineSep(Contents);

				if (Line.f_IsEmpty())
					continue;

				if (Line[Line.f_GetLen()-1] == '/')
					Line = Line.f_Left(Line.f_GetLen() - 1);

				Directories.f_Insert(Line);
			}
		}

		TCVector<CStr> Files;
		for (auto & Dir : Directories)
		{
			auto FoundFiles = CFile::fs_FindFiles(Dir + "/*.MalterlibDependency");
			for (auto & File : FoundFiles)
			{
				Files.f_Insert(File);
			}
		}

		TCAtomic<bool> UsesDigest;

		CMutual DependenciesLock;
		TCVector<CDependency> Dependencies;
		fg_ParallellForEach
			(
				Files
				, [&Dependencies, &DependenciesLock, &UsesDigest](CStr const& _File)
				{
					CStr const& File = _File;

					CDirectory *pLastDirectory = nullptr;
					CDependency Dependency;
					CStr Contents = CFile::fs_ReadStringFromFile(CStr(File));
					fg_StrForEachLine
						(
							Contents
							, [&](CStrPtr const& _Str)
							{
								CStrPtr const& Line = _Str;
								if (Line.f_IsEmpty())
									return;

								if (Line.f_StartsWith("-"))
								{
									if (pLastDirectory)
									{
										CStr Path;
										Path.f_AddStr(Line.f_GetStr() + 1, Line.f_GetLen() - 1);
										pLastDirectory->m_Excluded.f_Insert(fg_GetStrSepEscaped<'\"'>(Path, " "));
									}
									return;
								}
								else if (Line.f_StartsWith("\t"))
								{
									if (pLastDirectory)
									{
										CStr Path;
										Path.f_AddStr(Line.f_GetStr() + 1, Line.f_GetLen() - 1);
										pLastDirectory->m_FoundFiles.f_Insert(fg_GetStrSepEscaped<'\"'>(Path, " "));
									}
									return;
								}
								else
								{
									if (pLastDirectory)
									{
										pLastDirectory->m_FoundFiles.f_Sort();
										pLastDirectory = nullptr;
									}
								}


								if (Line.f_StartsWith("Output "))
								{
									CStr Path;
									Path.f_AddStr(Line.f_GetStr() + 7, Line.f_GetLen() - 7);

									Dependency.m_Outputs.f_Insert(Path);
								}
								else if (Line.f_StartsWith("Directory "))
								{
									auto &Directory = Dependency.m_Directories.f_Insert();
									pLastDirectory = &Directory;

									aint nParsed = 0;
									int32 bRecursive;
									uint64 Seconds;
									uint64 Fraction;
									uint32 Attributes;
									int32 bFollowLinks;
									aint nChars = (CStrPtr::CParse("Directory {} {nfh} {} {nfh} {nfh} ") >> bRecursive >> Attributes >> bFollowLinks >> Seconds >> Fraction).f_Parse(Line, nParsed);

									if (nParsed != 5)
										DError("Invalid 'Directory' entry in dependency file");

									CStr Path;
									Path.f_AddStr(Line.f_GetStr() + nChars, Line.f_GetLen() - nChars);

									Directory.m_Path = fg_GetStrSepEscaped<'\"'>(Path, " ");
									Directory.m_Pattern = fg_GetStrSepEscaped<'\"'>(Path, " ");
									Directory.m_bRecursive = bRecursive;
									Directory.m_Attributes = (EFileAttrib)Attributes;
									Directory.m_bFollowLinks = bFollowLinks;
									Directory.m_Timestamp.f_SetSecondsNoFraction(Seconds);
									Directory.m_Timestamp.f_SetFractionInt(Fraction);
								}
								else if (Line.f_StartsWith("FileDigest "))
								{
									auto &File = Dependency.m_Files.f_Insert();

									aint nParsed = 0;
									uint64 Seconds;
									uint64 Fraction;
									CStr Digest;
									aint nChars = (CStr::CParse("FileDigest {nfh} {nfh} {} ") >> Seconds >> Fraction >> Digest).f_Parse(Line, nParsed);

									if (nParsed != 3)
										DError("Invalid 'FileDigest' entry in dependency file");


									CStr Path;
									Path.f_AddStr(Line.f_GetStr() + nChars, Line.f_GetLen() - nChars);

									File.m_Path = fg_GetStrSepEscaped<'\"'>(Path, " ");
									File.m_Timestamp.f_SetSecondsNoFraction(Seconds);
									File.m_Timestamp.f_SetFractionInt(Fraction);
									File.m_Digest = CHashDigest_MD5::fs_FromString(Digest);
									File.m_bDigest = true;
									UsesDigest.f_Exchange(true);
								}
								else if (Line.f_StartsWith("File "))
								{
									auto &File = Dependency.m_Files.f_Insert();

									aint nParsed = 0;
									uint64 Seconds;
									uint64 Fraction;
									aint nChars = (CStr::CParse("File {nfh} {nfh} ") >> Seconds >> Fraction).f_Parse(Line, nParsed);

									if (nParsed != 2)
										DError("Invalid 'File' entry in dependency file");


									CStr Path;
									Path.f_AddStr(Line.f_GetStr() + nChars, Line.f_GetLen() - nChars);

									File.m_Path = fg_GetStrSepEscaped<'\"'>(Path, " ");
									File.m_Timestamp.f_SetSecondsNoFraction(Seconds);
									File.m_Timestamp.f_SetFractionInt(Fraction);
								}
							}
						)
					;

					if (pLastDirectory)
					{
						pLastDirectory->m_FoundFiles.f_Sort();
						pLastDirectory = nullptr;
					}

					{
						DLock(DependenciesLock);
						Dependencies.f_Insert(fg_Move(Dependency));
					}
				}
			)
		;

		bool bUsesDigest = UsesDigest.f_Load();

		fg_ParallellForEach
			(
				Dependencies
				, [this, bVerbose, bUsesDigest, &fConvertPath](CDependency const& _Dependency)
				{
					CDependency const& Dependency = _Dependency;
					TCAtomic<bool> bNeedsUpdating(false);
					do
					{
//						fg_ParallellForEach // Thread pool implementation is too poor for now
						fg_ForEach
							(
								Dependency.m_Files
								, [this, bVerbose, &bNeedsUpdating](CDependencyFile const& _File)
								{
									CDependencyFile const& File = _File;
									try
									{
										CTime WriteTime = f_GetWriteTime(File.m_Path);

										bool bChanged = File.m_Timestamp != WriteTime;
										if (bChanged && File.m_bDigest)
											bChanged = CFile::fs_GetFileChecksum(File.m_Path) != File.m_Digest;

										if (bChanged)
										{
											if (bVerbose)
												DConOut("Dependency check: File Timestamp ({}): {} != {}\n", File.m_Path << File.m_Timestamp << WriteTime);
											bNeedsUpdating = true;
										}
									}
									catch (CExceptionFile const& _Exception)
									{
										if (bVerbose)
											DConOut("Dependency check: Exception reading file write time({}): {}\n", File.m_Path << _Exception.f_GetErrorStr());
										bNeedsUpdating = true;
									}
								}
							)
						;

						if (bNeedsUpdating)
							break;

						for (auto & Directory : Dependency.m_Directories)
						{
							if (!bUsesDigest)
							{
								if (Directory.m_Timestamp.f_IsValid())
								{
									try
									{
										CTime WriteTime = f_GetWriteTime(Directory.m_Path);

										if (Directory.m_Timestamp != WriteTime)
										{
											if (bVerbose)
												DConOut("Dependency check: Directory Timestamp ({}): {} != {}\n", Directory.m_Path << Directory.m_Timestamp << WriteTime);
											bNeedsUpdating = true;
											break;
										}
									}
									catch (CExceptionFile const& _Exception)
									{
										if (bVerbose)
											DConOut("Dependency check: Exception reading directory write time({}): {}\n", Directory.m_Path << _Exception.f_GetErrorStr());
										bNeedsUpdating = true;
										break;
									}

								}
								else if (CFile::fs_FileExists(Directory.m_Path, EFileAttrib_Directory))
								{
									if (bVerbose)
										DConOut("Dependency check: Directory does exist ({})}\n", Directory.m_Path);
									bNeedsUpdating = true;
									break;
								}
							}

							CFile::CFindFilesOptions Options(CFile::fs_AppendPath(Directory.m_Path, Directory.m_Pattern), Directory.m_bRecursive);

							Options.m_AttribMask = Directory.m_Attributes;
							Options.m_bFollowLinks = Directory.m_bFollowLinks;
							Options.m_ExcludePatterns = Directory.m_Excluded;

							auto FoundFiles = CFile::fs_FindFiles(Options);

							TCVector<CStr> Files;

							for (auto File : FoundFiles)
								Files.f_Insert(fConvertPath(File.m_Path));

							Files.f_Sort();

							if (Files != Directory.m_FoundFiles)
							{
								if (bVerbose)
									DConOut("Dependency check: Found files differ ({}): {} != {}\n", Directory.m_Path << Files.f_GetLen() << Directory.m_FoundFiles.f_GetLen());
								bNeedsUpdating = true;
								break;
							}
						}

						if (bNeedsUpdating)
							break;
					}
					while (false)
						;

					if (bNeedsUpdating)
						Dependency.f_NeedsUpdating();
				}
			)
		;

		if (!pOneDir)
			DConOut("Dependency check: Checked dependencies {fe1} ms\n", Clock.f_GetTime() * 1000.0);

		return 0;
	}
};

DMibRuntimeClass(CTool, CTool_CheckDependencies);
