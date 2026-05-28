// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// MTool BuildAppHost — turns a copy of the .NET apphost.exe template into the
// native launcher for a .NET 5+ managed assembly. Mirrors what the .NET SDK's
// Microsoft.NET.HostModel.AppHost / Bundler do internally, so generators that
// drive csc.exe directly (e.g. the Ninja generator) don't need MSBuild to
// produce a runnable .exe — and can optionally produce a single-file bundle
// that embeds the managed assembly and runtimeconfig.json inside the .exe.
//
//   MTool BuildAppHost --template PATH --output PATH --assembly NAME
//                      [--gui]
//                      [--bundle-file PATH[,PATH]...]
//
// Without --bundle-file the apphost path placeholder is patched and we emit
// the .exe (the .dll and runtimeconfig.json are deployed as separate files).
// With --bundle-file (a comma-separated list of paths) we additionally append
// a .NET 8 single-file bundle (format version 6.0) — the apphost loads all
// entries from inside the .exe at runtime, no sibling files needed. File type
// is inferred from each path's extension (.dll → assembly, .runtimeconfig.json
// → runtimeconfig, .deps.json → depsjson, .pdb → symbols, otherwise unknown).

#include "Malterlib_Tool_App_MTool_Main.h"

#include <Mib/Cryptography/Hashes/SHA>
#include <Mib/Stream/Streams/Vector>

namespace
{
	// FileType byte values from Microsoft.NET.HostModel.Bundle.FileType.
	enum EBundleFileType : uint8
	{
		EBundleFileType_Unknown = 0,
		EBundleFileType_Assembly = 1,
		EBundleFileType_NativeBinary = 2,
		EBundleFileType_DepsJson = 3,
		EBundleFileType_RuntimeConfigJson = 4,
		EBundleFileType_Symbols = 5,
	};

	struct CBundleFile
	{
		CStr m_SrcPath;
		EBundleFileType m_Type = EBundleFileType_Unknown;
		CStr m_RelPath;
		CByteVector m_Contents;
		uint64 m_Offset = 0;
		uint64 m_Size = 0;
	};

	EBundleFileType fg_InferBundleFileType(CStr const &_Path)
	{
		if (_Path.f_EndsWithNoCase(".runtimeconfig.json"))
			return EBundleFileType_RuntimeConfigJson;
		if (_Path.f_EndsWithNoCase(".deps.json"))
			return EBundleFileType_DepsJson;
		if (_Path.f_EndsWithNoCase(".dll"))
			return EBundleFileType_Assembly;
		if (_Path.f_EndsWithNoCase(".pdb"))
			return EBundleFileType_Symbols;
		if (_Path.f_EndsWithNoCase(".exe") || _Path.f_EndsWithNoCase(".so") || _Path.f_EndsWithNoCase(".dylib"))
			return EBundleFileType_NativeBinary;
		return EBundleFileType_Unknown;
	}

	// .NET BinaryWriter writes string length as a 7-bit-encoded varint then the
	// UTF-8 bytes. Matches BinaryWriter.Write7BitEncodedInt byte-for-byte.
	void fg_FeedVarLenString(CBinaryStreamMemory<> &_Stream, CStr const &_Str)
	{
		uint32 nValue = uint32(_Str.f_GetLen());
		uint8 Header[5];
		umint nHeader = 0;
		while (nValue >= 0x80)
		{
			Header[nHeader++] = uint8((nValue & 0x7F) | 0x80);
			nValue >>= 7;
		}
		Header[nHeader++] = uint8(nValue);
		_Stream.f_FeedBytes(Header, nHeader);
		_Stream.f_FeedBytes(_Str.f_GetStr(), _Str.f_GetLen());
	}

	// Search the stream buffer for a byte pattern. Returns umint(-1) if not
	// found. Read-only — the stream's contents must not be modified during
	// the search.
	umint fg_FindPatternInStream(CBinaryStreamMemory<> const &_Stream, uint8 const *_pNeedle, umint _NeedleLen, umint _Start = 0)
	{
		uint8 const *pData = (uint8 const *)_Stream.f_GetBufferConst();
		umint nDataLen = umint(_Stream.f_GetLength());
		if (nDataLen < _NeedleLen || _Start > nDataLen - _NeedleLen)
			return umint(-1);
		for (umint i = _Start; i + _NeedleLen <= nDataLen; ++i)
		{
			bool bMatch = true;
			for (umint j = 0; j < _NeedleLen; ++j)
			{
				if (pData[i + j] != _pNeedle[j])
				{
					bMatch = false;
					break;
				}
			}
			if (bMatch)
				return i;
		}
		return umint(-1);
	}

	// RAII helper that restores a stream's position when it goes out of scope,
	// so random-access patches don't disturb the caller's append cursor.
	struct CStreamPositionGuard
	{
		CBinaryStreamMemory<> &m_Stream;
		NStream::CFilePos m_SavedPos;

