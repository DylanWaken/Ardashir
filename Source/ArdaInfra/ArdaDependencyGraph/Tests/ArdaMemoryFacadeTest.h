#pragma once

#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include <gtest/gtest.h>

namespace arda::memory_facade_tests
{
	/** Independently exercise each invalid operation because an edit retains its first failure. */
	template <class Operation, class Parameters>
	void ExpectRejectedOperation(FArdaDependencyGraph& Graph,
	    const Operation& Invoke,
	    Parameters ParametersValue,
	    const eastl::string& Name)
	{
		FArdaGraphEdit RejectedEdit(Graph);
		ASSERT_TRUE(RejectedEdit.GetStatus());
		EXPECT_FALSE(Invoke(RejectedEdit, eastl::move(ParametersValue), Name));
		EXPECT_FALSE(RejectedEdit.GetStatus());
		EXPECT_FALSE(RejectedEdit.Commit());
		EXPECT_TRUE(RejectedEdit.Cancel());
	}
}
