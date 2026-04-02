// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <iostream>

#define inline_never __attribute__((noinline))
#define inline_always inline __attribute__((always_inline))

extern "C" void fg_TestInspect(void *_pThisFunction, void *_pCallSite)
{
	std::cout << "    Inspected\n";
}

struct [[clang::instrument_non_coroutine_function_enter("fg_TestInspect")]] CTesting
{
};

inline_never CTesting fg_TestFunction()
{
	std::cout << "    fg_TestFunction\n";
	return {};
}

[[clang::instrument_non_coroutine_function_enter_disable]] inline_never CTesting fg_TestFunctionDisabled()
{
	std::cout << "    fg_TestFunction\n";
	return {};
}

inline_always CTesting fg_TestFunctionInline()
{
	std::cout << "    fg_TestFunctionInline\n";
	return {};
}

[[clang::instrument_non_coroutine_function_enter_disable]] int main()
{
	std::cout << "NoInline: Calling\n";
	fg_TestFunction();
	std::cout << "NoInline: Done\n\n";

	std::cout << "Disabled: Calling\n";
	fg_TestFunctionDisabled();
	std::cout << "Disabled: Done\n\n";

	std::cout << "Inline: Calling\n";
	fg_TestFunctionInline();
	std::cout << "Inline: Done\n\n";

	auto fTestLambda = []() -> CTesting
		{
			std::cout << "    fTestLambda\n";
			return {};
		}
	;

	std::cout << "Lambda: Calling\n";
	fTestLambda();
	std::cout << "Lambda: Done\n\n";

	auto fTestLambdaDisabled = [] [[clang::instrument_non_coroutine_function_enter_disable]] () -> CTesting
		{
			std::cout << "    fTestLambdaDisabled\n";
			return {};
		}
	;

	std::cout << "LambdaDisabled: Calling\n";
	fTestLambdaDisabled();
	std::cout << "LambdaDisabled: Done\n\n";

	return 0;
}