		CStreamPositionGuard(CBinaryStreamMemory<> &_Stream)
			: m_Stream(_Stream)
			, m_SavedPos(_Stream.f_GetPosition())
		{
		}

		~CStreamPositionGuard()
		{
			m_Stream.f_SetPosition(m_SavedPos);
		}
	};

	// Search 64-byte AppBinaryPath placeholder. Replaces it with the managed-dll
	// name (UTF-8, null-terminated) and zeros the remainder of the 64-byte slot.
	void fg_PatchAppBinaryPath(CBinaryStreamMemory<> &_AppHost, CStr const &_ManagedDllName, CStr const &_TemplatePath)
	{
		static char const c_pPlaceholder[] = "c3ab8ff13720e8ad9047dd39466b3c8974e592c2fa383d4a3960714caef0c4f2";
		constexpr umint c_PlaceholderLen = sizeof(c_pPlaceholder) - 1;
		constexpr umint c_MaxPathLen = 1024;

		umint nFound = fg_FindPatternInStream(_AppHost, (uint8 const *)c_pPlaceholder, c_PlaceholderLen);
		if (nFound == umint(-1))
			DMibError("Could not find apphost path placeholder in template: '{}'"_f << _TemplatePath);

		umint nPathLen = _ManagedDllName.f_GetLen();
		if (nPathLen >= c_MaxPathLen)
			DMibError("Managed dll name too long ({} bytes; max {}): '{}'"_f << nPathLen << (c_MaxPathLen - 1) << _ManagedDllName);

		CStreamPositionGuard Guard(_AppHost);
		_AppHost.f_SetPosition(NStream::CFilePos(nFound));
		_AppHost.f_FeedBytes(_ManagedDllName.f_GetStr(), nPathLen);
		// Only zero-pad the remainder of the 64-byte placeholder slot when the
		// path is shorter than it; longer paths write into the apphost's
		// zero-initialized reserved buffer (matching Microsoft.NET.HostModel's
		// BinaryUtils.Pad0). Subtracting unconditionally would underflow umint.
		if (nPathLen < c_PlaceholderLen)
		{
			uint8 Zeros[c_PlaceholderLen] = {};
			_AppHost.f_FeedBytes(Zeros, c_PlaceholderLen - nPathLen);
		}
	}

	void fg_PatchSubsystemWindowsGUI(CBinaryStreamMemory<> &_AppHost, CStr const &_TemplatePath)
	{
		umint nDataLen = umint(_AppHost.f_GetLength());
		if (nDataLen < 0x40)
			DMibError("Apphost template too small to be a valid PE file: '{}'"_f << _TemplatePath);

		CStreamPositionGuard Guard(_AppHost);

		_AppHost.f_SetPosition(NStream::CFilePos(0x3C));
		uint32 LfaNew;
		_AppHost >> LfaNew;

		// Subsystem field is at offset 68 in the Optional Header for both PE32
		// and PE32+. Optional Header starts after the 4-byte PE signature and
		// 20-byte COFF file header.
		umint nSubsystemOffset = umint(LfaNew) + 4 + 20 + 68;
		if (nSubsystemOffset + 2 > nDataLen)
			DMibError("Apphost template appears truncated; cannot patch PE subsystem: '{}'"_f << _TemplatePath);

		_AppHost.f_SetPosition(NStream::CFilePos(nSubsystemOffset));
		_AppHost << uint16(2); // IMAGE_SUBSYSTEM_WINDOWS_GUI
	}

	// 32-byte SHA-256 of the literal string ".net core bundle" — present
	// verbatim in every apphost.exe shipped by the .NET SDK. The 8 bytes
	// immediately before this signature hold the bundle_header_offset (int64
	// little-endian), zero in the template, written here when bundling.
	uint8 const gc_BundleSignature[32] =
	{
		0x8B, 0x12, 0x02, 0xB9, 0x6A, 0x61, 0x20, 0x38,
		0x72, 0x7B, 0x93, 0x02, 0x14, 0xD7, 0xA0, 0x32,
		0x13, 0xF5, 0xB9, 0xE6, 0xEF, 0xAE, 0x33, 0x18,
		0xEE, 0x3B, 0x2D, 0xCE, 0x24, 0xB3, 0x6A, 0xAE
	};

