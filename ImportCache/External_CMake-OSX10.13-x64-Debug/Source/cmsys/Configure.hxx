/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing#kwsys for details.  */
#ifndef cmsys_Configure_hxx
#define cmsys_Configure_hxx

/* Include C configuration.  */
#include <cmsys/Configure.h>

/* Whether wstring is available.  */
#define cmsys_STL_HAS_WSTRING 1
/* Whether <ext/stdio_filebuf.h> is available. */
#define cmsys_CXX_HAS_EXT_STDIO_FILEBUF_H                         \
  0

/* If building a C++ file in kwsys itself, give the source file
   access to the macros without a configured namespace.  */
#if defined(KWSYS_NAMESPACE)
#if !cmsys_NAME_IS_KWSYS
#define kwsys cmsys
#endif
#define KWSYS_NAME_IS_KWSYS cmsys_NAME_IS_KWSYS
#define KWSYS_STL_HAS_WSTRING cmsys_STL_HAS_WSTRING
#define KWSYS_CXX_HAS_EXT_STDIO_FILEBUF_H                                     \
  cmsys_CXX_HAS_EXT_STDIO_FILEBUF_H
#endif

#endif
