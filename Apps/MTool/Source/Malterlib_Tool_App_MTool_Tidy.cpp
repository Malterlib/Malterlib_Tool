// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_App_MTool_Tidy.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Container/Map>
#include <Mib/Container/Set>
#include <Mib/Cryptography/UUID>
#include <Mib/Git/Helpers/Launch>
#include <Mib/Process/ProcessLaunchActor>

// Actor call arguments and results need externally nameable types, so the command's helpers live in a named
// namespace instead of an anonymous one.
namespace NMib::NTool::NTidy
{
	// A target's compile database for one configuration.
	struct CTidyDatabase
	{
		CStr m_Path;
		CStr m_Workspace;
		CStr m_Target;
		CStr m_Configuration;										// "<Platform> <Architecture> <Configuration>"
		CStr m_FileName;											// The name of the configuration file without its extension.
		int64 m_WorkspacePriority = 0;
		int64 m_ConfigurationPriority = 0;
	};

	// A compile command a run checks.
	struct CTidyCommand
	{
		CStr m_File;												// Condensed absolute path.
		CStr m_Directory;
		CStr m_Entry;												// The database entry, which the run checks alone.
		CStr m_Repository;
		CStr m_Output;												// Condensed absolute path, as Ninja's dependencies name it.
		CStr m_BuildDirectory;										// The Ninja build directory of its target's configuration.
		CStr m_DependencyFile;
		CStr m_Key;													// The source and the arguments that are not outputs.
		TCVector<CStr> m_EquivalentOutputs;							// Of the commands that differ only in their outputs, whose
		TCVector<CStr> m_EquivalentDependencyFiles;					// dependencies count for this one.
		TCVector<CStr> m_EquivalentBuildDirectories;
	};

	struct CTidyJobResult
	{
		CStr m_StdOut;
		CStr m_StdErr;
		uint32 m_ExitCode = 0;
		bool m_bLaunched = false;
		bool m_bWithoutChecks = false;								// No .clang-tidy enables a check for the source.
	};

	struct CTidyState
	{
		CTidyOptions m_Options;
		TCVector<CTidyCommand> m_Commands;
		TCVector<CTidyJobResult> m_Results;
		TCOptional<TCSet<CStr>> m_Changed;
		CStr m_RunDirectory;										// Holds the database of each command and its fixes.
		NFunction::TCFunctionMovable<void (NStr::CStr const &_Text)> m_fProgress;	// The sink's report, which the progress also goes to.

		TCAtomic<umint> m_nDone = 0;
		TCAtomic<umint> m_nRunning = 0;
		CStopwatch m_Stopwatch{true};
		CMutual m_ProgressLock;
		fp64 m_LastProgressTime = 0.0;
		umint m_LastProgressLength = 0;
	};

	struct CTidyInventory
	{
		TCVector<CTidyCommand> m_Commands;
		umint m_nDatabases = 0;
		umint m_nUncheckedChanged = 0;
		umint m_nWithoutDependencies = 0;							// Commands whose includes are not known.
		umint m_nUncertainChanged = 0;								// Changed headers of repositories with such commands.
		TCSet<CStr> m_AllSources;									// The sources of the checked repositories that any database compiles.
		TCVector<CStr> m_UnbuiltChangedSources;						// Changed sources that no database compiles.
		CStr m_Error;												// A selector that matches no database.
	};

	struct CTidyDiagnostic
	{
		CStr m_Path;
		umint m_Line = 0;
		umint m_Column = 0;
		CStr m_Header;												// The diagnostic line after its location.
		CStr m_Text;
		bool m_bError = false;
	};

	CStr fg_EscapeTidyRegex(CStr const &_Path)
	{
		CStr Escaped;
		for (umint i = 0; i < _Path.f_GetLen(); ++i)
		{
			ch8 Char = _Path[i];
			switch (Char)
			{
			case '\\': case '.': case '[': case ']': case '(': case ')': case '{': case '}':
			case '*': case '+': case '?': case '|': case '^': case '$':
				Escaped.f_AddChar('\\');
				break;
			default:
				break;
			}
			Escaped.f_AddChar(Char);
		}

		return Escaped;
	}

	CStr fg_GetTidyRepository(CStr const &_Path, TCVector<CStr> const &_Locations)
	{
		CStr Repository;
		for (auto &Location : _Locations)
		{
			if (Location.f_GetLen() > Repository.f_GetLen() && _Path.f_StartsWith(Location + "/"))
				Repository = Location;
		}

		return Repository;
	}

	// The name of a configuration without the variant in parentheses, as "Release Testing (Tests)" is a
	// variant of "Release Testing".
	CStr fg_GetTidyBaseConfiguration(CStr const &_Configuration)
	{
		if (_Configuration.f_EndsWith(")"))
		{
			aint iOpen = _Configuration.f_FindReverse(" (");
			if (iOpen >= 0)
				return _Configuration.f_Left(iOpen);
		}

		return _Configuration;
	}

	bool fg_MatchTidyWildcard(CStr const &_Value, CStr const &_Wildcard)
	{
		return fg_StrMatchWildcard(_Value.f_GetStr(), _Wildcard.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted;
	}

	bool fg_MatchTidyWildcards(CStr const &_Value, TCVector<CStr> const &_Wildcards)
	{
		for (auto &Wildcard : _Wildcards)
		{
			if (fg_MatchTidyWildcard(_Value, Wildcard))
				return true;
		}

		return false;
	}

	int64 fg_ReadTidyPriority(CStr const &_File, CStr const &_Member)
	{
		if (!CFile::fs_FileExists(_File, EFileAttrib_File))
			return 0;

		auto Json = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(_File, true), _File);
		if (auto pValue = Json.f_GetMember(_Member); pValue && pValue->f_IsInteger())
			return pValue->f_Integer();

		return 0;
	}

	// The databases the build system generated, from the configuration files of the targets.
	TCVector<CTidyDatabase> fg_FindTidyDatabases(CTidyOptions const &_Options)
	{
		TCVector<CTidyDatabase> Databases;
		if (!CFile::fs_FileExists(_Options.m_ConfigStore, EFileAttrib_Directory))
			return Databases;

		for (auto &WorkspaceDirectory : CFile::fs_FindFiles(_Options.m_ConfigStore + "/*", EFileAttrib_Directory, false))
		{
			CStr Workspace = CFile::fs_GetFile(WorkspaceDirectory);
			int64 WorkspacePriority = fg_ReadTidyPriority(WorkspaceDirectory / "Workspace.json", "priority");
			if (!CFile::fs_FileExists(WorkspaceDirectory / "Targets", EFileAttrib_Directory))
				continue;

			TCMap<CStr, int64> ConfigurationPriorities;
			if (CFile::fs_FileExists(WorkspaceDirectory / "Configs", EFileAttrib_Directory))
			{
				for (auto &File : CFile::fs_FindFiles(WorkspaceDirectory / "Configs/*.json", EFileAttrib_File, false))
					ConfigurationPriorities[CFile::fs_GetFileNoExt(File)] = fg_ReadTidyPriority(File, "configurationPriority");
			}

			for (auto &TargetDirectory : CFile::fs_FindFiles(WorkspaceDirectory / "Targets/*", EFileAttrib_Directory, false))
			{
				if (!CFile::fs_FileExists(TargetDirectory / "Configs", EFileAttrib_Directory))
					continue;

				for (auto &File : CFile::fs_FindFiles(TargetDirectory / "Configs/*.json", EFileAttrib_File, false))
				{
					auto Json = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(File, true), File);
					auto pCompileCommands = Json.f_GetMember("compileCommands");
					if (!pCompileCommands || !pCompileCommands->f_IsString() || !CFile::fs_FileExists(pCompileCommands->f_String(), EFileAttrib_File))
						continue;

					auto fMember = [&](ch8 const *_pName) -> CStr
						{
							auto pValue = Json.f_GetMember(_pName);
							return pValue && pValue->f_IsString() ? pValue->f_String() : CStr();
						}
					;

					auto &Database = Databases.f_Insert();
					Database.m_Path = pCompileCommands->f_String();
					Database.m_Workspace = Workspace;
					// The name of the target's directory, as the name in the file is qualified by its group with
					// some generators.
					Database.m_Target = CFile::fs_GetFile(TargetDirectory);
					Database.m_Configuration = "{} {} {}"_f << fMember("platform") << fMember("architecture") << fMember("configuration");
					Database.m_FileName = CFile::fs_GetFileNoExt(File);
					Database.m_WorkspacePriority = WorkspacePriority;
					if (auto pPriority = ConfigurationPriorities.f_FindEqual(Database.m_FileName))
						Database.m_ConfigurationPriority = *pPriority;
				}
			}
		}