	void fg_PatchBundleHeaderOffset(CBinaryStreamMemory<> &_AppHost, uint64 _ManifestOffset, CStr const &_TemplatePath)
	{
		// Start search at offset 8 — we patch the 8 bytes preceding the
		// signature, so the signature can't legally be at offset 0..7.
		umint nFound = fg_FindPatternInStream(_AppHost, gc_BundleSignature, sizeof(gc_BundleSignature), 8);
		if (nFound == umint(-1))
			DMibError("Could not find bundle header signature in apphost: '{}'"_f << _TemplatePath);

		// Patch the 8 bytes immediately preceding the signature with the int64
		// manifest offset (little-endian). Leave the signature bytes untouched.
		CStreamPositionGuard Guard(_AppHost);
		_AppHost.f_SetPosition(NStream::CFilePos(nFound - 8));
		_AppHost << _ManifestOffset;
	}

	// Derive a per-app deterministic BundleID by hashing the assembly name and
	// each entry's (type, relpath, size, contents). The .NET runtime treats
	// BundleID opaquely but uses it as the cache key when extracting native/
	// symbol entries to disk, so two distinct apps must not collide. Must be
	// called while m_Contents is still populated.
	CStr fg_ComputeBundleID(CStr const &_AssemblyName, TCVector<CBundleFile> const &_Files)
	{
		CHash_SHA256_16 Hash;
		Hash.f_AddData(_AssemblyName.f_GetStr(), _AssemblyName.f_GetLen());
		for (auto const &File : _Files)
		{
			uint8 Type = uint8(File.m_Type);
			Hash.f_AddData(&Type, sizeof(Type));
			Hash.f_AddData(File.m_RelPath.f_GetStr(), File.m_RelPath.f_GetLen());
			uint64 Size = File.m_Size;
			Hash.f_AddData(&Size, sizeof(Size));
			Hash.f_AddData(File.m_Contents.f_GetArray(), File.m_Contents.f_GetLen());
		}
		return Hash.f_GetDigest().f_GetString(); // 32 hex chars
	}
}

