// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NTool
{
	/// _Path expanded once, as the file API expands the path it is given, with '/' as separator and without trailing separators. _Path is
	/// the path as given, since expanding an expanded path again removes the one trailing separator that the first expansion keeps. With
	/// _bWin32PathNames it names what Win32 opens: a name that ends in a single period loses it, and when the expanded path doesn't end in a
	/// separator its last name loses all trailing periods and spaces. A name of only periods is not one that ends in a single period, and "."
	/// and ".." are relative components that keep their periods. The server and share of a UNC path form its root, which Win32 keeps as it
	/// is. Like the file API, a //?/ prefix is removed after expansion and the rest is expanded again, which removes the trailing separator the
	/// first expansion keeps, and a path that then has MAX_PATH or more UTF-16 code units, counting the one trailing separator expansion keeps,
	/// keeps its names, since the file API opens it in extended-length form, where names are literal.
	inline NStr::CStr fg_ProtectedDirectoryComparePath(NStr::CStr const &_Path, bool _bWin32PathNames)
	{
		constexpr umint c_WindowsMaxPath = 260;

		auto fRemoveTrailingSeparators = [](NStr::CStr &o_Path)
			{
				while (o_Path.f_GetLen() > 1 && o_Path.f_EndsWith("/"))
					o_Path = o_Path.f_Left(o_Path.f_GetLen() - 1);
			}
		;

		NStr::CStr Path = NFile::CFile::fs_GetExpandedPath(_Path, true);

		if (!_bWin32PathNames)
		{
			fRemoveTrailingSeparators(Path);
			return Path;
		}

		if (Path.f_CmpNoCase("//?/UNC/", 8) == 0)
			Path = NFile::CFile::fs_GetExpandedPath("//" + Path.f_Extract(8), false);
		else if (Path.f_Cmp("//?/", 4) == 0)
			Path = NFile::CFile::fs_GetExpandedPath(Path.f_Extract(4), false);

		// Expansion removes a single trailing separator, so only the expanded path tells whether Win32 sees one
		bool bEndsWithSeparator = Path.f_GetLen() > 1 && Path.f_EndsWith("/");
		bool bLongPath = NStr::CWStr(Path).f_GetLen() >= c_WindowsMaxPath;
		fRemoveTrailingSeparators(Path);

		if (bLongPath)
			return Path;

		umint RootLength = 0;
		if (Path.f_Cmp("//", 2) == 0 && Path.f_Cmp("//./", 4) != 0)
		{
			ch8 const *pRootEnd = Path.f_GetStr() + 2;
			for (umint iRootName = 0; iRootName < 2 && *pRootEnd; ++iRootName)
			{
				if (iRootName)
					++pRootEnd;

				while (*pRootEnd && *pRootEnd != '/')
					++pRootEnd;
			}

			RootLength = pRootEnd - Path.f_GetStr();
		}

		NStr::CStr Normalized = Path.f_Left(RootLength);
		NStr::CStr Name;
		auto fAddName = [&](bool _bLastName)
			{
				bool bRelative = Name == "." || Name == "..";
				if (!bRelative && Name.f_EndsWith(".") && !Name.f_EndsWith(".."))
					Name = Name.f_Left(Name.f_GetLen() - 1);

				if (_bLastName && !bEndsWithSeparator && !bRelative)
				{
					umint Length = Name.f_GetLen();
					while (Length > 0 && (fg_Const(Name)[Length - 1] == '.' || fg_Const(Name)[Length - 1] == ' '))
						--Length;

					Name = Name.f_Left(Length);
				}

				Normalized += Name;
				Name.f_Clear();
			}
		;

		for (ch8 const *pParse = Path.f_GetStr() + RootLength; *pParse; ++pParse)
		{
			if (*pParse == '/')
			{
				fAddName(false);
				Normalized.f_AddChar('/');
			}
			else
				Name.f_AddChar(*pParse);
		}

		fAddName(true);
		fRemoveTrailingSeparators(Normalized);

		return Normalized;
	}

	/// Whether the argument _Path, as given, is one of _ProtectedDirectories or a path below one of them, compared as strings ignoring ASCII
	/// case after expansion. A protected directory can end with a separator. An entry relative to the working directory, or to the working
	/// directory of a drive like C:Shared, protects nothing, and an argument relative to the working directory of a drive, like C: or C:Shared,
	/// is in none.
	inline bool fg_IsInProtectedDirectory(NStr::CStr const &_Path, NContainer::TCVector<NStr::CStr> const &_ProtectedDirectories, bool _bWin32PathNames)
	{
		// Expansion keeps such an argument relative to the working directory of its drive, where a bare drive would match the root of the drive
		bool bDriveRelative = _Path.f_GetLen() > 1 && fg_Const(_Path)[1] == ':' && (_Path.f_GetLen() == 2 || (fg_Const(_Path)[2] != '/' && fg_Const(_Path)[2] != '\\'));
		if (bDriveRelative)
			return false;

		NStr::CStr Path = fg_ProtectedDirectoryComparePath(_Path, _bWin32PathNames);

		for (auto const &ProtectedDirectory : _ProtectedDirectories)
		{
			// The server resolves relative paths from a directory of its own, so an entry that expansion resolves against the working directory
			// here names no directory the server shares
			bool bDriveRoot
				= ProtectedDirectory.f_GetLen() > 2 && fg_Const(ProtectedDirectory)[1] == ':' && (fg_Const(ProtectedDirectory)[2] == '/' || fg_Const(ProtectedDirectory)[2] == '\\')
			;
			if (!ProtectedDirectory.f_StartsWith("/") && !bDriveRoot)
				continue;

			NStr::CStr Directory = fg_ProtectedDirectoryComparePath(ProtectedDirectory, _bWin32PathNames);

			// An entry that names no directory protects nothing; the root is "/"
			if (Directory.f_IsEmpty())
				continue;

			if (Path.f_CmpNoCase(Directory) == 0)
				return true;

			// Only a root such as "/" still ends with a separator here
			NStr::CStr Prefix = Directory;
			if (!Prefix.f_EndsWith("/"))
				Prefix += "/";

			if (Path.f_StartsWithNoCase(Prefix))
				return true;
		}

		return false;
	}

	/// The spelling of the protected directory argument _Param, as given, that the build server gets: the absolute path expansion makes of it,
	/// which resolves ".." by name as the protected directory check does, since the server runs the tool in a directory of its own. A path from
	/// the root of the current drive gets the drive of the client, since that is the drive the ProtectedDirs entries are expanded with when the
	/// argument is found protected. With _bWin32PathNames an argument whose names Win32 changes is passed as what it names, since the tools on
	/// the server can open it in extended-length form, where the names are literal and would name another directory than the one found
	/// protected. An argument in extended-length form stays in that form when its names are what Win32 opens, or when they are literal, since
	/// tools that don't add the prefix themselves need it for a long path. _bWindowsPaths tells that the paths are Windows paths also when their
	/// names are literal, as with DMibAlwaysUseLongWindowsPaths; _bWin32PathNames implies it.
	inline NStr::CStr fg_ProtectedDirectoryForwardPath(NStr::CStr const &_Param, bool _bWin32PathNames, bool _bWindowsPaths)
	{
		NStr::CStr Expanded = NFile::CFile::fs_GetExpandedPath(_Param, true);
		NStr::CStr Path = Expanded.f_TrimRight("/");

		// A root of only separators
		if (Path.f_IsEmpty())
			return Expanded;

		if (_bWin32PathNames)
		{
			NStr::CStr Win32Path = fg_ProtectedDirectoryComparePath(_Param, true);
			NStr::CStr LiteralPath = fg_ProtectedDirectoryComparePath(_Param, false);
			if (Win32Path != LiteralPath)
			{
				NStr::CStr LiteralNames;
				if (LiteralPath.f_CmpNoCase("//?/UNC/", 8) == 0)
					LiteralNames = "//" + LiteralPath.f_Extract(8);
				else if (LiteralPath.f_Cmp("//?/", 4) == 0)
					LiteralNames = LiteralPath.f_Extract(4);

				if (LiteralNames != Win32Path)
					Path = Win32Path;
			}
		}

		// Without a separator a drive names the current directory of that drive
		if (Path.f_GetLen() == 2 && fg_Const(Path)[1] == ':')
			return Path + "/";

		// The tool expands the path again, which removes a single trailing separator and keeps one of several. In Win32 a kept separator keeps
		// the trailing periods and spaces of the last name.
		if (Expanded.f_EndsWith("/"))
			Path += "//";
		else if (_Param.f_EndsWith("/") || _Param.f_EndsWith("\\"))
			Path += "/";

		// Win32 takes a path in extended-length form as it is, so only with backslashes
		if ((_bWin32PathNames || _bWindowsPaths) && Path.f_Cmp("//?/", 4) == 0)
			NStr::fg_StrReplaceChar(Path, '/', '\\');

		return Path;
	}
}
