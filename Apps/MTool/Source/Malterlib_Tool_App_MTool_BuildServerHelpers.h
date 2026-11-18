// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/String/String>
#include <Mib/Container/Vector>
#include <Mib/File/File>

namespace NMib::NTool
{
	// With DMibAlwaysUseLongWindowsPaths the file API opens ordinary paths in extended-length form, where Win32 doesn't trim trailing periods and spaces
#if defined(DPlatformFamily_Windows) && !defined(DMibAlwaysUseLongWindowsPaths)
	inline constexpr bool gc_bWin32PathNames = true;
#else
	inline constexpr bool gc_bWin32PathNames = false;
#endif

#ifdef DPlatformFamily_Windows
	inline constexpr bool gc_bWindowsPaths = true;
#else
	inline constexpr bool gc_bWindowsPaths = false;
#endif

	NStr::CStr fg_ProtectedDirectoryComparePath(NStr::CStr const &_Path, bool _bWin32PathNames);
	bool fg_IsMalformedExtendedLengthPath(NStr::CStr const &_Path);
	bool fg_IsInProtectedDirectory(NStr::CStr const &_Path, NContainer::TCVector<NStr::CStr> const &_ProtectedDirectories, bool _bWin32PathNames = gc_bWin32PathNames);
	NStr::CStr fg_ProtectedDirectoryForwardPath(NStr::CStr const &_Param, bool _bWin32PathNames = gc_bWin32PathNames, bool _bWindowsPaths = gc_bWindowsPaths);
}

#include "Malterlib_Tool_App_MTool_BuildServerHelpers.hpp"
