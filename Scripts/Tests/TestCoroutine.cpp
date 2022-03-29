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

inline_always CTesting fg_TestFunctionInline()
{
	std::cout << "    fg_TestFunctionInline\n";
	return {};
}

int main()
{
	std::cout << "NoInline: Calling\n";
	fg_TestFunction();
	std::cout << "NoInline: Done\n";

	std::cout << "Inline: Calling\n";
	fg_TestFunctionInline();
	std::cout << "Inline: Done\n";

	return 0;
}
