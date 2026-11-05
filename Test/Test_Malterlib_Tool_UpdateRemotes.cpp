// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Tool_TestHelpers.h"

namespace NMib::NTool
{
	using namespace NTestHelpers;

	struct CUpdateRemotes_Tests : CTest
	{
		void f_DoTests();
	};

	void CUpdateRemotes_Tests::f_DoTests()
	{
		DMibTestSuite("UpdateRemotes") -> TCFuture<void>
		{
			CRepositoryFixture Repo;
			co_await Repo.f_Init();

			CCheckoutFixture Checkout(Repo);
			co_await Checkout.f_Init({"A", "B"});
			co_await Checkout.f_MibSucceeds({"update-repos"});

			co_await Checkout.f_MibSucceeds({"status", "--update-remotes"});

			co_return {};
		};
	}

	DMibTestRegister(CUpdateRemotes_Tests, Malterlib::Tool);
}
