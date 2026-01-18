
#if __OBJC__ && __cplusplus

#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>

#elif defined(__cplusplus)

#if __has_include("llvm/ADT/BitVector.h")
	#include "llvm/ADT/BitVector.h"
#endif
#if __has_include("llvm/ADT/DenseMap.h")
	#include "llvm/ADT/DenseMap.h"
#endif
#if __has_include("llvm/ADT/DenseSet.h")
	#include "llvm/ADT/DenseSet.h"
#endif
#if __has_include("llvm/ADT/GraphTraits.h")
	#include "llvm/ADT/GraphTraits.h"
#endif
#if __has_include("llvm/ADT/PostOrderIterator.h")
	#include "llvm/ADT/PostOrderIterator.h"
#endif
#if __has_include("llvm/ADT/SmallPtrSet.h")
	#include "llvm/ADT/SmallPtrSet.h"
#endif
#if __has_include("llvm/ADT/SmallVector.h")
	#include "llvm/ADT/SmallVector.h"
#endif
#if __has_include("llvm/ADT/SparseBitVector.h")
	#include "llvm/ADT/SparseBitVector.h"
#endif
#if __has_include("llvm/ADT/Twine.h")
	#include "llvm/ADT/Twine.h"
#endif
#if __has_include("llvm/ADT/iterator_range.h")
	#include "llvm/ADT/iterator_range.h"
#endif
#if __has_include("llvm/Support/BlockFrequency.h")
	#include "llvm/Support/BlockFrequency.h"
#endif
#if __has_include("llvm/Support/BranchProbability.h")
	#include "llvm/Support/BranchProbability.h"
#endif
#if __has_include("llvm/Support/CommandLine.h")
	#include "llvm/Support/CommandLine.h"
#endif
#if __has_include("llvm/Support/DOTGraphTraits.h")
	#include "llvm/Support/DOTGraphTraits.h"
#endif
#if __has_include("llvm/Support/Debug.h")
	#include "llvm/Support/Debug.h"
#endif
#if __has_include("llvm/Support/Format.h")
	#include "llvm/Support/Format.h"
#endif
#if __has_include("llvm/Support/ScaledNumber.h")
	#include "llvm/Support/ScaledNumber.h"
#endif
#if __has_include("llvm/Support/raw_ostream.h")
	#include "llvm/Support/raw_ostream.h"
#endif
#if __has_include("llvm/ADT/StringSwitch.h")
	#include "llvm/ADT/StringSwitch.h"
#endif
#if __has_include("llvm/Option/ArgList.h")
	#include "llvm/Option/ArgList.h"
#endif
#if __has_include("llvm/TargetParser/Host.h")
	#include "llvm/TargetParser/Host.h"
#endif

#if __has_include("clang/Basic/DiagnosticGroups.inc") && __has_include("clang/Driver/Options.inc")
	#if __has_include("clang/Driver/Driver.h")
		#include "clang/Driver/Driver.h"
	#endif
	#if __has_include("clang/Driver/DriverDiagnostic.h")
		#include "clang/Driver/DriverDiagnostic.h"
	#endif
	#if __has_include("clang/Driver/Options.h")
		#include "clang/Driver/Options.h"
	#endif
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#else

#include <stdlib.h>

#endif