struct CTool_BuildAppHost : public CDistributedTool, public CAllowUnsafeThis
{
public:
	void f_Register
		(
			TCActor<CDistributedToolAppActor> const &_ToolActor
			, CDistributedAppCommandLineSpecification::CSection &o_ToolsSection
			, CDistributedAppCommandLineSpecification &o_CommandLine
			, NStr::CStr const &_ClassName
		)
	{
		if (fg_IsMalterlib())
			return;

		o_ToolsSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["BuildAppHost"]
					, "Description"_o=
						"Patch a .NET apphost.exe template into the launcher for a managed assembly.\n"
						"With one or more --bundle-file, additionally append a .NET 8 single-file\n"
						"bundle (format v6) embedding the managed assembly and runtimeconfig.json.\n"
					, "Category"_o= "Build"
					, "Options"_o=
					{
						"Template"_o=
						{
							"Names"_o= _o["--template"]
							, "Type"_o= ""
							, "Description"_o= "Path to the apphost.exe template (e.g. from packs/Microsoft.NETCore.App.Host.win-<arch>/<ver>/runtimes/win-<arch>/native/apphost.exe).\n"
						}
						, "Output"_o=
						{
							"Names"_o= _o["--output"]
							, "Type"_o= ""
							, "Description"_o= "Path of the .exe to write.\n"
						}
						, "AssemblyName"_o=
						{
							"Names"_o= _o["--assembly"]
							, "Type"_o= ""
							, "Description"_o= "Filename of the managed entry-point .dll (embedded in the apphost so hostfxr finds it).\n"
						}
						, "Gui?"_o=
						{
							"Names"_o= _o["--gui"]
							, "Default"_o= false
							, "Description"_o= "Patch the apphost's PE Subsystem field to Windows GUI (2). Default is Console (3).\n"
						}
						, "BundleFiles?"_o=
						{
							"Names"_o= _o["--bundle-file"]
							, "Type"_o= _o[""]
							, "Default"_o= _o[]
							, "Description"_o=
								"Comma-separated paths of files to embed in a single-file bundle.\n"
								"Type is inferred from each extension: .dll = assembly, .runtimeconfig.json\n"
								"= runtimeconfig, .deps.json = depsjson, .pdb = symbols, .exe/.so/.dylib =\n"
								"native, otherwise unknown. Omit to skip bundle generation. When bundling,\n"
								"the file list must include the managed entry-point .dll named in --assembly.\n"
						}
					}
				}
				, [](NEncoding::CEJsonSorted const _Params, TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await ECoroutineFlag_CaptureExceptions;

					CStr Template = _Params["Template"].f_String();
					CStr Output = _Params["Output"].f_String();
					CStr AssemblyName = _Params["AssemblyName"].f_String();
					bool bGui = _Params["Gui"].f_Boolean();
					TCVector<CStr> BundleFiles = _Params["BundleFiles"].f_StringArray();

					TCVector<CBundleFile> Files;
					for (auto const &Path : BundleFiles)
					{
						CBundleFile File;
						File.m_SrcPath = Path;
						File.m_Type = fg_InferBundleFileType(Path);
						File.m_RelPath = NFile::CFile::fs_GetFile(Path);
						Files.f_InsertLast(fg_Move(File));
					}

					CByteVector TemplateBytes = NFile::CFile::fs_ReadFile(Template);
					if (TemplateBytes.f_IsEmpty())
						DMibError("Failed to read apphost template: '{}'"_f << Template);

					CBinaryStreamMemory<> AppHost;
					AppHost.f_Open(fg_Move(TemplateBytes));
					AppHost.f_SetPosition(AppHost.f_GetLength());

					fg_PatchAppBinaryPath(AppHost, AssemblyName, Template);

					if (bGui)
						fg_PatchSubsystemWindowsGUI(AppHost, Template);

					if (!Files.f_IsEmpty())
					{
						bool bAssemblyFound = false;
						for (auto const &File : Files)
						{
							if (File.m_Type == EBundleFileType_Assembly && File.m_RelPath == AssemblyName)
							{
								bAssemblyFound = true;
								break;
							}
						}
						if (!bAssemblyFound)
							DMibError("Bundle does not contain managed entry-point assembly '{}'; add it to --bundle-file"_f << AssemblyName);

						for (auto &File : Files)
						{
							File.m_Contents = NFile::CFile::fs_ReadFile(File.m_SrcPath);
							File.m_Size = File.m_Contents.f_GetLen();
							if (File.m_Size == 0)
								DMibError("Bundle source file is empty or missing: '{}'"_f << File.m_SrcPath);
						}

						// Compute BundleID before contents are consumed by the
						// append loop below.
						CStr BundleID = fg_ComputeBundleID(AssemblyName, Files);

						// Ninja .NET pipeline runs on Windows hosts only — the
						// apphost pack we patch is per-target-arch, but every
						// Windows variant uses the same 4096-byte alignment.
						uint64 const c_AssemblyAlignment = 4096;

						for (auto &File : Files)
						{
							if (File.m_Type == EBundleFileType_Assembly)
							{
								uint64 nCurrent = AppHost.f_GetLength();
								uint64 nAligned = (nCurrent + c_AssemblyAlignment - 1) / c_AssemblyAlignment * c_AssemblyAlignment;
								if (nAligned > nCurrent)
								{
									AppHost.f_SetLength(NStream::CFilePos(nAligned));
									AppHost.f_SetPosition(NStream::CFilePos(nAligned));
								}
							}
							File.m_Offset = AppHost.f_GetPosition();
							AppHost.f_FeedBytes(File.m_Contents.f_GetArray(), umint(File.m_Size));
							// We don't compress — the runtime treats entries
							// with CompressedSize == 0 as stored uncompressed.
							File.m_Contents.f_Clear();
						}

						uint64 nManifestOffset = AppHost.f_GetPosition();

						// Manifest header (.NET 8 = bundle format v6.0).
						AppHost << uint32(6); // MajorVersion
						AppHost << uint32(0); // MinorVersion
						AppHost << uint32(Files.f_GetLen()); // FileCount

						// BundleID derived from the bundle contents so each app
						// gets a unique value (the runtime uses it as the cache
						// key when extracting native/symbol entries).
						fg_FeedVarLenString(AppHost, BundleID);

						// v6 header carries deps.json / runtimeconfig.json
						// offsets explicitly so hostfxr finds them without
						// scanning the file table.
						uint64 nDepsJsonOffset = 0, nDepsJsonSize = 0;
						uint64 nRuntimeCfgOffset = 0, nRuntimeCfgSize = 0;
						for (auto const &File : Files)
						{
							if (File.m_Type == EBundleFileType_DepsJson)
							{
								nDepsJsonOffset = File.m_Offset;
								nDepsJsonSize = File.m_Size;
							}
							else if (File.m_Type == EBundleFileType_RuntimeConfigJson)
							{
								nRuntimeCfgOffset = File.m_Offset;
								nRuntimeCfgSize = File.m_Size;
							}
						}
						AppHost << nDepsJsonOffset;
						AppHost << nDepsJsonSize;
						AppHost << nRuntimeCfgOffset;
						AppHost << nRuntimeCfgSize;
						AppHost << uint64(0); // Flags

						for (auto const &File : Files)
						{
							AppHost << File.m_Offset;
							AppHost << File.m_Size;
							AppHost << uint64(0); // CompressedSize
							AppHost << uint8(File.m_Type);
							fg_FeedVarLenString(AppHost, File.m_RelPath);
						}

						fg_PatchBundleHeaderOffset(AppHost, nManifestOffset, Template);
					}

					NFile::CFile::fs_CopyFileDiff(AppHost.f_MoveVector(), Output, NTime::CTime::fs_NowUTC());
					co_return 0u;
				}
			)
		;
	}
};

DMibRuntimeClass(NMib::NConcurrency::CDistributedTool, CTool_BuildAppHost);