		return Databases;
	}

	// How well a database matches a requested configuration: by its name or file name, then by base name,
	// and when any configuration will do, by priority. Lower is better; none means no match.
	TCOptional<umint> fg_RankTidyConfiguration(CTidyDatabase const &_Database, CStr const &_Requested, bool _bAny)
	{
		if (!_Requested)
			return umint(0);
		if
			(
				fg_MatchTidyWildcard(_Database.m_Configuration, _Requested)
				|| fg_MatchTidyWildcard(_Database.m_FileName, _Requested)
				|| fg_MatchTidyWildcard(_Database.m_FileName + ".json", _Requested)
			)
			return umint(0);
		if (fg_MatchTidyWildcard(fg_GetTidyBaseConfiguration(_Database.m_Configuration), fg_GetTidyBaseConfiguration(_Requested)))
			return umint(1);
		if (_bAny)
			return umint(2);

		return {};
	}

	// The databases checked for one requested configuration: for each target, the best match.
	TCVector<umint> fg_SelectTidyDatabases(TCVector<CTidyDatabase> const &_Databases, CStr const &_Requested, bool _bAny)
	{
		TCMap<CStr, umint> Best;
		TCMap<CStr, umint> BestRank;
		for (umint iDatabase = 0; iDatabase < _Databases.f_GetLen(); ++iDatabase)
		{
			auto &Database = _Databases[iDatabase];
			auto Rank = fg_RankTidyConfiguration(Database, _Requested, _bAny);
			if (!Rank)
				continue;

			CStr Key = "{}/{}"_f << Database.m_Workspace << Database.m_Target;
			auto *pBest = Best.f_FindEqual(Key);
			if (pBest)
			{
				auto &Current = _Databases[*pBest];
				umint CurrentRank = BestRank[Key];
				if (CurrentRank < *Rank)
					continue;
				if (CurrentRank == *Rank)
				{
					if (Current.m_ConfigurationPriority > Database.m_ConfigurationPriority)
						continue;
					if (Current.m_ConfigurationPriority == Database.m_ConfigurationPriority && Current.m_FileName <= Database.m_FileName)
						continue;
				}
			}

			Best[Key] = iDatabase;
			BestRank[Key] = *Rank;
		}

		TCVector<umint> Selected;
		for (auto &iDatabase : Best)
			Selected.f_Insert(iDatabase);

		return Selected;
	}

	bool fg_IsTidyOutputArgument(CStr const &_Argument, bool &o_bSkipNext)
	{
		if (_Argument == "-o" || _Argument == "-MF" || _Argument == "-MT" || _Argument == "-MQ" || _Argument == "--serialize-diagnostics")
		{
			o_bSkipNext = true;
			return true;
		}

		return _Argument == "-MD" || _Argument == "-MMD" || _Argument.f_StartsWith("-MF") || _Argument.f_StartsWith("-MT") || _Argument.f_StartsWith("-MQ")
			|| _Argument.f_StartsWith("/Fo") || _Argument.f_StartsWith("-Fo")
		;
	}

	bool fg_IsTidyHeaderCompile(TCVector<CStr> const &_Arguments, CStr const &_Output)
	{
		if (_Output.f_EndsWith(".pch") || _Output.f_EndsWith(".gch"))
			return true;

		for (umint i = 0; i < _Arguments.f_GetLen(); ++i)
		{
			auto &Argument = _Arguments[i];
			CStr Language;
			if (Argument.f_StartsWith("-x") && Argument.f_GetLen() > 2)
				Language = Argument.f_Extract(2);
			else if (Argument == "-x" && i + 1 < _Arguments.f_GetLen())
				Language = _Arguments[i + 1];

			if (Language.f_EndsWith("-header"))
				return true;
		}

		return false;
	}

	CStr fg_GetTidyDependencyFile(TCVector<CStr> const &_Arguments, CStr const &_Directory)
	{
		for (umint i = 0; i < _Arguments.f_GetLen(); ++i)
		{
			CStr File;
			if (_Arguments[i] == "-MF" && i + 1 < _Arguments.f_GetLen())
				File = _Arguments[i + 1];
			else if (_Arguments[i].f_StartsWith("-MF") && _Arguments[i].f_GetLen() > 3)
				File = _Arguments[i].f_Extract(3);

			if (File)
				return CFile::fs_GetFullPath(File, _Directory);
		}

		return {};
	}

	// The files a make-style dependency file lists after its targets.
	TCVector<CStr> fg_ReadTidyDependencies(CStr const &_File, CStr const &_Directory)
	{
		TCVector<CStr> Dependencies;
		if (!_File || !CFile::fs_FileExists(_File, EFileAttrib_File))
			return Dependencies;

		CStr Contents = CFile::fs_ReadStringFromFile(_File, true);
		CStr Current;
		bool bTargets = true;
		auto fFlush = [&]
			{
				if (Current)
				{
					if (bTargets && Current.f_EndsWith(":"))
						bTargets = false;
					else if (!bTargets)
						Dependencies.f_Insert(CFile::fs_CondensePath(CFile::fs_GetFullPath(Current, _Directory)));
				}
				Current = CStr();
			}
		;

		for (umint i = 0; i < Contents.f_GetLen(); ++i)
		{
			ch8 Char = Contents[i];
			if (Char == '\\' && i + 1 < Contents.f_GetLen())
			{
				ch8 Next = Contents[i + 1];
				if (Next == '\n' || Next == '\r')
				{
					fFlush();
					++i;
					continue;
				}
				if (Next == ' ' || Next == '#' || Next == '\\')
				{
					Current.f_AddChar(Next);
					++i;
					continue;
				}
			}
			if (Char == '$' && i + 1 < Contents.f_GetLen() && Contents[i + 1] == '$')
			{
				Current.f_AddChar('$');
				++i;
				continue;
			}
			if (Char == ' ' || Char == '\t' || Char == '\n' || Char == '\r')
			{
				fFlush();
				continue;
			}
			Current.f_AddChar(Char);
		}
		fFlush();

		return Dependencies;
	}

	bool fg_IsTidyCodeFile(CStr const &_Path)
	{
		// The public headers of a module have no extension, like Include/Mib/String/String.
		CStr Extension = CFile::fs_GetExtension(_Path).f_LowerCase();
		if (!Extension && _Path.f_Find("/Include/") >= 0)
			return true;
		return Extension == "c" || Extension == "cc" || Extension == "cpp" || Extension == "cxx" || Extension == "m" || Extension == "mm"
			|| Extension == "h" || Extension == "hh" || Extension == "hpp" || Extension == "hxx" || Extension == "inl" || Extension == "ipp"
		;
	}

	// The commands a run checks, read from the databases the mode selects.
	CTidyInventory fg_ReadTidyInventory(CTidyOptions const &_Options)
	{
		CTidyInventory Inventory;
		auto Databases = fg_FindTidyDatabases(_Options);
		Inventory.m_nDatabases = Databases.f_GetLen();

		TCSet<CStr> Checked;
		for (auto &Location : _Options.m_CheckedRepositories)
			Checked[Location];

		// The passes of the run: one per requested configuration, or one with any configuration.
		TCVector<CStr> Passes = _Options.m_Configurations;
		if (Passes.f_IsEmpty())
			Passes.f_Insert(CStr());

		// Every source of the checked repositories that any database compiles, also those the selectors leave out,
		// which tells a changed source left out from one no database knows.
		for (auto &Database : Databases)
		{
			auto Json = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(Database.m_Path, true), Database.m_Path);
			if (!Json.f_IsArray())
				continue;
			for (auto &Entry : Json.f_Array())
			{
				auto pFile = Entry.f_GetMember("file");
				auto pDirectory = Entry.f_GetMember("directory");
				if (pFile && pFile->f_IsString() && pDirectory && pDirectory->f_IsString())
					Inventory.m_AllSources[CFile::fs_CondensePath(CFile::fs_GetFullPath(pFile->f_String(), pDirectory->f_String()))];
			}
		}

		TCMap<CStr, TCVector<CEJsonSorted>> ReadDatabases;
		auto fGetEntries = [&](CStr const &_Path) -> TCVector<CEJsonSorted> &
			{
				auto *pEntries = ReadDatabases.f_FindEqual(_Path);
				if (pEntries)
					return *pEntries;

				auto &Entries = ReadDatabases[_Path];
				auto Json = CEJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(_Path, true), _Path);
				if (Json.f_IsArray())
					Entries = fg_Move(Json.f_Array());

				return Entries;
			}
		;

		if (Databases.f_IsEmpty())
			return Inventory;

		// Each selector must match a target with a database, in --workspace when it names one, or the targets it
		// names would go unchecked.
		if (_Options.m_Mode == ETidyMode::mc_Target)
		{
			for (auto &Target : _Options.m_Targets)
			{
				bool bMatched = false;
				for (auto &Database : Databases)
				{
					if (_Options.m_Workspace && !fg_MatchTidyWildcard(Database.m_Workspace, _Options.m_Workspace))
						continue;
					bMatched = bMatched || fg_MatchTidyWildcard(Database.m_Target, Target);
				}
				if (!bMatched)
				{
					Inventory.m_Error = "No compile database belongs to a target that matches --target {}"_f << Target;
					return Inventory;
				}
			}
		}

		TCMap<CStr, umint> SeenKeys;
		for (auto &Requested : Passes)
		{
			// A source can be built by only some configurations of a target, so file mode ranks the commands of
			// every database for each source, and falls back to any configuration.
			bool bAny = _Options.m_Mode == ETidyMode::mc_File;
			TCVector<umint> Selected;
			if (bAny)
			{
				for (umint iDatabase = 0; iDatabase < Databases.f_GetLen(); ++iDatabase)
					Selected.f_Insert(iDatabase);
			}
			else
			{
				for (auto iDatabase : fg_SelectTidyDatabases(Databases, Requested, false))
				{
					auto &Database = Databases[iDatabase];
					if (_Options.m_Mode == ETidyMode::mc_Target && !fg_MatchTidyWildcards(Database.m_Target, _Options.m_Targets))
						continue;
					if (_Options.m_Workspace && !fg_MatchTidyWildcard(Database.m_Workspace, _Options.m_Workspace))
						continue;
					Selected.f_Insert(iDatabase);
				}
				// One target is one database: of the target's workspaces, the one --workspace names, or else the
				// one of the highest priority.
				if (_Options.m_Mode == ETidyMode::mc_Target && !_Options.m_Workspace)
				{
					TCMap<CStr, umint> BestForTarget;
					for (auto iDatabase : Selected)
					{
						auto &Database = Databases[iDatabase];
						auto *piBest = BestForTarget.f_FindEqual(Database.m_Target);
						if (piBest)
						{
							auto &Best = Databases[*piBest];
							if (Best.m_WorkspacePriority > Database.m_WorkspacePriority)
								continue;
							if (Best.m_WorkspacePriority == Database.m_WorkspacePriority && Best.m_Workspace <= Database.m_Workspace)
								continue;
						}
						BestForTarget[Database.m_Target] = iDatabase;
					}

					Selected.f_Clear();
					for (auto iDatabase : BestForTarget)
						Selected.f_Insert(iDatabase);
				}

				// Every target selector still matches a database once the configuration is chosen.
				if (_Options.m_Mode == ETidyMode::mc_Target)
				{
					for (auto &Target : _Options.m_Targets)
					{
						bool bMatched = false;
						for (auto iDatabase : Selected)
							bMatched = bMatched || fg_MatchTidyWildcard(Databases[iDatabase].m_Target, Target);
						if (!bMatched)
						{
							Inventory.m_Error = Requested
								? CStr("No target that matches --target {} has a compile database for the configuration '{}'"_f << Target << Requested)
								: CStr("No target that matches --target {} has a compile database"_f << Target)
							;
							return Inventory;
						}
					}
				}

				if (Selected.f_IsEmpty())
				{
					Inventory.m_Error = Requested
						? CStr("No selected target has a compile database for the configuration '{}'"_f << Requested)
						: CStr("No compile database belongs to a selected target")
					;
					return Inventory;
				}
			}

			// The candidates of a file in this pass, with the rank of their target and workspace.
			struct CCandidate
			{
				CTidyCommand m_Command;
				CTidyDatabase const *m_pDatabase = nullptr;
				umint m_ConfigurationRank = 0;
			};
			TCMap<CStr, CCandidate> BestForFile;
			TCMap<CStr, TCVector<CTidyCommand>> OtherForFile;			// The candidates that lost, whose dependencies count when equivalent.
			TCVector<CTidyCommand> PassCommands;

			auto fRank = [&](CCandidate const &_Candidate)
				{
					auto &Database = *_Candidate.m_pDatabase;
					umint TargetRank = fg_MatchTidyWildcards(Database.m_Target, _Options.m_Targets) ? 0 : 1;
					umint WorkspaceRank = _Options.m_Workspace && fg_MatchTidyWildcard(Database.m_Workspace, _Options.m_Workspace) ? 0 : 1;
					return fg_Tuple
						(
							TargetRank
							, WorkspaceRank
							, _Candidate.m_ConfigurationRank
							, -Database.m_WorkspacePriority
							, -Database.m_ConfigurationPriority
							, Database.m_Workspace
							, Database.m_Target
						)
					;
				}
			;

			for (auto iDatabase : Selected)
			{
				auto &Database = Databases[iDatabase];
				umint ConfigurationRank = *fg_RankTidyConfiguration(Database, Requested, bAny);

				for (auto &Entry : fGetEntries(Database.m_Path))
				{
					auto pFile = Entry.f_GetMember("file");
					auto pDirectory = Entry.f_GetMember("directory");
					auto pArguments = Entry.f_GetMember("arguments");
					if (!pFile || !pFile->f_IsString() || !pDirectory || !pDirectory->f_IsString() || !pArguments || !pArguments->f_IsArray())
						continue;

					CStr Directory = CFile::fs_CondensePath(pDirectory->f_String());
					while (Directory.f_GetLen() > 1 && Directory.f_EndsWith("/"))
						Directory = Directory.f_Left(Directory.f_GetLen() - 1);
					CStr File = CFile::fs_CondensePath(CFile::fs_GetFullPath(pFile->f_String(), Directory));
					CStr Repository = fg_GetTidyRepository(File, _Options.m_AllRepositories);
					if (!Repository || !Checked.f_FindEqual(Repository))
						continue;

					TCVector<CStr> Arguments;
					for (auto &Argument : pArguments->f_Array())
					{
						if (Argument.f_IsString())
							Arguments.f_Insert(Argument.f_String());
					}

					CStr Output;
					if (auto pOutput = Entry.f_GetMember("output"); pOutput && pOutput->f_IsString())
						Output = pOutput->f_String();
					if (fg_IsTidyHeaderCompile(Arguments, Output))
						continue;

					CTidyCommand Command;
					Command.m_File = File;
					Command.m_Directory = Directory;
					Command.m_Entry = Entry.f_ToString();
					Command.m_Repository = Repository;
					if (Output)
						Command.m_Output = CFile::fs_CondensePath(CFile::fs_GetFullPath(Output, Directory));
					Command.m_BuildDirectory = CFile::fs_GetPath(_Options.m_ConfigStore) / Database.m_Workspace / Database.m_Configuration;
					Command.m_DependencyFile = fg_GetTidyDependencyFile(Arguments, Directory);

					// Commands that differ only in their outputs check the same. The directory resolves relative paths and
					// the compiler can define differently, so they count.
					CStr Key = File + "\x1F" + Directory + "\x1F" + (Arguments.f_IsEmpty() ? CStr() : Arguments[0]);
					bool bSkipNext = false;
					for (umint iArgument = 1; iArgument < Arguments.f_GetLen(); ++iArgument)
					{
						if (bSkipNext)
						{
							bSkipNext = false;
							continue;
						}
						if (fg_IsTidyOutputArgument(Arguments[iArgument], bSkipNext))
							continue;
						Key += "\x1F";
						Key += Arguments[iArgument];
					}
					Command.m_Key = fg_Move(Key);

					if (_Options.m_Mode != ETidyMode::mc_File)
					{
						PassCommands.f_Insert(fg_Move(Command));
						continue;
					}

					CCandidate Candidate{fg_Move(Command), &Database, ConfigurationRank};
					auto *pBest = BestForFile.f_FindEqual(File);
					if (!pBest)
						BestForFile[File] = fg_Move(Candidate);
					else if (fRank(Candidate) < fRank(*pBest))
					{
						OtherForFile[File].f_Insert(fg_Move(pBest->m_Command));
						*pBest = fg_Move(Candidate);
					}
					else
						OtherForFile[File].f_Insert(fg_Move(Candidate.m_Command));
				}
			}

			// An equivalent command of another target or workspace tells what the source includes, when it was the
			// one built.
			for (auto &Candidate : BestForFile)
			{
				auto &Command = Candidate.m_Command;
				if (auto *pOthers = OtherForFile.f_FindEqual(Command.m_File))
				{
					for (auto &Other : *pOthers)
					{
						if (Other.m_Key != Command.m_Key)
							continue;
						Command.m_EquivalentOutputs.f_Insert(Other.m_Output);
						Command.m_EquivalentDependencyFiles.f_Insert(Other.m_DependencyFile);
						Command.m_EquivalentBuildDirectories.f_Insert(Other.m_BuildDirectory);
					}
				}
				PassCommands.f_Insert(fg_Move(Command));
			}

			for (auto &Command : PassCommands)
			{
				if (auto *piCommand = SeenKeys.f_FindEqual(Command.m_Key))
				{
					auto &Kept = Inventory.m_Commands[*piCommand];
					Kept.m_EquivalentOutputs.f_Insert(Command.m_Output);
					Kept.m_EquivalentDependencyFiles.f_Insert(Command.m_DependencyFile);
					Kept.m_EquivalentBuildDirectories.f_Insert(Command.m_BuildDirectory);
					Kept.m_EquivalentOutputs.f_Insert(Command.m_EquivalentOutputs);
					Kept.m_EquivalentDependencyFiles.f_Insert(Command.m_EquivalentDependencyFiles);
					Kept.m_EquivalentBuildDirectories.f_Insert(Command.m_EquivalentBuildDirectories);
					continue;
				}

				SeenKeys[Command.m_Key] = Inventory.m_Commands.f_GetLen();
				Inventory.m_Commands.f_Insert(fg_Move(Command));
			}
		}

		Inventory.m_Commands.f_Sort
			(
				[](CTidyCommand const &_Left, CTidyCommand const &_Right)
				{
					if (auto Compare = _Left.m_File <=> _Right.m_File; Compare != 0)
						return Compare;
					return _Left.m_Key <=> _Right.m_Key;
				}
			)
		;

		return Inventory;
	}

	// The dependencies Ninja keeps of each output it built, from `ninja -t deps`. Ninja reads the dependency
	// files of the compiles into its log and deletes them.
	void fg_ParseTidyNinjaDependencies(CStr const &_Output, CStr const &_BuildDirectory, TCMap<CStr, TCVector<CStr>> &o_Dependencies)
	{
		TCVector<CStr> *pCurrent = nullptr;
		for (auto &Line : _Output.f_SplitLine<true>())
		{
			if (Line.f_StartsWith(" ") || Line.f_StartsWith("\t"))
			{
				if (pCurrent)
					pCurrent->f_Insert(CFile::fs_CondensePath(CFile::fs_GetFullPath(Line.f_Trim(), _BuildDirectory)));
				continue;
			}

			aint iDeps = Line.f_Find(": #deps ");
			pCurrent = iDeps > 0 ? &o_Dependencies[CFile::fs_CondensePath(CFile::fs_GetFullPath(Line.f_Left(iDeps), _BuildDirectory))] : nullptr;
		}
	}

	// Keeps the commands that compile a changed file or include one, as their dependency file or else Ninja's
	// dependencies tell, and counts the changed files that none of them covers.
	void fg_FilterTidyChanged(CTidyInventory &o_Inventory, TCSet<CStr> const &_Changed, TCMap<CStr, TCVector<CStr>> const &_NinjaDependencies, TCVector<CStr> const &_AllRepositories)
	{
		TCSet<CStr> Covered;
		TCSet<CStr> Sources;
		TCSet<CStr> RepositoriesWithoutDependencies;
		TCVector<CTidyCommand> Commands;
		for (auto &Command : o_Inventory.m_Commands)
			Sources[Command.m_File];

		for (auto &Command : o_Inventory.m_Commands)
		{
			// A deleted source is not compiled; a deleted header still selects the sources that include it.
			if (!CFile::fs_FileExists(Command.m_File, EFileAttrib_File))
				continue;

			bool bChanged = !!_Changed.f_FindEqual(Command.m_File);

			// Whichever of the equivalent commands was built tells what the source includes.
			TCVector<CStr> Dependencies;
			auto fAddDependencies = [&](CStr const &_DependencyFile, CStr const &_Output)
				{
					auto FileDependencies = fg_ReadTidyDependencies(_DependencyFile, Command.m_Directory);
					if (FileDependencies.f_IsEmpty())
					{
						if (auto *pDependencies = _NinjaDependencies.f_FindEqual(_Output))
							FileDependencies = *pDependencies;
					}
					Dependencies.f_Insert(fg_Move(FileDependencies));
				}
			;
			fAddDependencies(Command.m_DependencyFile, Command.m_Output);
			for (umint i = 0; i < Command.m_EquivalentOutputs.f_GetLen(); ++i)
				fAddDependencies(Command.m_EquivalentDependencyFiles[i], Command.m_EquivalentOutputs[i]);
			if (Dependencies.f_IsEmpty())
			{
				++o_Inventory.m_nWithoutDependencies;
				RepositoriesWithoutDependencies[Command.m_Repository];
			}

			// Paths are compared as spelled, also on file systems that ignore case: an include spelled with other
			// case than the file is an error of the compile, as clang's -Wnonportable-include-path is on by default
			// and Malterlib compiles with -Werror.
			// A command only reports, and so covers, the headers of its own repository.
			for (auto &Dependency : Dependencies)
			{
				if (_Changed.f_FindEqual(Dependency) && fg_GetTidyRepository(Dependency, _AllRepositories) == Command.m_Repository)
				{
					bChanged = true;
					Covered[Dependency];
				}
			}
			if (!bChanged)
				continue;

			Covered[Command.m_File];
			Commands.f_Insert(fg_Move(Command));
		}

		for (auto &File : _Changed)
		{
			if (!fg_IsTidyCodeFile(File))
				continue;
			if (!Covered.f_FindEqual(File) && CFile::fs_FileExists(File, EFileAttrib_File))
				++o_Inventory.m_nUncheckedChanged;

			// A changed source that no database compiles is new, or its database is out of date.
			CStr Extension = CFile::fs_GetExtension(File).f_LowerCase();
			bool bSource = Extension == "c" || Extension == "cc" || Extension == "cpp" || Extension == "cxx" || Extension == "m" || Extension == "mm";
			if (bSource && !o_Inventory.m_AllSources.f_FindEqual(File) && CFile::fs_FileExists(File, EFileAttrib_File))
				o_Inventory.m_UnbuiltChangedSources.f_Insert(File);

			// A command of the header's repository whose includes are not known may include it with other defines.
			if (!Sources.f_FindEqual(File) && RepositoriesWithoutDependencies.f_FindEqual(fg_GetTidyRepository(File, _AllRepositories)))
				++o_Inventory.m_nUncertainChanged;
		}

		o_Inventory.m_Commands = fg_Move(Commands);
	}

	// The files changed in a repository: in the index, or committed from the merge base with _Base to HEAD.
	TCFuture<TCVector<CStr>> fg_GetTidyChangedFiles(CStr _Repository, CStr _Base)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % ("Finding the changed files of '{}'"_f << _Repository));

		TCVector<CStr> Comparison = {"--cached"};
		if (_Base)
		{
			auto Base = co_await NGit::fg_LaunchGitWithResult({"rev-parse", "--verify", "--end-of-options", _Base + "^{commit}"}, _Repository);
			if (Base.m_ExitCode)
				co_return DMibErrorInstance("Cannot resolve the reference '{}': {}"_f << _Base << Base.f_GetStdErr().f_Trim());

			auto Result = co_await NGit::fg_LaunchGitWithResult({"merge-base", "--all", Base.f_GetStdOut().f_Trim(), "HEAD"}, _Repository);
			if (Result.m_ExitCode == 1)
				co_return DMibErrorInstance("The base '{}' has no common ancestor with HEAD"_f << _Base);
			if (Result.m_ExitCode)
				co_return DMibErrorInstance("Cannot find the merge base for '{}': {}"_f << _Base << Result.f_GetStdErr().f_Trim());

			auto MergeBases = Result.f_GetStdOut().f_SplitLine<true>();
			if (MergeBases.f_GetLen() != 1)
				co_return DMibErrorInstance("The base '{}' does not have a unique merge base with HEAD; specify a more precise base commit"_f << _Base);

			Comparison = {MergeBases[0], "HEAD"};
		}

		// Relative to the repository, so the paths are spelled like those of the build system, whatever links
		// git resolves.
		TCVector<CStr> Params = {"diff", "--name-only", "-z", "--relative", "--no-renames", "--no-ext-diff", "--ignore-submodules=all"};
		Params.f_Insert(Comparison);
		Params.f_Insert("--");

		// The names are NUL-terminated.
		auto Output = co_await NGit::fg_LaunchGit(Params, _Repository);
		TCVector<CStr> Files;
		CStr Name;
		for (umint i = 0; i < Output.f_GetLen(); ++i)
		{
			if (Output[i])
			{
				Name.f_AddChar(Output[i]);
				continue;
			}
			if (Name)
				Files.f_Insert(CFile::fs_CondensePath(_Repository / Name));
			Name = CStr();
		}

		co_return Files;
	}

	// clang-tidy's header filter matches a header by the name it was included with, absolute or relative to the
	// directory of the compile, and with dot segments, like "../Include/X.h" or "/Other/../Repo/X.h". The
	// filter only narrows what clang-tidy reports, so it lets every relative name and every name with dot
	// segments through, and reporting keeps what is in the source's repository, not in one nested in it, once
	// the names are resolved. Ownership is by location as the build system spells it, so a header included
	// through a symbolic link from elsewhere is not the repository's.
	CStr fg_GetTidyHeaderFilter(CStr const &_Location)
	{
		return "^({}/|[^/])|/\\.\\.?/"_f << fg_EscapeTidyRegex(_Location);
	}

	TCVector<CStr> fg_GetTidyArguments(CTidyState const &_State, CTidyCommand const &_Command, CStr const &_DatabaseDirectory, CStr const &_FixesFile)
	{
		auto &_Options = _State.m_Options;

		// The output is parsed, so no colors, whatever a .clang-tidy file asks for.
		TCVector<CStr> Arguments =
			{
				"-p", _DatabaseDirectory, "--quiet", "--use-color=false"
				, "--header-filter={}"_f << fg_GetTidyHeaderFilter(_Command.m_Repository)
			}
		;


		if (_Options.m_Checks)
			Arguments.f_Insert("--checks={}"_f << _Options.m_Checks);

		// The changed files are reported, and fixed, by the resolved names of what is reported, not by
		// clang-tidy's line filter, which matches the names as they were spelled.
		if (_FixesFile)
			Arguments.f_Insert("--export-fixes={}"_f << _FixesFile);

		Arguments.f_Insert(_Command.m_File);

		return Arguments;
	}

	// Reports how many commands are done and running: on a terminal one line rewritten in place, which _bDone
	// clears, and otherwise a line every ten seconds.
	void fg_ReportTidyProgress(CTidyState &_State, bool _bDone)
	{
		if (!_State.m_Options.m_bShowProgress || !_State.m_fProgress)
			return;

		DMibLock(_State.m_ProgressLock);

		umint nDone = _State.m_nDone.f_Load();
		umint nTotal = _State.m_Commands.f_GetLen();
		CStr Progress = "  clang-tidy: {}/{} done, {} running, {} left"_f << nDone << nTotal << _State.m_nRunning.f_Load() << (nTotal - nDone);
		fp64 Now = _State.m_Stopwatch.f_GetTime();

		if (_State.m_Options.m_bAnsiProgress)
		{
			if (_bDone)
			{
				if (_State.m_LastProgressLength)
					_State.m_fProgress("\x1B[2K");
				_State.m_LastProgressLength = 0;
				return;
			}
			// Redrawn about ten times a second at most.
			if (Now - _State.m_LastProgressTime < 0.1)
				return;
			_State.m_LastProgressTime = Now;
			_State.m_fProgress("\x1B[2K{}\x1B[{}D"_f << Progress << Progress.f_GetLen());
			_State.m_LastProgressLength = Progress.f_GetLen();
			return;
		}

		if (_bDone || Now - _State.m_LastProgressTime < 10.0)
			return;

		_State.m_LastProgressTime = Now;
		_State.m_fProgress(Progress + "\n");
	}

	bool fg_IsTidyCancelled(CTidyOptions const &_Options)
	{
		return _Options.m_pCancelled && _Options.m_pCancelled->f_Load();
	}

	// Stops a running clang-tidy when the user aborts, until it is done. A stop that comes before the process is
	// started does nothing, so it is repeated.
	TCFuture<void> fg_StopTidyOnCancel(TCActor<CProcessLaunchActor> _ProcessLaunch, TCSharedPointer<TCAtomic<bool>> _pCancelled, TCSharedPointer<TCAtomic<bool>> _pDone)
	{
		while (!_pDone->f_Load())
		{
			if (_pCancelled && _pCancelled->f_Load())
				[[maybe_unused]] auto Stopped = co_await _ProcessLaunch(&CProcessLaunchActor::f_StopProcess).f_Wrap();

			co_await fg_Timeout(0.1);
		}

		co_return {};
	}

	// Launches a process that the user's abort stops.
	TCFuture<CProcessLaunchActor::CSimpleLaunchResult> fg_LaunchTidyProcess
		(
			TCSharedPointer<TCAtomic<bool>> _pCancelled
			, CStr _Executable
			, TCVector<CStr> _Arguments
			, CStr _Directory
		)
	{
		TCActor<CProcessLaunchActor> ProcessLaunch = fg_Construct();
		auto Destroy = co_await fg_AsyncDestroy(ProcessLaunch);

		TCSharedPointer<TCAtomic<bool>> pDone = fg_Construct(false);
		auto Stopper = fg_StopTidyOnCancel(ProcessLaunch, _pCancelled, pDone);
		auto Result = co_await ProcessLaunch
			(
				&CProcessLaunchActor::f_LaunchSimple
				, CProcessLaunchActor::CSimpleLaunch{_Executable, _Arguments, _Directory, CProcessLaunchActor::ESimpleLaunchFlag_None}
			)
			.f_Wrap()
		;
		pDone->f_Exchange(true);
		co_await fg_Move(Stopper);

		co_return fg_Move(Result);
	}

	// The problems --verify-config reports. It can report a configuration it cannot parse or read and still
	// succeed, so anything but its all-clear is a problem.
	CStr fg_GetTidyConfigurationProblems(CProcessLaunchActor::CSimpleLaunchResult const &_Result)
	{
		CStr Output = _Result.f_GetCombinedOut().f_Trim();
		if (_Result.m_ExitCode || Output != "No config errors detected.")
			return Output ? Output : CStr("exit code {}"_f << _Result.m_ExitCode);

		return {};
	}

	// Runs the commands whose index is _iLane modulo the number of lanes, one after the other, so that no more
	// than that many clang-tidy processes run at once.
	TCFuture<void> fg_RunTidyLane(TCSharedPointer<CTidyState> _pState, umint _iLane, umint _nLanes)
	{
		for (umint iCommand = _iLane; iCommand < _pState->m_Commands.f_GetLen(); iCommand += _nLanes)
		{
			if (fg_IsTidyCancelled(_pState->m_Options))
				break;

			// A launch actor runs one process.
			TCActor<CProcessLaunchActor> ProcessLaunch = fg_Construct();
			auto Destroy = co_await fg_AsyncDestroy(ProcessLaunch);

			auto &Command = _pState->m_Commands[iCommand];
			auto &JobResult = _pState->m_Results[iCommand];

			// The command alone in a database, so clang-tidy runs it and not the other commands of the source.
			CStr DatabaseDirectory = _pState->m_RunDirectory / ("{}"_f << iCommand);
			auto Written = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [DatabaseDirectory, Entry = Command.m_Entry]()
					{
						CFile::fs_CreateDirectory(DatabaseDirectory);
						CFile::fs_WriteStringToFile(DatabaseDirectory / "compile_commands.json", "[{}]"_f << Entry, false);
					}
				)
				.f_Wrap()
			;
			if (!Written)
			{
				JobResult.m_StdErr = Written.f_GetExceptionStr();
				continue;
			}

			if (fg_IsTidyCancelled(_pState->m_Options))
				break;

			CStr FixesFile;
			if (_pState->m_Options.m_bFix)
				FixesFile = _pState->m_RunDirectory / ("{}.yaml"_f << iCommand);

			CProcessLaunchActor::CSimpleLaunch Launch
				{
					_pState->m_Options.m_ClangTidy
					, fg_GetTidyArguments(*_pState, Command, DatabaseDirectory, FixesFile)
					, Command.m_Directory
					, CProcessLaunchActor::ESimpleLaunchFlag_None
				}
			;

			TCSharedPointer<TCAtomic<bool>> pDone = fg_Construct(false);
			auto Stopper = fg_StopTidyOnCancel(ProcessLaunch, _pState->m_Options.m_pCancelled, pDone);
			++_pState->m_nRunning;
			fg_ReportTidyProgress(*_pState, false);
			auto Result = co_await ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch)).f_Wrap();
			--_pState->m_nRunning;
			++_pState->m_nDone;
			fg_ReportTidyProgress(*_pState, false);
			pDone->f_Exchange(true);
			co_await fg_Move(Stopper);

			if (!Result)
			{
				JobResult.m_StdErr = Result.f_GetExceptionStr();
				continue;
			}

			JobResult.m_bLaunched = true;
			JobResult.m_ExitCode = Result->m_ExitCode;
			JobResult.m_StdOut = Result->f_GetStdOut();
			JobResult.m_StdErr = Result->f_GetStdErr();

			// A repository enables checks one at a time, so a source none is enabled for yet is skipped.
			// It is that error alone, which a configuration error comes before.
			if (JobResult.m_ExitCode && JobResult.m_StdErr.f_Trim() == "Error: no checks enabled.")
			{
				JobResult.m_bWithoutChecks = true;
				JobResult.m_StdOut = CStr();
				JobResult.m_StdErr = CStr();
			}
		}

		co_return {};
	}

	// Parses the location of a diagnostic line, "<path>:<line>:<column>: <severity>: <message>".
	bool fg_ParseTidyDiagnosticLine(CStr const &_Line, CStr const &_Directory, CTidyDiagnostic &o_Diagnostic, bool &o_bNote)
	{
		// Source excerpts are indented, so text in them that looks like a diagnostic is not one.
		if (_Line.f_StartsWith(" ") || _Line.f_StartsWith("\t"))
			return false;

		// A diagnostic without a location, like a warning about the configuration.
		for (ch8 const *pSeverity : {"warning: ", "error: "})
		{
			if (_Line.f_StartsWith(pSeverity))
			{
				o_bNote = false;
				o_Diagnostic.m_Path = CStr();
				o_Diagnostic.m_Line = 0;
				o_Diagnostic.m_Column = 0;
				o_Diagnostic.m_bError = CStr(pSeverity) == "error: ";
				o_Diagnostic.m_Header = ": " + _Line;
				return true;
			}
		}

		for (ch8 const *pSeverity : {": warning: ", ": error: ", ": note: "})
		{
			aint iSeverity = _Line.f_Find(pSeverity);
			if (iSeverity < 0)
				continue;

			CStr Location = _Line.f_Left(iSeverity);
			aint iColumn = Location.f_FindReverse(":");
			if (iColumn <= 0)
				return false;
			aint iLine = Location.f_Left(iColumn).f_FindReverse(":");
			if (iLine <= 0)
				return false;

			umint Line = Location.f_Extract(iLine + 1, iColumn - iLine - 1).f_ToInt(umint(0));
			umint Column = Location.f_Extract(iColumn + 1).f_ToInt(umint(0));
			if (!Line || !Column)
				return false;

			o_bNote = CStr(pSeverity) == ": note: ";
			o_Diagnostic.m_Path = CFile::fs_CondensePath(CFile::fs_GetFullPath(Location.f_Left(iLine), _Directory));
			o_Diagnostic.m_Line = Line;
			o_Diagnostic.m_Column = Column;
			o_Diagnostic.m_bError = CStr(pSeverity) == ": error: ";
			o_Diagnostic.m_Header = _Line.f_Extract(iSeverity);

			return true;
		}

		return false;
	}

	// The diagnostics of clang-tidy's output: each warning or error with the notes and source lines after it.
	TCVector<CTidyDiagnostic> fg_ParseTidyOutput(CStr const &_Output, CStr const &_Directory)
	{
		TCVector<CTidyDiagnostic> Diagnostics;
		for (auto &Line : _Output.f_SplitLine<false>())
		{
			CTidyDiagnostic Diagnostic;
			bool bNote = false;
			if (fg_ParseTidyDiagnosticLine(Line, _Directory, Diagnostic, bNote) && !bNote)
			{
				if (Diagnostic.m_Path)
					Diagnostic.m_Text = "{}:{}:{}{}\n"_f << Diagnostic.m_Path << Diagnostic.m_Line << Diagnostic.m_Column << Diagnostic.m_Header;
				else
					Diagnostic.m_Text = Diagnostic.m_Header.f_Extract(2) + "\n";
				Diagnostics.f_Insert(fg_Move(Diagnostic));
			}
			else if (!Diagnostics.f_IsEmpty())
				Diagnostics.f_GetLast().m_Text += Line + "\n";
		}

		for (auto &Diagnostic : Diagnostics)
		{
			while (Diagnostic.m_Text.f_EndsWith("\n\n"))
				Diagnostic.m_Text = Diagnostic.m_Text.f_Left(Diagnostic.m_Text.f_GetLen() - 1);
		}

		return Diagnostics;
	}

	// Keeps the diagnostics of a file of exported fixes whose files are all allowed, so that no replacement of
	// a check lands in a file that is not reported. clang-tidy writes one "  - DiagnosticName:" block per
	// diagnostic, naming each file of its message, notes, and replacements in a "FilePath:" line. Empty when
	// none is kept.
	CStr fg_FilterTidyFixes(CStr const &_Fixes, NFunction::TCFunctionMovable<bool (CStr const &_File)> &&_fAllowed)
	{
		CStr Head;
		CStr Tail;
		TCVector<CStr> Blocks;
		bool bInDiagnostics = false;
		for (auto &Line : _Fixes.f_SplitLine<false>())
		{
			if (Line.f_StartsWith("  - DiagnosticName:"))
			{
				bInDiagnostics = true;
				Blocks.f_Insert(Line + "\n");
			}
			else if (bInDiagnostics && Line.f_StartsWith("  "))
				Blocks.f_GetLast() += Line + "\n";
			else if (Blocks.f_IsEmpty())
				Head += Line + "\n";
			else
			{
				bInDiagnostics = false;
				Tail += Line + "\n";
			}
		}

		auto fUnquote = [](CStr _Value) -> CStr
			{
				_Value = _Value.f_Trim();
				if (_Value.f_GetLen() >= 2 && _Value.f_StartsWith("'") && _Value.f_EndsWith("'"))
					return _Value.f_Extract(1, _Value.f_GetLen() - 2).f_Replace("''", "'");
				if (_Value.f_GetLen() >= 2 && _Value.f_StartsWith("\"") && _Value.f_EndsWith("\""))
					return _Value.f_Extract(1, _Value.f_GetLen() - 2).f_Replace("\\\"", "\"").f_Replace("\\\\", "\\");
				return _Value;
			}
		;

		// The file of the message and those of the replacements count; a note only points at a location.
		CStr Kept;
		for (auto &Block : Blocks)
		{
			bool bAllowed = true;
			bool bInNotes = false;
			for (auto &Line : Block.f_SplitLine<true>())
			{
				CStr Trimmed = Line.f_Trim();
				if (Trimmed == "Notes:")
					bInNotes = true;
				else if (!Line.f_StartsWith("      ") && Trimmed.f_EndsWith(":"))
					bInNotes = false;

				// Keys only, not text of a replacement that holds the word.
				bool bReplacement = Trimmed.f_StartsWith("- FilePath:");
				if (!bReplacement && !Trimmed.f_StartsWith("FilePath:"))
					continue;
				if (bInNotes && !bReplacement)
					continue;
				CStr File = fUnquote(Trimmed.f_Extract(Trimmed.f_Find("FilePath:") + 9));
				if (File && !_fAllowed(File))
				{
					bAllowed = false;
					break;
				}
			}
			if (bAllowed)
				Kept += Block;
		}

		if (!Kept)
			return {};

		return Head + Kept + Tail;
	}

	TCFuture<CTidyRunResult> fg_RunTidy(CTidyOptions _Options, NFormat::CFormatSink _Sink)
	{
		auto CaptureScope = co_await (g_CaptureExceptions % "Running clang-tidy");

		CTidyRunResult RunResult;
		auto &Summary = RunResult.m_Summary;

		// Locations are compared with condensed paths, without a trailing separator.
		for (auto *pLocations : {&_Options.m_CheckedRepositories, &_Options.m_AllRepositories})
		{
			for (auto &Location : *pLocations)
			{
				Location = CFile::fs_CondensePath(Location);
				while (Location.f_GetLen() > 1 && Location.f_EndsWith("/"))
					Location = Location.f_Left(Location.f_GetLen() - 1);
			}
		}

		if (!CFile::fs_FileExists(_Options.m_ClangTidy, EFileAttrib_File))
			co_return DMibErrorInstance("Cannot find clang-tidy at '{}'"_f << _Options.m_ClangTidy);

		// A misspelled check enables none, which would otherwise look like a source without enabled checks.
		if (_Options.m_Checks)
		{
			// Against an empty configuration, as the configuration files are verified for each source.
			auto Verify = co_await fg_LaunchTidyProcess
				(
					_Options.m_pCancelled
					, _Options.m_ClangTidy
					, {"--verify-config", "--config={}", "--checks={}"_f << _Options.m_Checks}
					, _Options.m_CheckedRepositories[0]
				)
			;
			if (fg_IsTidyCancelled(_Options))
				co_return DMibErrorInstance("Aborted");
			if (CStr Problems = fg_GetTidyConfigurationProblems(Verify))
				co_return DMibErrorInstance("--checks '{}' is not valid: {}"_f << _Options.m_Checks << Problems);
		}

		TCOptional<TCSet<CStr>> Changed;
		if (_Options.m_bStaged || _Options.m_Base)
		{
			TCFutureVector<TCVector<CStr>> Pending;
			for (auto &Repository : _Options.m_CheckedRepositories)
				fg_GetTidyChangedFiles(Repository, _Options.m_Base) > Pending;

			Changed = TCSet<CStr>();
			auto &ChangedFiles = *Changed;
			for (auto &Files : co_await fg_AllDone(Pending))
			{
				for (auto &File : Files)
					ChangedFiles[File];
			}
		}

		CTidyInventory Inventory = co_await
			(
				g_Dispatch(fg_BlockingActor()) / [Options = _Options]() -> CTidyInventory
				{
					return fg_ReadTidyInventory(Options);
				}
			)
		;

		if (!Inventory.m_nDatabases)
			co_return DMibErrorInstance("No compile database found in '{}'; generate the build system first, with MalterlibGenerateCompileCommands=true in CI"_f << _Options.m_ConfigStore);
		if (Inventory.m_Error)
			co_return DMibErrorInstance(Inventory.m_Error);

		// A repository that no generated workspace builds is not checked.
		{
			TCSet<CStr> Built;
			for (auto &Command : Inventory.m_Commands)
				Built[Command.m_Repository];
			if (Built.f_IsEmpty())
				co_return DMibErrorInstance("No compile command of the generated workspaces compiles a source of the selected repositories; generate a workspace that builds them");

			TCVector<CStr> Unbuilt;
			for (auto &Repository : _Options.m_CheckedRepositories)
			{
				if (!Built.f_FindEqual(Repository))
					Unbuilt.f_Insert(Repository);
			}
			if (!Unbuilt.f_IsEmpty())
				_Sink.m_fReport("No compile command compiles a source of {}, so {} not checked\n"_f << CStr::fs_Join(Unbuilt, ", ") << (Unbuilt.f_GetLen() == 1 ? "it is" : "they are"));
		}
		if (fg_IsTidyCancelled(_Options))
			co_return DMibErrorInstance("Aborted");

		if (Changed)
		{
			// Ninja's dependencies of the commands that have no dependency file.
			TCSet<CStr> BuildDirectories;
			for (auto &Command : Inventory.m_Commands)
			{
				if (Command.m_BuildDirectory)
					BuildDirectories[Command.m_BuildDirectory];
				for (auto &Directory : Command.m_EquivalentBuildDirectories)
				{
					if (Directory)
						BuildDirectories[Directory];
				}
			}

			TCMap<CStr, TCVector<CStr>> NinjaDependencies;
			CStr Ninja = CFile::fs_GetProgramDirectory() / ("ninja" + CFile::mc_ExecutableExtension);
			if (CFile::fs_FileExists(Ninja, EFileAttrib_File))
			{
				TCFutureVector<CProcessLaunchActor::CSimpleLaunchResult> Pending;
				TCVector<CStr> Directories;
				for (auto &Directory : BuildDirectories)
				{
					if (!CFile::fs_FileExists(Directory / "build.ninja", EFileAttrib_File))
						continue;
					Directories.f_Insert(Directory);
					fg_LaunchTidyProcess(_Options.m_pCancelled, Ninja, {"-C", Directory, "-t", "deps"}, Directory) > Pending;
				}

				auto Results = co_await fg_AllDoneWrapped(Pending);
				for (umint i = 0; i < Results.f_GetLen(); ++i)
				{
					if (Results[i] && !Results[i]->m_ExitCode)
						fg_ParseTidyNinjaDependencies(Results[i]->f_GetStdOut(), Directories[i], NinjaDependencies);
					else
						_Sink.m_fReport("Cannot read the dependencies Ninja keeps in '{}', so sources that include a changed header may not be checked\n"_f << Directories[i]);
				}
			}

			Inventory = co_await
				(
					g_Dispatch(fg_BlockingActor()) /
					[
						Inventory = fg_Move(Inventory)
						, Changed = *Changed
						, NinjaDependencies = fg_Move(NinjaDependencies)
						, AllRepositories = _Options.m_AllRepositories
					]() mutable -> CTidyInventory
					{
						fg_FilterTidyChanged(Inventory, Changed, NinjaDependencies, AllRepositories);
						return fg_Move(Inventory);
					}
				)
			;

			// Only a built source tells what it includes; generators other than Ninja record nothing.
			if (Inventory.m_nWithoutDependencies)
			{
				_Sink.m_fReport
					(
						"{} {} no dependency information, from a dependency file or Ninja, so changed headers they include are not checked through them\n"_f
						<< Inventory.m_nWithoutDependencies
						<< (Inventory.m_nWithoutDependencies == 1 ? "command has" : "commands have")
					)
				;
			}
		}

		Summary.m_nUncheckedChanged = Inventory.m_nUncheckedChanged;

		TCSharedPointer<CTidyState> pState = fg_Construct();
		pState->m_Options = _Options;
		pState->m_Commands = fg_Move(Inventory.m_Commands);
		pState->m_Results.f_SetLen(pState->m_Commands.f_GetLen());
		pState->m_Changed = Changed;
		pState->m_fProgress = fg_Move(_Sink.m_fReport);
		CUniversallyUniqueIdentifier RunID(EUniversallyUniqueIdentifierGenerate_Random);
		pState->m_RunDirectory = fg_GetToolRootDirectory() / "Tidy" / RunID.f_GetAsString(EUniversallyUniqueIdentifierFormat_AlphaNum);
		CFile::fs_CreateDirectory(pState->m_RunDirectory);

		auto CleanupRun = g_OnScopeExit / [RunDirectory = pState->m_RunDirectory]
			{
				if (CFile::fs_FileExists(RunDirectory, EFileAttrib_Directory))
					CFile::fs_DeleteDirectoryRecursive(RunDirectory);
			}
		;

		{
			umint nLanes = fg_Max(fg_Min(_Options.m_nJobs, pState->m_Commands.f_GetLen()), umint(1));
			TCFutureVector<void> Lanes;
			for (umint iLane = 0; iLane < nLanes; ++iLane)
				fg_RunTidyLane(pState, iLane, nLanes) > Lanes;
			auto LaneResults = co_await fg_AllDoneWrapped(Lanes);
			fg_ReportTidyProgress(*pState, true);
			co_await (fg_Move(LaneResults) | g_Unwrap);
		}

		if (fg_IsTidyCancelled(_Options))
			co_return DMibErrorInstance("Aborted");

		// clang-tidy leaves out a misspelled check or option, and a configuration it cannot read, so the
		// configuration of each directory of a checked source is verified. A configuration that enables no check
		// because of a misspelling is no skip either.
		TCMap<CStr, CStr> InvalidConfigurations;
		{
			TCSet<CStr> Directories;
			for (umint iCommand = 0; iCommand < pState->m_Commands.f_GetLen(); ++iCommand)
			{
				if (pState->m_Results[iCommand].m_bLaunched)
					Directories[CFile::fs_GetPath(pState->m_Commands[iCommand].m_File)];
			}

			TCVector<CStr> Pending;
			for (auto &Directory : Directories)
				Pending.f_Insert(Directory);

			for (umint iFirst = 0; iFirst < Pending.f_GetLen(); iFirst += fg_Max(_Options.m_nJobs, umint(1)))
			{
				if (fg_IsTidyCancelled(_Options))
					co_return DMibErrorInstance("Aborted");

				TCFutureVector<CProcessLaunchActor::CSimpleLaunchResult> Verifications;
				umint iEnd = fg_Min(iFirst + fg_Max(_Options.m_nJobs, umint(1)), Pending.f_GetLen());
				for (umint i = iFirst; i < iEnd; ++i)
					fg_LaunchTidyProcess(_Options.m_pCancelled, _Options.m_ClangTidy, {"--verify-config"}, Pending[i]) > Verifications;

				auto Results = co_await fg_AllDoneWrapped(Verifications);
				if (fg_IsTidyCancelled(_Options))
					co_return DMibErrorInstance("Aborted");
				for (umint i = iFirst; i < iEnd; ++i)
				{
					auto &Result = Results[i - iFirst];
					if (!Result)
						InvalidConfigurations[Pending[i]] = Result.f_GetExceptionStr();
					else if (CStr Problems = fg_GetTidyConfigurationProblems(*Result))
						InvalidConfigurations[Pending[i]] = Problems;
				}
			}
		}

		// Each diagnostic once, in path order, also those of a header that several sources report.
		TCVector<CTidyDiagnostic> Diagnostics;
		TCSet<CStr> Seen;
		CStr Failures;
		TCSet<CStr> CheckedFiles;
		TCSet<CStr> ReportedConfigurations;
		umint nCheckedCommands = 0;
		for (umint iCommand = 0; iCommand < pState->m_Commands.f_GetLen(); ++iCommand)
		{
			auto &Command = pState->m_Commands[iCommand];
			auto &Result = pState->m_Results[iCommand];
			if (auto *pInvalid = InvalidConfigurations.f_FindEqual(CFile::fs_GetPath(Command.m_File)))
			{
				++Summary.m_nFailed;
				CStr Directory = CFile::fs_GetPath(Command.m_File);
				if (!ReportedConfigurations.f_FindEqual(Directory))
				{
					ReportedConfigurations[Directory];
					Failures += "{}: the configuration of the sources is not valid: {}\n"_f << Directory << *pInvalid;
				}

				// The fixes of a configuration that is not valid are not applied.
				CStr FixesFile = pState->m_RunDirectory / ("{}.yaml"_f << iCommand);
				if (CFile::fs_FileExists(FixesFile, EFileAttrib_File))
					CFile::fs_DeleteFile(FixesFile);
				continue;
			}
			if (Result.m_bWithoutChecks)
			{
				++Summary.m_nWithoutChecks;
				continue;
			}
			CheckedFiles[Command.m_File];
			++nCheckedCommands;

			bool bReportedError = false;
			bool bCompileError = false;
			for (auto &Diagnostic : fg_ParseTidyOutput(Result.m_StdOut, Command.m_Directory))
			{
				bReportedError = bReportedError || Diagnostic.m_bError;
				// Any error of the compiler, also a warning that -Werror makes one.
				bCompileError = bCompileError || (Diagnostic.m_bError && Diagnostic.m_Header.f_Find("[clang-diagnostic-") >= 0);
				// One without a location is about the run, like its configuration, and is always reported.
				if (Diagnostic.m_Path)
				{
					if (Changed && !Changed->f_FindEqual(Diagnostic.m_Path))
						continue;

					// A check can report in a header it was not asked to, when a note is in a reported file.
					if (fg_GetTidyRepository(Diagnostic.m_Path, _Options.m_AllRepositories) != Command.m_Repository)
						continue;
				}
				CStr Key = "{}\x1F{}\x1F{}\x1F{}"_f << Diagnostic.m_Path << Diagnostic.m_Line << Diagnostic.m_Column << Diagnostic.m_Header;
				if (Seen.f_FindEqual(Key))
					continue;
				Seen[Key];
				Diagnostics.f_Insert(fg_Move(Diagnostic));
			}

			// A compile error can be in a header of another repository, so name the source it stopped. It fails the
			// command, also when the error is in a file that is not reported.
			if (bCompileError)
			{
				++Summary.m_nFailed;
				Failures += "{}: does not compile with the command of its database, which may be out of date\n"_f << Command.m_File;

				// As clang-tidy does without --fix-errors, the fixes of a source that does not compile are left.
				CStr FixesFile = pState->m_RunDirectory / ("{}.yaml"_f << iCommand);
				if (CFile::fs_FileExists(FixesFile, EFileAttrib_File))
					CFile::fs_DeleteFile(FixesFile);
			}
			// clang-tidy fails a command with errors, which it reports; any other failure is the command's.
			else if (!Result.m_bLaunched || (Result.m_ExitCode && !bReportedError))
			{
				++Summary.m_nFailed;
				// The cause can be on either stream, like an argument clang does not know.
				Failures += "{}: clang-tidy failed{}: {}\n"_f
					<< Command.m_File
					<< (Result.m_bLaunched ? CStr(" with exit code {}"_f << Result.m_ExitCode) : CStr(" to launch"))
					<< (Result.m_StdOut.f_Trim() + "\n" + Result.m_StdErr.f_Trim()).f_Trim()
				;
			}

			// Only the fixes of files the command reports are applied, also of the replacements a check offers in
			// other files.
			if (_Options.m_bFix && !bCompileError)
			{
				CStr FixesFile = pState->m_RunDirectory / ("{}.yaml"_f << iCommand);
				if (CFile::fs_FileExists(FixesFile, EFileAttrib_File))
				{
					CStr Filtered = fg_FilterTidyFixes
						(
							CFile::fs_ReadStringFromFile(FixesFile, true)
							, [&](CStr const &_File)
							{
								CStr File = CFile::fs_CondensePath(CFile::fs_GetFullPath(_File, Command.m_Directory));
								if (fg_GetTidyRepository(File, _Options.m_AllRepositories) != Command.m_Repository)
									return false;
								return !Changed || !!Changed->f_FindEqual(File);
							}
						)
					;
					if (Filtered)
						CFile::fs_WriteStringToFile(FixesFile, Filtered, false);
					else
						CFile::fs_DeleteFile(FixesFile);
				}
			}
		}

		Summary.m_nCommands = nCheckedCommands;
		Summary.m_nFiles = CheckedFiles.f_GetLen();

		Diagnostics.f_Sort
			(
				[](CTidyDiagnostic const &_Left, CTidyDiagnostic const &_Right)
				{
					if (auto Compare = _Left.m_Path <=> _Right.m_Path; Compare != 0)
						return Compare;
					if (auto Compare = _Left.m_Line <=> _Right.m_Line; Compare != 0)
						return Compare;
					if (auto Compare = _Left.m_Column <=> _Right.m_Column; Compare != 0)
						return Compare;
					return _Left.m_Header <=> _Right.m_Header;
				}
			)
		;

		for (auto &Diagnostic : Diagnostics)
		{
			if (Diagnostic.m_bError)
				++Summary.m_nErrors;
			else
				++Summary.m_nWarnings;
			pState->m_fProgress(Diagnostic.m_Text);
		}

		if (Failures)
			pState->m_fProgress(Failures);

		if (_Options.m_bFix && !CFile::fs_FindFiles(pState->m_RunDirectory / "*.yaml", EFileAttrib_File, false).f_IsEmpty())
		{
			if (!CFile::fs_FileExists(_Options.m_ApplyReplacements, EFileAttrib_File))
				co_return DMibErrorInstance("Cannot find clang-apply-replacements at '{}'"_f << _Options.m_ApplyReplacements);

			if (fg_IsTidyCancelled(_Options))
				co_return DMibErrorInstance("Aborted");

			TCActor<CProcessLaunchActor> ProcessLaunch = fg_Construct();
			auto Destroy = co_await fg_AsyncDestroy(ProcessLaunch);

			CProcessLaunchActor::CSimpleLaunch Launch
				{
					_Options.m_ApplyReplacements
					, {pState->m_RunDirectory}
					, pState->m_RunDirectory
					, CProcessLaunchActor::ESimpleLaunchFlag_None
				}
			;
			TCSharedPointer<TCAtomic<bool>> pDone = fg_Construct(false);
			auto Stopper = fg_StopTidyOnCancel(ProcessLaunch, _Options.m_pCancelled, pDone);
			auto Result = co_await ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch)).f_Wrap();
			pDone->f_Exchange(true);
			co_await fg_Move(Stopper);
			if (fg_IsTidyCancelled(_Options))
				co_return DMibErrorInstance("Aborted, while the fixes were applied; some may be");
			if (!Result)
				co_return Result.f_GetException();
			if (Result->m_ExitCode)
				co_return DMibErrorInstance("Applying the fixes failed (exit code {}): {}"_f << Result->m_ExitCode << Result->f_GetCombinedOut().f_Trim());

			Summary.m_bFixed = true;
		}

		// A changed file no source is known to include may be included by one whose dependencies are not known.
		bool bUnknownCoverage = Inventory.m_nUncertainChanged > 0;
		if (bUnknownCoverage)
			pState->m_fProgress("Changed files may be unchecked: some commands have no dependency information, so build first\n");
		if (!Inventory.m_UnbuiltChangedSources.f_IsEmpty())
		{
			bUnknownCoverage = true;
			pState->m_fProgress
				(
					"No compile database compiles the changed {} {}, so regenerate the build system\n"_f
					<< (Inventory.m_UnbuiltChangedSources.f_GetLen() == 1 ? "source" : "sources")
					<< CStr::fs_Join(Inventory.m_UnbuiltChangedSources, ", ")
				)
			;
		}

		RunResult.m_ExitCode = Summary.m_nWarnings || Summary.m_nErrors || Summary.m_nFailed || bUnknownCoverage ? 1 : 0;

		co_return RunResult;
	}

	CStr fg_DescribeTidySummary(CTidySummary const &_Summary, fp64 _Seconds)
	{
		CStr Description = "Checked {} {} with {} {}: {} {}, {} {}"_f
			<< _Summary.m_nFiles << (_Summary.m_nFiles == 1 ? "file" : "files")
			<< _Summary.m_nCommands << (_Summary.m_nCommands == 1 ? "command" : "commands")
			<< _Summary.m_nWarnings << (_Summary.m_nWarnings == 1 ? "warning" : "warnings")
			<< _Summary.m_nErrors << (_Summary.m_nErrors == 1 ? "error" : "errors")
		;
		if (_Summary.m_nFailed)
			Description += ", {} failed"_f << _Summary.m_nFailed;
		if (_Summary.m_nWithoutChecks)
			Description += ", {} {} without enabled checks"_f << _Summary.m_nWithoutChecks << (_Summary.m_nWithoutChecks == 1 ? "command" : "commands");
		if (_Summary.m_nUncheckedChanged)
			Description += ", {} changed {} no checked source compiles or includes"_f << _Summary.m_nUncheckedChanged << (_Summary.m_nUncheckedChanged == 1 ? "file" : "files");
		if (_Summary.m_bFixed)
			Description += ", fixes applied";
		Description += " in {fe2}s\n"_f << _Seconds;

		return Description;
	}
}
