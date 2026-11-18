// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NTool
{
	/// Returns the key that _Path, as given and not yet expanded, is compared by, not a path to open: _Path expanded once as the file API expands
	/// it, with '/' as separator and without trailing separators, except for the root "/". With _bWin32PathNames the names are trimmed as Win32
	/// trims the names of ordinary paths: a name that ends in a single period loses it, and the last name loses its trailing periods and spaces
	/// unless the expanded path ends in a separator. The server and share names of a UNC path are not trimmed. A //?/ prefix is removed and the
	/// rest expanded again, and a path that is MAX_PATH or more UTF-16 code units long with its trailing separators keeps its names, as in
	/// extended-length form.
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

		// fs_GetExpandedPath removes a lone trailing separator but can leave one of several, so the separator Win32 sees is checked after expansion
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

	/// Returns whether _Path, with '/' as separator, has the extended-length prefix //?/ without a drive root like C:/ or a UNC/ with a server name
	/// after it. The compare key of such a path can be empty or "/", which would protect every path that starts with '/'.
	inline bool fg_IsMalformedExtendedLengthPath(NStr::CStr const &_Path)
	{
		if (_Path.f_Cmp("//?/", 4) != 0)
			return false;

		NStr::CStr Rest = _Path.f_Extract(4);
		if (Rest.f_CmpNoCase("UNC/", 4) == 0)
			return Rest.f_GetLen() == 4 || fg_Const(Rest)[4] == '/';

		return !(Rest.f_GetLen() > 2 && fg_Const(Rest)[1] == ':' && fg_Const(Rest)[2] == '/');
	}

	/// Returns whether the argument _Path, as given, equals an entry of _ProtectedDirectories or starts with the entry followed by a separator. The
	/// keys of fg_ProtectedDirectoryComparePath are compared as strings ignoring ASCII case, so links are not resolved. An entry can end with a
	/// separator, and must not be a malformed extended-length path, see fg_IsMalformedExtendedLengthPath. An entry relative to a working directory,
	/// like Shared or C:Shared, protects nothing, and an argument relative to the working directory of a drive, like C: or C:Shared, never matches.
	inline bool fg_IsInProtectedDirectory(NStr::CStr const &_Path, NContainer::TCVector<NStr::CStr> const &_ProtectedDirectories, bool _bWin32PathNames)
	{
		// The key of the drive root C:/ is C:, so without this check the argument C:, which names the working directory of drive C, would match
		// a protected drive root
		bool bDriveRelative = _Path.f_GetLen() > 1 && fg_Const(_Path)[1] == ':' && (_Path.f_GetLen() == 2 || (fg_Const(_Path)[2] != '/' && fg_Const(_Path)[2] != '\\'));
		if (bDriveRelative)
			return false;

		NStr::CStr Path = fg_ProtectedDirectoryComparePath(_Path, _bWin32PathNames);

		for (auto const &ProtectedDirectory : _ProtectedDirectories)
		{
			// An entry relative to a working directory is ignored. On Windows an entry like /Symbols gets the drive or UNC server of the client's
			// working directory.
			bool bDriveRoot
				= ProtectedDirectory.f_GetLen() > 2 && fg_Const(ProtectedDirectory)[1] == ':' && (fg_Const(ProtectedDirectory)[2] == '/' || fg_Const(ProtectedDirectory)[2] == '\\')
			;
			if (!ProtectedDirectory.f_StartsWith("/") && !bDriveRoot)
				continue;

			NStr::CStr Directory = fg_ProtectedDirectoryComparePath(ProtectedDirectory, _bWin32PathNames);

			// Case is ignored also on case-sensitive filesystems, where a build volume has no directories whose names differ only in case
			if (Path.f_CmpNoCase(Directory) == 0)
				return true;

			// Only the key "/" ends with a separator
			NStr::CStr Prefix = Directory;
			if (!Prefix.f_EndsWith("/"))
				Prefix += "/";

			if (Path.f_StartsWithNoCase(Prefix))
				return true;
		}

		return false;
	}

	/// Returns the spelling of the protected directory argument _Param, as given, to forward to the build server, which runs the tool in a directory
	/// of its own. _Param is expanded on the client, which resolves ".." by name as the protected directory check does. On Windows the expansion
	/// prefixes a path that starts with a single separator with the drive letter or UNC server of the working directory. With _bWin32PathNames an
	/// argument whose names Win32 trims is forwarded with the trimmed names, since the tool can open it in extended-length form, where Win32 doesn't
	/// trim names. An argument in extended-length form keeps that form unless trimming changes its names. Windows paths are forwarded with
	/// backslashes as separators. _bWindowsPaths tells that the paths are Windows paths also when Win32 doesn't trim their names, as with
	/// DMibAlwaysUseLongWindowsPaths; _bWin32PathNames implies it.
	inline NStr::CStr fg_ProtectedDirectoryForwardPath(NStr::CStr const &_Param, bool _bWin32PathNames, bool _bWindowsPaths)
	{
		NStr::CStr Expanded = NFile::CFile::fs_GetExpandedPath(_Param, true);
		NStr::CStr Path = Expanded.f_TrimRight("/");

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

		// Windows reads a bare C: as the current directory of drive C, so a drive root keeps its separator
		if ((Path.f_GetLen() == 2 && fg_Const(Path)[1] == ':') || _Param.f_EndsWith("/") || _Param.f_EndsWith("\\"))
			Path += "/";

		// Windows paths are forwarded with native separators: the tool can pass the path to Win32 as it is, which doesn't convert slashes after an
		// extended-length prefix, and some tools, like the commands of cmd.exe, read a '/' in an argument as the start of an option
		if (_bWin32PathNames || _bWindowsPaths)
			NStr::fg_StrReplaceChar(Path, '/', '\\');

		return Path;
	}
}
